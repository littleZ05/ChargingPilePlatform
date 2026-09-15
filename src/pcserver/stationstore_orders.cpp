#include "stationstore.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>
#include <cmath>
namespace {
constexpr double kNewUserBonusYuan = 100.0;
QString queryError(const QSqlQuery &query) { return query.lastError().text(); }
}
namespace pcserver {
bool StationStore::settleChargingOrderByCode(const QString &pileCode, double kwh,
                                             double amount, int *orderIdOut,
                                             double *balanceOut, QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }
    QString innerErr;
    QString txnErr;
    bool ok = runInTransaction([&](QSqlDatabase &db) {
        QSqlQuery q(db);
        // 1) 定位电桩
        q.prepare(QStringLiteral("SELECT id, station_id, state FROM piles WHERE code = ?"));
        q.addBindValue(pileCode);
        if (!q.exec() || !q.next()) {
            innerErr = QStringLiteral("电桩编码不存在：%1").arg(pileCode);
            return false;
        }
        const int pileId    = q.value(0).toInt();
        const int stationId = q.value(1).toInt();
        const int pileState = q.value(2).toInt();

        // 2) 找到“充电中”订单；桩为充电中但无订单视为脏数据，拒绝结算
        QSqlQuery orderQ(db);
        orderQ.prepare(QStringLiteral(
            "SELECT id, user_id FROM orders WHERE pile_id = ? AND state = 0 "
            "ORDER BY id DESC LIMIT 1"));
        orderQ.addBindValue(pileId);
        if (!orderQ.exec() || !orderQ.next()) {
            innerErr = pileState == static_cast<int>(cp::PileState::Charging)
                           ? QStringLiteral("电桩处于充电中但缺少对应订单，请先建单")
                           : QStringLiteral("该桩当前无充电中订单，无法结算上报");
            return false;
        }
        const int orderId = orderQ.value(0).toInt();
        const int userId  = orderQ.value(1).toInt();

        // 3) 完成订单并落库
        const double price = kwh > 0.0 ? amount / kwh : 0.0;
        QSqlQuery up(db);
        up.prepare(QStringLiteral(
            "UPDATE orders SET state = 1, end_time = datetime('now','localtime'), "
            " kwh = ?, price = ?, amount = ? WHERE id = ?"));
        up.addBindValue(kwh);
        up.addBindValue(price);
        up.addBindValue(amount);
        up.addBindValue(orderId);
        if (!up.exec()) {
            innerErr = QStringLiteral("订单完成失败：%1").arg(up.lastError().text());
            return false;
        }

        // 4) 扣减用户余额并取最新余额
        //    先做余额校验：不足时给出明确业务原因，而不是让 balance>=0 约束抛底层错误
        QSqlQuery balCheck(db);
        balCheck.prepare(QStringLiteral("SELECT COALESCE(balance,0) FROM users WHERE id = ?"));
        balCheck.addBindValue(userId);
        const double currentBalance =
            (balCheck.exec() && balCheck.next()) ? balCheck.value(0).toDouble() : 0.0;
        if (currentBalance + 0.0001 < amount) {
            innerErr = QStringLiteral("余额不足：当前 ¥%1，本次需 ¥%2，请先充值")
                           .arg(currentBalance, 0, 'f', 2)
                           .arg(amount, 0, 'f', 2);
            return false;
        }

        QSqlQuery bal(db);
        bal.prepare(QStringLiteral("UPDATE users SET balance = balance - ? WHERE id = ?"));
        bal.addBindValue(amount);
        bal.addBindValue(userId);
        if (!bal.exec()) {
            innerErr = QStringLiteral("余额扣减失败：%1").arg(bal.lastError().text());
            return false;
        }
        QSqlQuery balQ(db);
        balQ.prepare(QStringLiteral("SELECT balance FROM users WHERE id = ?"));
        balQ.addBindValue(userId);
        balQ.exec();
        double newBalance = 0.0;
        if (balQ.next())
            newBalance = balQ.value(0).toDouble();

        // 5) 释放电桩并累计次数/时长（时长在 C++ 计算并钳制非负，避免 CHECK 失败）
        QSqlQuery startQ(db);
        startQ.prepare(QStringLiteral("SELECT start_time FROM orders WHERE id = ?"));
        startQ.addBindValue(orderId);
        qint64 durationSec = 0;
        if (startQ.exec() && startQ.next()) {
            const QDateTime start =
                QDateTime::fromString(startQ.value(0).toString(),
                                      QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (start.isValid()) {
                const qint64 secs = start.secsTo(QDateTime::currentDateTime());
                durationSec = secs > 0 ? secs : 0;
            }
        }
        QSqlQuery pile(db);
        pile.prepare(QStringLiteral(
            "UPDATE piles SET state = 0, charge_count = charge_count + 1, "
            " charge_seconds = charge_seconds + ? "
            "WHERE id = ?"));
        pile.addBindValue(durationSec);
        pile.addBindValue(pileId);
        if (!pile.exec()) {
            innerErr = QStringLiteral("电桩状态更新失败：%1").arg(pile.lastError().text());
            return false;
        }

        if (orderIdOut)  *orderIdOut  = orderId;
        if (balanceOut)  *balanceOut  = newBalance;
        if (!refreshOnlineRate(stationId, &innerErr))
            return false;
        return true;
    }, &txnErr);
    if (!ok) {
        if (error)
            *error = innerErr.isEmpty()
                         ? (txnErr.isEmpty() ? QStringLiteral("结算失败") : txnErr)
                         : innerErr;
        return false;
    }
    return true;
}

bool StationStore::currentPriceOf(int stationId, double *priceOut, double *discountOut,
                                  bool *onSaleOut, QString *error) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT s.base_price, COALESCE(m.discount, 1.0) "
        "FROM stations s LEFT JOIN marketing_strategy m "
        "  ON m.station_id = s.id AND m.is_active = 1 "
        "WHERE s.id = ? ORDER BY m.id DESC LIMIT 1"));
    q.addBindValue(stationId);
    if (!q.exec() || !q.next()) {
        if (error)
            *error = QStringLiteral("未找到电站：id=%1").arg(stationId);
        return false;
    }
    const double base = q.value(0).toDouble();
    double discount = q.value(1).toDouble();
    if (discount <= 0.0)
        discount = 1.0;
    if (priceOut)    *priceOut = base * discount;
    if (discountOut) *discountOut = discount;
    if (onSaleOut)   *onSaleOut = discount < 0.999;
    return true;
}

