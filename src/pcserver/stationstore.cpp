#include "stationstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace {

int nextConnectionSeq()
{
    static int seq = 0;
    return ++seq;
}

/** 把 schema.sql 按语句拆分执行（过滤 -- 注释行），避免多语句混在一起执行失败 */
QStringList splitSqlStatements(const QString &sql)
{
    QStringList statements;
    QString current;
    bool insideTriggerBody = false;
    const QStringList lines = sql.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("--")))
            continue; // 注释行不参与执行
        current += rawLine + QLatin1Char('\n');

        if (insideTriggerBody) {
            // BEGIN ... END 之间的分号属于触发器过程体，不能按普通语句切分
            if (line.compare(QStringLiteral("END;"), Qt::CaseInsensitive) == 0) {
                statements << current;
                current.clear();
                insideTriggerBody = false;
            }
            continue;
        }

        if (line.compare(QStringLiteral("BEGIN"), Qt::CaseInsensitive) == 0) {
            insideTriggerBody = true;
        } else if (line.endsWith(QLatin1Char(';'))) {
            statements << current;
            current.clear();
        }
    }
    if (!current.trimmed().isEmpty())
        statements << current;
    return statements;
}

QString queryError(const QSqlQuery &q)
{
    return q.lastError().text();
}

} // namespace

namespace pcserver {

StationStore::StationStore()
    : m_connectionName(QStringLiteral("cp_stationstore_%1").arg(nextConnectionSeq()))
{
}

StationStore::~StationStore()
{
    close();
}

bool StationStore::open(const QString &dbPath, QString *error)
{
    close();
    const QFileInfo dbInfo(dbPath);
    QDir().mkpath(dbInfo.absolutePath());
    QFile dbFile(dbInfo.absoluteFilePath());
    if (!dbFile.exists()) {
        if (!dbFile.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("无法创建数据库文件 %1：%2")
                             .arg(dbInfo.absoluteFilePath(), dbFile.errorString());
            return false;
        }
        dbFile.close();
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbInfo.absoluteFilePath());
    if (!m_db.open()) {
        if (error)
            *error = QStringLiteral("无法打开数据库 %1：%2")
                         .arg(dbInfo.absoluteFilePath(), m_db.lastError().text());
        return false;
    }
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    if (!executeSchema(error)) {
        close();
        return false;
    }
    return true;
}

void StationStore::close()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool StationStore::runInTransaction(const std::function<bool(QSqlDatabase &)> &fn,
                                    QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }
    if (!fn) {
        if (error) *error = QStringLiteral("事务回调为空");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        if (error) *error = QStringLiteral("开启事务失败：%1").arg(db.lastError().text());
        return false;
    }

    if (!fn(db)) {
        db.rollback();
        if (error) *error = QStringLiteral("事务回调失败，已回滚");
        return false;
    }
    if (!db.commit()) {
        db.rollback();
        if (error) *error = QStringLiteral("提交事务失败，已回滚：%1")
                                .arg(db.lastError().text());
        return false;
    }
    return true;
}

bool StationStore::integrityCheck(QString *report)
{
    if (!isOpen()) {
        if (report) *report = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA integrity_check"))) {
        if (report) *report = QStringLiteral("完整性检查执行失败：%1")
                                  .arg(query.lastError().text());
        return false;
    }

    QStringList lines;
    while (query.next())
        lines << query.value(0).toString();
    const QString text = lines.join(QLatin1Char('\n'));
    if (report)
        *report = text;
    return text.trimmed() == QStringLiteral("ok");
}

