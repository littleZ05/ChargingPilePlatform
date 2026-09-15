#include "charge_service.h"
#include "stationstore.h"

#include <QBuffer>
#include <QDebug>
#include <QCryptographicHash>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

namespace pcserver {
namespace {
struct BusinessError { int code; QString message; };

void require(bool condition, int code, const QString &message)
{
    if (!condition)
        throw BusinessError{code, message};
}

QSqlQuery query(QSqlDatabase db, const QString &sql, const QVariantList &args = {})
{
    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        qWarning() << "[business-db prepare]" << q.lastError().databaseText();
        throw BusinessError{503, QStringLiteral("数据库查询准备失败")};
    }
    for (const QVariant &arg : args)
        q.addBindValue(arg);
    if (!q.exec()) {
        qWarning() << "[business-db]" << q.lastError().nativeErrorCode()
                   << q.lastError().databaseText();
        throw BusinessError{503, QStringLiteral("数据库操作失败，请稍后重试")};
    }
    return q;
}

QJsonObject reply(int code, const QString &message)
{
    return {{"code", code}, {"message", message},
            {"server_time", QDateTime::currentDateTime().toString(Qt::ISODate)}};
}

double money(double value) { return std::round(value * 100.0) / 100.0; }

QJsonObject orderObject(QSqlDatabase db, int userId, int orderId)
{
    auto q = query(db, QStringLiteral(
        "SELECT o.id,o.pile_id,o.station_id,p.code,p.power_kw,o.start_time,"
        "o.kwh,o.price,o.amount,o.state,s.name FROM orders o "
        "JOIN piles p ON p.id=o.pile_id JOIN stations s ON s.id=o.station_id "
        "WHERE o.id=? AND o.user_id=?"), {orderId, userId});
    require(q.next(), 404, QStringLiteral("订单不存在或不属于当前用户"));
    return {{"order_id", q.value(0).toInt()}, {"pile_id", q.value(1).toInt()},
            {"station_id", q.value(2).toInt()}, {"pile_code", q.value(3).toString()},
            {"power_kw", q.value(4).toDouble()}, {"start_time", q.value(5).toString()},
            {"kwh", q.value(6).toDouble()}, {"unit_price", q.value(7).toDouble()},
            {"amount", q.value(8).toDouble()}, {"state", q.value(9).toInt()},
            {"station_name", q.value(10).toString()}};
}

QJsonObject profile(QSqlDatabase db, int userId)
{
    auto q = query(db, QStringLiteral(
        "SELECT phone,nickname,balance,COALESCE(avatar_path,''),status FROM users WHERE id=?"),
        {userId});
    require(q.next(), 401, QStringLiteral("请重新登录"));
    QJsonObject out = reply(0, QStringLiteral("ok"));
    out.insert("phone", q.value(0).toString());
    out.insert("nickname", q.value(1).toString());
    out.insert("balance", q.value(2).toDouble());
    out.insert("avatar", q.value(3).toString());
    out.insert("status", q.value(4).toInt());
    QJsonArray orders;
    auto ids = query(db, QStringLiteral(
        "SELECT id FROM orders WHERE user_id=? ORDER BY id DESC LIMIT 100"), {userId});
    while (ids.next())
        orders.append(orderObject(db, userId, ids.value(0).toInt()));
    out.insert("orders", orders);
    auto active = query(db, QStringLiteral(
        "SELECT id FROM orders WHERE user_id=? AND state=0 ORDER BY id LIMIT 1"), {userId});
    out.insert("active_order", active.next()
               ? QJsonValue(orderObject(db, userId, active.value(0).toInt())) : QJsonValue());
    return out;
}

QJsonObject start(QSqlDatabase db, int userId, const QJsonObject &r)
{
    auto u = query(db, QStringLiteral("SELECT status FROM users WHERE id=?"), {userId});
    require(u.next() && u.value(0).toInt() == 0, 403, QStringLiteral("账号已冻结，不能新建订单"));
    auto active = query(db, QStringLiteral("SELECT id FROM orders WHERE user_id=? AND state=0"),
                        {userId});
    require(!active.next(), 409, QStringLiteral("您有未完成的充电订单，请先结算"));
    auto p = query(db, QStringLiteral(
        "SELECT p.id,p.station_id,p.state,p.health_level,s.base_price,"
        "COALESCE((SELECT discount FROM marketing_strategy m WHERE m.station_id=s.id "
        "AND m.is_active=1 AND (m.valid_from IS NULL OR m.valid_from<=datetime('now','localtime')) "
        "AND (m.valid_to IS NULL OR m.valid_to>=datetime('now','localtime')) "
        "ORDER BY id DESC LIMIT 1),1.0) FROM piles p JOIN stations s ON s.id=p.station_id "
        "WHERE p.code=?"), {r.value("pile_code").toString()});
    require(p.next(), 404, QStringLiteral("电桩不存在"));
    const int pileId = p.value(0).toInt();
    const int stationId = p.value(1).toInt();
    require(p.value(2).toInt() == 0 && p.value(3).toInt() == 0, 409,
            QStringLiteral("电桩正在使用或需要检查，请选择正常空闲桩"));
    auto busy = query(db, QStringLiteral("SELECT id FROM orders WHERE pile_id=? AND state=0"),
                      {pileId});
    require(!busy.next(), 409, QStringLiteral("电桩有未完成订单"));
    const double price = p.value(4).toDouble() * p.value(5).toDouble();
    require(std::isfinite(price) && price >= 0 && p.value(5).toDouble() > 0,
            503, QStringLiteral("计价策略无效"));
    auto ins = query(db, QStringLiteral(
        "INSERT INTO orders(user_id,pile_id,station_id,price,state) VALUES(?,?,?,?,0)"),
        {userId, pileId, stationId, price});
    query(db, QStringLiteral("UPDATE piles SET state=1 WHERE id=?"), {pileId});
    QJsonObject out = orderObject(db, userId, ins.lastInsertId().toInt());
    out.insert("code", 0);
    out.insert("message", QStringLiteral("充电已开始，单价已锁定"));
    return out;
}

QJsonObject settle(QSqlDatabase db, int userId, const QJsonObject &r)
{
    const int id = r.value("order_id").toInt();
    QJsonObject out = orderObject(db, userId, id);
    require(r.value("pile_code").toString().isEmpty()
            || r.value("pile_code").toString() == out.value("pile_code").toString(),
            409, QStringLiteral("订单与电桩不一致"));
    require(out.value("state").toInt() != 2, 409, QStringLiteral("订单已取消"));
    const int pileId = out.value("pile_id").toInt();
    if (out.value("state").toInt() == 0) {
        const double kwh = r.value("kwh").toDouble(-1);
        const auto started = QDateTime::fromString(out.value("start_time").toString(),
                                                 QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const qint64 seconds = qMax<qint64>(1, started.secsTo(QDateTime::currentDateTime()));
        const double maximum = out.value("power_kw").toDouble() * seconds / 3600.0;
        require(started.isValid() && std::isfinite(kwh) && kwh >= 0
                && kwh <= maximum * 1.2 + 0.05, 422, QStringLiteral("电量超出充电时长和额定功率范围"));
        const double amount = money(kwh * out.value("unit_price").toDouble());
        require(std::isfinite(amount) && amount >= 0, 422, QStringLiteral("结算金额无效"));
        auto balance = query(db, QStringLiteral("SELECT balance FROM users WHERE id=?"), {userId});
        require(balance.next() && balance.value(0).toDouble() + 0.000001 >= amount,
                409, QStringLiteral("余额不足，请充值后重试结算"));
        query(db, QStringLiteral("UPDATE users SET balance=ROUND(balance-?,2) WHERE id=?"),
              {amount, userId});
        query(db, QStringLiteral(
            "UPDATE orders SET kwh=?,amount=?,state=1,end_time=datetime('now','localtime') WHERE id=?"),
            {kwh, amount, id});
        query(db, QStringLiteral(
            "UPDATE piles SET state=CASE WHEN health_level=2 THEN 2 ELSE 0 END,"
            "charge_count=charge_count+1,charge_seconds=charge_seconds+? WHERE id=?"),
            {seconds, pileId});
        query(db, QStringLiteral(
            "INSERT INTO pile_power_logs(pile_id,order_id,real_power) VALUES(?,?,?)"),
            {pileId, id, kwh * 3600.0 / seconds});
        out = orderObject(db, userId, id);
    }
    auto balance = query(db, QStringLiteral("SELECT balance FROM users WHERE id=?"), {userId});
    require(balance.next(), 503, QStringLiteral("余额查询失败"));
    out.insert("balance", balance.value(0).toDouble());
    out.insert("code", 0);
    out.insert("message", QStringLiteral("结算成功"));
    out.insert("received", true);
    out.insert("pile_freed", true);
    out.insert("order_no", QString::number(id));
    return out;
}

QJsonObject recharge(QSqlDatabase db, int userId, const QJsonObject &r)
{
    const double amount = r.value("amount").toDouble(-1);
    require(std::isfinite(amount) && amount > 0 && amount <= 100000
            && std::abs(amount - money(amount)) < 0.000001, 422,
            QStringLiteral("充值金额需为 0.01 至 100000 元，最多两位小数"));
    query(db, QStringLiteral("UPDATE users SET balance=ROUND(balance+?,2) WHERE id=?"),
          {amount, userId});
    auto q = query(db, QStringLiteral("SELECT balance FROM users WHERE id=?"), {userId});
    require(q.next(), 503, QStringLiteral("余额查询失败"));
    QJsonObject out = reply(0, QStringLiteral("充值成功"));
    out.insert("amount", amount);
    out.insert("balance", q.value(0).toDouble());
    return out;
}

QJsonObject updateProfile(QSqlDatabase db, int userId, const QJsonObject &r)
{
    auto user = query(db, QStringLiteral("SELECT status FROM users WHERE id=?"), {userId});
    require(user.next() && user.value(0).toInt() == 0, 403, QStringLiteral("冻结账号不能修改资料"));
    if (r.contains("nickname")) {
        const QString nickname = r.value("nickname").toString().trimmed();
        require(!nickname.isEmpty() && nickname.size() <= 32, 422, QStringLiteral("昵称需为 1 至 32 字"));
        query(db, QStringLiteral("UPDATE users SET nickname=?,gmt_modified=datetime('now','localtime') WHERE id=?"),
              {nickname, userId});
    }
    if (r.contains("avatar")) {
        const QByteArray encoded = r.value("avatar").toString().toLatin1();
        require(encoded.size() <= 350000, 422, QStringLiteral("头像过大"));
        QByteArray bytes = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        require(size.isValid() && size.width() <= 2048 && size.height() <= 2048,
                422, QStringLiteral("头像必须是尺寸不超过 2048 的有效图片"));
        QImage img = reader.read();
        require(!img.isNull(), 422, QStringLiteral("无法读取头像"));
        QByteArray png;
        QBuffer destination(&png);
        destination.open(QIODevice::WriteOnly);
        require(img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                    .save(&destination, "PNG"), 422, QStringLiteral("头像转换失败"));
        query(db, QStringLiteral("UPDATE users SET avatar_path=? WHERE id=?"),
              {QString::fromLatin1(png.toBase64()), userId});
    }
    return profile(db, userId);
}
} // namespace

QJsonObject ChargeService::execute(int type, const QString &phone, const QJsonObject &request)
{
    QJsonObject out;
    const QString requestId = request.value("request_id").toString();
    if (!m_store || !m_store->isOpen())
        return reply(503, QStringLiteral("数据库服务未就绪"));
    QSqlDatabase db = QSqlDatabase::database(m_store->connectionName());
    bool transaction = false;
    try {
        require(!phone.isEmpty(), 401, QStringLiteral("请先登录"));
        require(!request.contains("phone") || request.value("phone").toString() == phone,
                403, QStringLiteral("不能操作其他用户"));
        auto u = query(db, QStringLiteral("SELECT id FROM users WHERE phone=?"), {phone});
        require(u.next(), 401, QStringLiteral("请重新登录"));
        const int userId = u.value(0).toInt();
        const bool writing = type != cp::MsgType::kProfileQuery;
        require(!writing || (!requestId.isEmpty() && requestId.size() <= 80),
                400, QStringLiteral("写请求必须包含 request_id（最多80字符）"));
        // Serialize state transitions and receipt insertion across SQLite connections.
        query(db, QStringLiteral("BEGIN IMMEDIATE"));
        transaction = true;
        QJsonObject fingerprint = request;
        fingerprint.remove("request_id");
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(fingerprint).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
        bool replay = false;
        if (writing) {
            auto old = query(db, QStringLiteral(
                "SELECT msg_type,fingerprint,response FROM request_receipts WHERE user_id=? AND request_id=?"),
                {userId, requestId});
            if (old.next()) {
                require(old.value(0).toInt() == type && old.value(1).toString() == hash,
                        409, QStringLiteral("request_id 已用于不同请求"));
                out = QJsonDocument::fromJson(old.value(2).toByteArray()).object();
                replay = true;
            }
        }
        if (!replay) {
            switch (type) {
            case cp::MsgType::kProfileQuery: out = profile(db, userId); break;
            case cp::MsgType::kProfileUpdate: out = updateProfile(db, userId, request); break;
            case cp::MsgType::kStartCharge: out = start(db, userId, request); break;
            case cp::MsgType::kStopCharge:
            case cp::MsgType::kOrderReport: out = settle(db, userId, request); break;
            case cp::MsgType::kRechargeRequest: out = recharge(db, userId, request); break;
            default: throw BusinessError{400, QStringLiteral("不支持的业务消息")};
            }
            if (writing) {
                query(db, QStringLiteral(
                    "INSERT INTO request_receipts(user_id,request_id,msg_type,fingerprint,response) VALUES(?,?,?,?,?)"),
                    {userId, requestId, type, hash, QJsonDocument(out).toJson(QJsonDocument::Compact)});
                query(db, QStringLiteral(
                    "UPDATE stations SET online_rate=COALESCE((SELECT "
                    "100.0*SUM(CASE WHEN p.state<>2 AND p.health_level<>2 THEN 1 ELSE 0 END)/COUNT(*) "
                    "FROM piles p WHERE p.station_id=stations.id),0)"));
            }
        }
        require(db.commit(), 503, QStringLiteral("事务提交失败，请重试"));
        transaction = false;
    } catch (const BusinessError &e) {
        if (transaction)
            db.rollback();
        out = reply(e.code, e.message);
    }
    out.insert("request_id", requestId);
    return out;
}
} // namespace pcserver