bool StationStore::startChargingOrder(const QString &phone, const QString &pileCode,
                                      int *orderIdOut, int *stationIdOut, int *pileIdOut,
                                      double *priceOut, QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }

    QString innerErr;
    QString txnErr;
    bool ok = runInTransaction([&](QSqlDatabase &db) {
        QSqlQuery uq(db);
        uq.prepare(QStringLiteral("SELECT id, COALESCE(status, 0) FROM users WHERE phone = ?"));
        uq.addBindValue(phone);
        if (!uq.exec() || !uq.next()) {
            innerErr = QStringLiteral("用户不存在，请先登录：%1").arg(phone);
            return false;
        }
        const int userId = uq.value(0).toInt();
        if (uq.value(1).toInt() != 0) {
            innerErr = QStringLiteral("账号已被冻结，无法发起充电（请联系管理员解冻）");
            return false;
        }

        QSqlQuery pq(db);
        pq.prepare(QStringLiteral("SELECT id, station_id, state FROM piles WHERE code = ?"));
        pq.addBindValue(pileCode);
        if (!pq.exec() || !pq.next()) {
            innerErr = QStringLiteral("电桩编码不存在：%1").arg(pileCode);
            return false;
        }
        const int pileId    = pq.value(0).toInt();
        const int stationId = pq.value(1).toInt();
        const int pileState = pq.value(2).toInt();
        if (pileState == static_cast<int>(cp::PileState::Fault)) {
            innerErr = QStringLiteral("电桩处于故障状态，无法发起充电：%1").arg(pileCode);
            return false;
        }
        if (pileState == static_cast<int>(cp::PileState::Charging)) {
            innerErr = QStringLiteral("电桩正在充电中，请选择其他空闲电桩：%1").arg(pileCode);
            return false;
        }

        QSqlQuery openQ(db);
        openQ.prepare(QStringLiteral(
            "SELECT id FROM orders WHERE pile_id = ? AND state = 0 LIMIT 1"));
        openQ.addBindValue(pileId);
        if (openQ.exec() && openQ.next()) {
            innerErr = QStringLiteral("该电桩存在未完成订单，请先结算");
            return false;
        }

        // 本次执行价 = 站基础价 × 当前生效折扣（创新点1 直接参与结算）
        QSqlQuery priceQ(db);
        priceQ.prepare(QStringLiteral(
            "SELECT s.base_price, COALESCE((SELECT m.discount FROM marketing_strategy m "
            " WHERE m.station_id = s.id AND m.is_active = 1 ORDER BY m.id DESC LIMIT 1), 1.0) "
            "FROM stations s WHERE s.id = ?"));
        priceQ.addBindValue(stationId);
        double unitPrice = 1.0;
        if (priceQ.exec() && priceQ.next()) {
            const double base = priceQ.value(0).toDouble();
            double discount = priceQ.value(1).toDouble();
            if (discount <= 0.0)
                discount = 1.0;
            unitPrice = base * discount;
        }

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO orders(user_id, pile_id, station_id, start_time, kwh, price, amount, state) "
            "VALUES(?, ?, ?, datetime('now','localtime'), 0, ?, 0, 0)"));
        ins.addBindValue(userId);
        ins.addBindValue(pileId);
        ins.addBindValue(stationId);
        ins.addBindValue(unitPrice);
        if (!ins.exec()) {
            innerErr = QStringLiteral("充电建单失败：%1").arg(ins.lastError().text());
            return false;
        }
        const int newOrderId = ins.lastInsertId().toInt();

        QSqlQuery up(db);
        up.prepare(QStringLiteral("UPDATE piles SET state = ? WHERE id = ?"));
        up.addBindValue(static_cast<int>(cp::PileState::Charging));
        up.addBindValue(pileId);
        if (!up.exec()) {
            innerErr = QStringLiteral("电桩状态更新失败：%1").arg(up.lastError().text());
            return false;
        }

        if (!refreshOnlineRate(stationId, &innerErr))
            return false;

        if (orderIdOut)   *orderIdOut   = newOrderId;
        if (stationIdOut) *stationIdOut = stationId;
        if (pileIdOut)    *pileIdOut    = pileId;
        if (priceOut)     *priceOut     = unitPrice;
        return true;
    }, &txnErr);

    if (!ok) {
        if (error)
            *error = innerErr.isEmpty()
                         ? (txnErr.isEmpty() ? QStringLiteral("充电建单失败") : txnErr)
                         : innerErr;
        return false;
    }
    return true;
}