bool StationStore::backupTo(const QString &destPath, QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }
    if (destPath.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("备份路径不能为空");
        return false;
    }
    // 路径将进入 VACUUM INTO 的字符串字面量；单引号可破坏 SQL 边界，直接拒绝
    if (destPath.contains(QLatin1Char('\''))) {
        if (error) *error = QStringLiteral("备份路径不能包含单引号");
        return false;
    }

    QString escaped = destPath;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped))) {
        if (error) *error = QStringLiteral("备份失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool StationStore::execPrepared(const QString &sql, const QVariantList &binds,
                                QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec()) {
        if (error) *error = QStringLiteral("SQL 执行失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool StationStore::executeSchema(QString *error)
{
    QFile schemaFile(QStringLiteral(":/database/schema.sql"));
    if (!schemaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("读取内置 schema.sql 失败：%1").arg(schemaFile.errorString());
        return false;
    }
    const QString sql = QString::fromUtf8(schemaFile.readAll());

    const QStringList statements = splitSqlStatements(sql);
    for (const QString &statement : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(statement)) {
            if (error)
                *error = QStringLiteral("执行建表语句失败：%1\nSQL: %2")
                             .arg(queryError(q), statement.trimmed());
            return false;
        }
    }
    return true;
}

bool StationStore::validateInput(const QString &name,
                                 const QString &address,
                                 double longitude,
                                 double latitude,
                                 int pileCount,
                                 QString *error)
{
    if (name.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("站名不能为空");
        return false;
    }
    if (address.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("地址不能为空");
        return false;
    }
    if (longitude < -180.0 || longitude > 180.0) {
        if (error) *error = QStringLiteral("经度须在 -180 ~ 180 之间");
        return false;
    }
    if (latitude < -90.0 || latitude > 90.0) {
        if (error) *error = QStringLiteral("纬度须在 -90 ~ 90 之间");
        return false;
    }
    if (pileCount < 1 || pileCount > 100) {
        if (error) *error = QStringLiteral("电桩数量须在 1 ~ 100 之间");
        return false;
    }
    return true;
}

bool StationStore::seedDemoIfEmpty(QString *error)
{
    QSqlQuery countQuery(m_db);
    if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM stations"))) {
        if (error) *error = QStringLiteral("检查演示数据失败：%1").arg(queryError(countQuery));
        return false;
    }
    countQuery.next();
    if (countQuery.value(0).toInt() > 0)
        return true;

    struct DemoStation {
        QString name;
        QString address;
        double longitude;
        double latitude;
        int pileCount;
    };
    const QVector<DemoStation> demos = {
        { QStringLiteral("东软软件园充电站"), QStringLiteral("沈阳市浑南区智慧二街 100 号"),
          123.495840, 41.715170, 10 },
        { QStringLiteral("沈阳奥体中心充电站"), QStringLiteral("沈阳市浑南区浑南中路 30 号"),
          123.472100, 41.728300, 12 },
        { QStringLiteral("中街大悦城充电站"), QStringLiteral("沈阳市大东区小东路 10 号"),
          123.465300, 41.799300, 6 },
    };

    for (const DemoStation &d : demos) {
        QString addError;
        const int id = addStation(d.name, d.address, d.longitude, d.latitude,
                                  d.pileCount, &addError);
        if (id < 0) {
            if (error) *error = QStringLiteral("写入演示电站失败（%1）：%2").arg(d.name, addError);
            return false;
        }
    }
    return true;
}

QVector<StationInfo> StationStore::listStations()
{
    QVector<StationInfo> result;
    QSqlQuery q(m_db);
    const QString sql = QStringLiteral(
        "SELECT s.id, s.name, s.address, s.longitude, s.latitude, "
        "       COUNT(p.id) AS total_cnt, "
        "       COALESCE(SUM(CASE WHEN p.state = 0 THEN 1 ELSE 0 END), 0) AS idle_cnt, "
        "       COALESCE(SUM(CASE WHEN p.state <> 2 THEN 1 ELSE 0 END), 0) AS online_cnt, "
        "       s.base_price, "
        "       COALESCE((SELECT m.discount FROM marketing_strategy m "
        "                    WHERE m.station_id = s.id AND m.is_active = 1 "
        "                    ORDER BY m.id DESC LIMIT 1), 1.0) AS discount "
        "FROM stations s "
        "LEFT JOIN piles p ON p.station_id = s.id "
        "GROUP BY s.id "
        "ORDER BY s.id");
    if (!q.exec(sql))
        return result;

    while (q.next()) {
        StationInfo info;
        info.id         = q.value(0).toInt();
        info.name       = q.value(1).toString();
        info.address    = q.value(2).toString();
        info.longitude  = q.value(3).toDouble();
        info.latitude   = q.value(4).toDouble();
        info.totalPiles = q.value(5).toInt();
        info.idlePiles  = q.value(6).toInt();
        info.onlinePiles = q.value(7).toInt();
        info.basePrice  = q.value(8).toDouble();
        const double discount = q.value(9).toDouble();
        info.discount = discount;
        info.onSale   = discount < 1.0 - 1e-9;
        info.currentPrice = info.basePrice * discount;
        info.onlineRate = info.totalPiles > 0
                              ? 100.0 * info.onlinePiles / info.totalPiles
                              : 0.0;
        result.append(info);
    }
    return result;
}

QVector<PileInfo> StationStore::listPiles(int stationId)
{
    QVector<PileInfo> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, station_id, code, type, power_kw, state, "
        "       charge_count, charge_seconds "
        "FROM piles WHERE station_id = ? ORDER BY id"));
    q.addBindValue(stationId);
    if (!q.exec())
        return result;

    while (q.next()) {
        PileInfo pile;
        pile.id            = q.value(0).toInt();
        pile.stationId     = q.value(1).toInt();
        pile.code          = q.value(2).toString();
        pile.type          = q.value(3).toString();
        pile.powerKw       = q.value(4).toDouble();
        pile.state         = static_cast<cp::PileState>(q.value(5).toInt());
        pile.chargeCount   = q.value(6).toInt();
        pile.chargeSeconds = q.value(7).toLongLong();
        result.append(pile);
    }
    return result;
}