bool StationStore::userLoginByPhone(const QString &phone, int *userIdOut,
                                    QString *nicknameOut, double *balanceOut,
                                    int *statusOut, bool *createdOut,
                                    QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }
    bool created = false;
    int userId = 0;
    double balance = 0.0;
    int status = 0;
    QString nickname;

    bool ok = runInTransaction([&](QSqlDatabase &db) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT id,nickname,balance,status FROM users WHERE phone=?"));
        q.addBindValue(phone);
        if (!q.exec()) {
            if (error) *error = QStringLiteral("查询用户失败：%1")
                                       .arg(q.lastError().text());
            return false;
        }
        if (!q.next()) {
            // 未注册：按说明书自动注册（昵称=用户+手机号后4位）。
            // 注册即赠送演示初始余额（NO.6）“注册即生成昵称与初始余额”，
            // 否则新用户余额为 0，第一次结算会因 balance>=0 约束失败。
            created = true;
            nickname = QStringLiteral("用户%1").arg(phone.right(4));
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral(
                "INSERT INTO users(phone,nickname,balance,status) VALUES(?,?,?,0)"));
            ins.addBindValue(phone);
            ins.addBindValue(nickname);
            ins.addBindValue(kNewUserBonusYuan);
            if (!ins.exec()) {
                if (error) *error = QStringLiteral("自动注册失败：%1")
                                           .arg(ins.lastError().text());
                return false;
            }
            userId = ins.lastInsertId().toInt();
            balance = kNewUserBonusYuan;
            status = 0;
        } else {
            created = false;
            userId  = q.value(0).toInt();
            nickname = q.value(1).toString();
            balance  = q.value(2).toDouble();
            status   = q.value(3).toInt();
        }
        return true;
    }, error);

    if (!ok)
        return false;
    if (createdOut) *createdOut = created;
    if (userIdOut)  *userIdOut  = userId;
    if (nicknameOut)*nicknameOut = nickname;
    if (balanceOut) *balanceOut  = balance;
    if (statusOut)  *statusOut   = status;
    return true;
}