bool StationStore::findPileByCode(const QString &code, PileInfo *out)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, station_id, code, type, power_kw, state, "
        "       charge_count, charge_seconds "
        "FROM piles WHERE code = ?"));
    q.addBindValue(code.trimmed());
    if (!q.exec() || !q.next())
        return false;

    if (out) {
        out->id            = q.value(0).toInt();
        out->stationId     = q.value(1).toInt();
        out->code          = q.value(2).toString();
        out->type          = q.value(3).toString();
        out->powerKw       = q.value(4).toDouble();
        out->state         = static_cast<cp::PileState>(q.value(5).toInt());
        out->chargeCount   = q.value(6).toInt();
        out->chargeSeconds = q.value(7).toLongLong();
    }
    return true;
}

int StationStore::addStation(const QString &name,
                             const QString &address,
                             double longitude,
                             double latitude,
                             int pileCount,
                             QString *error)
{
    QString inputError;
    if (!validateInput(name, address, longitude, latitude, pileCount, &inputError)) {
        if (error) *error = inputError;
        return -1;
    }
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return -1;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        if (error) *error = QStringLiteral("开启事务失败：%1").arg(db.lastError().text());
        return -1;
    }

    QSqlQuery insertStation(db);
    insertStation.prepare(QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude, total_piles, online_rate) "
        "VALUES (?, ?, ?, ?, ?, 0)"));
    insertStation.addBindValue(name.trimmed());
    insertStation.addBindValue(address.trimmed());
    insertStation.addBindValue(longitude);
    insertStation.addBindValue(latitude);
    insertStation.addBindValue(pileCount);
    if (!insertStation.exec()) {
        db.rollback();
        if (error) *error = QStringLiteral("新增电站失败：%1").arg(queryError(insertStation));
        return -1;
    }
    const int stationId = insertStation.lastInsertId().toInt();

    // 批量生成模拟电桩：编号 S%03d-P%02d；快慢充、初始状态采用固定分布，便于测试断言
    for (int i = 0; i < pileCount; ++i) {
        const bool isSlow = (i % 4 == 3);
        const QString type = isSlow ? QStringLiteral("慢充") : QStringLiteral("快充");
        const double powerKw = isSlow ? 7.0 : 120.0;
        cp::PileState state;
        if (i % 10 == 9)
            state = cp::PileState::Fault;      // 约 10% 初始故障
        else if (i % 3 == 1)
            state = cp::PileState::Charging;   // 约 1/3 充电中
        else
            state = cp::PileState::Idle;       // 其余闲置

        const QString code = QStringLiteral("S%1-P%2")
                                 .arg(stationId, 3, 10, QLatin1Char('0'))
                                 .arg(i + 1, 2, 10, QLatin1Char('0'));
        QSqlQuery insertPile(db);
        insertPile.prepare(QStringLiteral(
            "INSERT INTO piles(station_id, code, type, power_kw, state) VALUES (?, ?, ?, ?, ?)"));
        insertPile.addBindValue(stationId);
        insertPile.addBindValue(code);
        insertPile.addBindValue(type);
        insertPile.addBindValue(powerKw);
        insertPile.addBindValue(static_cast<int>(state));
        if (!insertPile.exec()) {
            db.rollback();
            if (error) *error = QStringLiteral("生成模拟电桩失败：%1").arg(queryError(insertPile));
            return -1;
        }
    }

    if (!db.commit()) {
        db.rollback();
        if (error) *error = QStringLiteral("提交新增电站事务失败：%1").arg(db.lastError().text());
        return -1;
    }

    QString rateError;
    if (!refreshOnlineRate(stationId, &rateError)) {
        if (error) *error = rateError;
        return -1;
    }
    return stationId;
}

bool StationStore::setPileState(int pileId, cp::PileState state, QString *error)
{
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }

    QSqlQuery stationQuery(m_db);
    stationQuery.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
    stationQuery.addBindValue(pileId);
    if (!stationQuery.exec() || !stationQuery.next()) {
        if (error) *error = QStringLiteral("未找到电桩 id=%1").arg(pileId);
        return false;
    }
    const int stationId = stationQuery.value(0).toInt();

    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE piles SET state = ? WHERE id = ?"));
    update.addBindValue(static_cast<int>(state));
    update.addBindValue(pileId);
    if (!update.exec()) {
        if (error) *error = QStringLiteral("更新电桩状态失败：%1").arg(queryError(update));
        return false;
    }
    return refreshOnlineRate(stationId, error);
}

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

        // 5) 释放电桩并累计次数/时长
        QSqlQuery pile(db);
        pile.prepare(QStringLiteral(
            "UPDATE piles SET state = 0, charge_count = charge_count + 1, "
            " charge_seconds = charge_seconds + CAST("
            "   (julianday('now','localtime') - "
            "    (SELECT julianday(start_time) FROM orders WHERE id = ?)) * 3600 "
            "   AS INTEGER) "
            "WHERE id = ?"));
        pile.addBindValue(orderId);
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