bool StationStore::rechargeBalance(const QString &phone, double amount,
                                   double *balanceOut, QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }
    if (!std::isfinite(amount) || amount <= 0.0 || amount > 10000.0) {
        if (error) *error = QStringLiteral("充值金额须在 0.01 ~ 10000 元之间");
        return false;
    }

    QString innerErr;
    double newBalance = 0.0;
    bool ok = runInTransaction([&](QSqlDatabase &db) {
        QSqlQuery up(db);
        up.prepare(QStringLiteral("UPDATE users SET balance = balance + ? WHERE phone = ?"));
        up.addBindValue(amount);
        up.addBindValue(phone);
        if (!up.exec()) {
            innerErr = QStringLiteral("充值失败：%1").arg(up.lastError().text());
            return false;
        }
        if (up.numRowsAffected() == 0) {
            innerErr = QStringLiteral("用户不存在，请先登录：%1").arg(phone);
            return false;
        }
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COALESCE(balance,0) FROM users WHERE phone = ?"));
        q.addBindValue(phone);
        if (q.exec() && q.next())
            newBalance = q.value(0).toDouble();
        return true;
    }, error);

    if (!ok) {
        if (error && !innerErr.isEmpty())
            *error = innerErr;
        return false;
    }
    if (balanceOut)
        *balanceOut = newBalance;
    return true;
}

bool StationStore::refreshOnlineRate(int stationId, QString *error)
{
    QSqlQuery countQuery(m_db);
    countQuery.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(CASE WHEN state <> 2 THEN 1 ELSE 0 END), 0) "
        "FROM piles WHERE station_id = ?"));
    countQuery.addBindValue(stationId);
    if (!countQuery.exec() || !countQuery.next()) {
        if (error) *error = QStringLiteral("统计电桩在线数失败：%1").arg(queryError(countQuery));
        return false;
    }
    const int total   = countQuery.value(0).toInt();
    const int online  = countQuery.value(1).toInt();
    const double rate = total > 0 ? 100.0 * online / total : 0.0;

    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE stations SET online_rate = ? WHERE id = ?"));
    update.addBindValue(rate);
    update.addBindValue(stationId);
    if (!update.exec()) {
        if (error) *error = QStringLiteral("更新电站在线率失败：%1").arg(queryError(update));
        return false;
    }
    return true;
}

QString StationStore::pileStateText(cp::PileState state)
{
    switch (state) {
    case cp::PileState::Idle:
        return QStringLiteral("闲置");
    case cp::PileState::Charging:
        return QStringLiteral("充电中");
    case cp::PileState::Fault:
        return QStringLiteral("故障");
    }
    return QStringLiteral("未知");
}

cp::PileState StationStore::nextSimulatedState(cp::PileState current)
{
    switch (current) {
    case cp::PileState::Idle:
        return cp::PileState::Charging;
    case cp::PileState::Charging:
        return cp::PileState::Idle;
    case cp::PileState::Fault:
        return cp::PileState::Idle;
    }
    return cp::PileState::Idle;
}

} // namespace pcserver
