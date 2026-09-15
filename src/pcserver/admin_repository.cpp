#include "admin_repository.h"
#include "stationstore.h"
#include "common.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QDateTime>
#include <algorithm>
namespace pcserver_admin {
QString &adminDbPathOverride() { static QString path; return path; }
constexpr char kConnectionName[] = "pcserver_sqlite_connection";
QString escapeLikePattern(const QString &input)
{
    QString value=input;
    value.replace("\\","\\\\").replace("%","\\%").replace("_","\\_");
    return value;
}
bool DatabaseManager::initialize(QString *error)
{
    if (m_initialized && m_db.isOpen()) {
        return true;
    }
    if (!openDatabase(error)) {
        return false;
    }
    if (!ensureSchema(error)) {
        return false;
    }
    if (!seedDemoData(error)) {
        return false;
    }
    m_initialized = true;
    return true;
}

bool DatabaseManager::verifyAdmin(const QString &username, const QString &password, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM admins WHERE username = ? AND password = ?"));
    query.addBindValue(username.trimmed());
    query.addBindValue(password);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return query.next();
}

SalesSummary DatabaseManager::salesSummary(QString *error) const
{
        SalesSummary summary;
        const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString month = QDate::currentDate().toString(QStringLiteral("yyyy-MM"));
        summary.today = scalarDouble(QStringLiteral(
            "SELECT COALESCE(SUM(amount), 0) FROM orders "
            "WHERE state = ? AND substr(end_time, 1, 10) = ?"),
            { static_cast<int>(cp::OrderState::Finished), today }, error);
        summary.month = scalarDouble(QStringLiteral(
            "SELECT COALESCE(SUM(amount), 0) FROM orders "
            "WHERE state = ? AND substr(end_time, 1, 7) = ?"),
            { static_cast<int>(cp::OrderState::Finished), month }, error);
        summary.total = scalarDouble(QStringLiteral(
            "SELECT COALESCE(SUM(amount), 0) FROM orders WHERE state = ?"),
            { static_cast<int>(cp::OrderState::Finished) }, error);
        return summary;
    }

QVector<RevenuePoint> DatabaseManager::revenueSeries(int days, QString *error) const
{
    days = std::max(1, days);
    QVector<RevenuePoint> points;
    const QDate startDate = QDate::currentDate().addDays(-(days - 1));
    const QString start = startDate.toString(QStringLiteral("yyyy-MM-dd"));
    const QString end = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    QHash<QString, double> revenueByDay;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT substr(end_time, 1, 10) AS day, COALESCE(SUM(amount), 0) "
        "FROM orders WHERE state = ? AND substr(end_time, 1, 10) BETWEEN ? AND ? "
        "GROUP BY day"));
    query.addBindValue(static_cast<int>(cp::OrderState::Finished));
    query.addBindValue(start);
    query.addBindValue(end);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
    } else {
        while (query.next()) {
            revenueByDay.insert(query.value(0).toString(), query.value(1).toDouble());
        }
    }

    for (int i = 0; i < days; ++i) {
        const QDate date = startDate.addDays(i);
        points.push_back({ date.toString(QStringLiteral("MM-dd")), revenueByDay.value(date.toString(QStringLiteral("yyyy-MM-dd")), 0.0) });
    }
    return points;
}

QVector<OrderRow> DatabaseManager::recentOrders(int limit, QString *error) const
{
    QVector<OrderRow> rows;
    limit = std::max(1, limit);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT o.id, COALESCE(u.nickname, ''), COALESCE(s.name, ''), COALESCE(p.code, ''), "
        "COALESCE(o.start_time, ''), COALESCE(o.end_time, ''), o.kwh, o.price, o.amount, o.state "
        "FROM orders o "
        "LEFT JOIN users u ON u.id = o.user_id "
        "LEFT JOIN piles p ON p.id = o.pile_id "
        "LEFT JOIN stations s ON s.id = o.station_id "
        "ORDER BY COALESCE(o.end_time, o.start_time) DESC, o.id DESC "
        "LIMIT %1").arg(limit));
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return rows;
    }
    while (query.next()) {
        OrderRow row;
        row.id = query.value(0).toInt();
        row.userName = query.value(1).toString();
        row.stationName = query.value(2).toString();
        row.pileCode = query.value(3).toString();
        row.startTime = query.value(4).toString();
        row.endTime = query.value(5).toString();
        row.kwh = query.value(6).toDouble();
        row.price = query.value(7).toDouble();
        row.amount = query.value(8).toDouble();
        row.state = query.value(9).toInt();
        rows.push_back(row);
    }
    return rows;
}

QVector<PileRow> DatabaseManager::pileRows(int stateFilter, QString *error) const
{
    QVector<PileRow> rows;
    QString sql = QStringLiteral(
        "SELECT p.id, p.station_id, COALESCE(s.name, ''), p.code, p.type, p.power_kw, p.state, p.charge_count, p.charge_seconds "
        "FROM piles p LEFT JOIN stations s ON s.id = p.station_id ");
    if (stateFilter >= 0) {
        sql += QStringLiteral("WHERE p.state = %1 ").arg(stateFilter);
    }
    sql += QStringLiteral("ORDER BY p.id ASC");

    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        if (error) {
            *error = query.lastError().text();
        }
        return rows;
    }
    while (query.next()) {
        PileRow row;
        row.id = query.value(0).toInt();
        row.stationId = query.value(1).toInt();
        row.stationName = query.value(2).toString();
        row.code = query.value(3).toString();
        row.type = query.value(4).toString();
        row.powerKw = query.value(5).toDouble();
        row.state = query.value(6).toInt();
        row.chargeCount = query.value(7).toInt();
        row.chargeSeconds = query.value(8).toInt();
        rows.push_back(row);
    }
    return rows;
}

QVector<StationRow> DatabaseManager::stations(QString *error) const
{
    QVector<StationRow> rows;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
        "SELECT id, name, address, longitude, latitude, total_piles, online_rate, base_price "
        "FROM stations ORDER BY id ASC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return rows;
    }
    while (query.next()) {
        StationRow row;
        row.id = query.value(0).toInt();
        row.name = query.value(1).toString();
        row.address = query.value(2).toString();
        row.longitude = query.value(3).toDouble();
        row.latitude = query.value(4).toDouble();
        row.totalPiles = query.value(5).toInt();
        row.onlineRate = query.value(6).toDouble();
        row.basePrice = query.value(7).toDouble();
        rows.push_back(row);
    }
    return rows;
}

QVector<UserRow> DatabaseManager::users(const QString &phoneKeyword,
                       QString *error) const
{
    QVector<UserRow> rows;
    QString sql = QStringLiteral(
        "SELECT id, phone, COALESCE(nickname, ''), balance, "
        "COALESCE(gmt_create, ''), status FROM users ");
    QVariantList binds;
    const QString keyword = phoneKeyword.trimmed();
    if (!keyword.isEmpty()) {
        sql += QStringLiteral("WHERE phone LIKE ? ESCAPE '\\' ");
        binds << QStringLiteral("%%1%").arg(escapeLikePattern(keyword));
    }
    sql += QStringLiteral("ORDER BY id ASC");

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return rows;
    }
    while (query.next()) {
        UserRow row;
        row.id = query.value(0).toInt();
        row.phone = query.value(1).toString();
        row.nickname = query.value(2).toString();
        row.balance = query.value(3).toDouble();
        row.gmtCreate = query.value(4).toString();
        row.status = query.value(5).toInt();
        rows.push_back(row);
    }
    return rows;
}

bool DatabaseManager::setUserStatus(int userId, int status, QString *error)
{
    if (userId <= 0) {
        if (error) *error = QStringLiteral("用户ID非法");
        return false;
    }
    if (status != 0 && status != 1) {
        if (error) *error = QStringLiteral("用户状态非法（仅 0 正常 / 1 冻结）");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE users SET status = ?, gmt_modified = datetime('now','localtime') "
        "WHERE id = ?"));
    query.addBindValue(status);
    query.addBindValue(userId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool DatabaseManager::addPile(int stationId, const QString &code, const QString &type, double powerKw, int state, int *newId, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state, charge_count, charge_seconds) "
        "VALUES(?, ?, ?, ?, ?, 0, 0)"));
    query.addBindValue(stationId);
    query.addBindValue(code.trimmed());
    query.addBindValue(type);
    query.addBindValue(powerKw);
    query.addBindValue(state);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    const int id = query.lastInsertId().toInt();
    if (newId) {
        *newId = id;
    }
    return recalculateStationStats(stationId, error);
}

bool DatabaseManager::updatePile(int pileId, int stationId, const QString &code, const QString &type, double powerKw, int state, QString *error)
{
    int oldStationId = -1;
    QSqlQuery lookup(m_db);
    lookup.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
    lookup.addBindValue(pileId);
    if (!lookup.exec() || !lookup.next()) {
        if (error) {
            *error = QStringLiteral("未找到要更新的电桩");
        }
        return false;
    }
    oldStationId = lookup.value(0).toInt();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE piles SET station_id = ?, code = ?, type = ?, power_kw = ?, state = ? WHERE id = ?"));
    query.addBindValue(stationId);
    query.addBindValue(code.trimmed());
    query.addBindValue(type);
    query.addBindValue(powerKw);
    query.addBindValue(state);
    query.addBindValue(pileId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    if (oldStationId == stationId) {
        return recalculateStationStats(stationId, error);
    }
    return recalculateStationStats(oldStationId, error) && recalculateStationStats(stationId, error);
}

bool DatabaseManager::deletePile(int pileId, QString *error)
{
    int stationId = -1;
    QSqlQuery lookup(m_db);
    lookup.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
    lookup.addBindValue(pileId);
    if (!lookup.exec() || !lookup.next()) {
        if (error) {
            *error = QStringLiteral("未找到要删除的电桩");
        }
        return false;
    }
    stationId = lookup.value(0).toInt();

    const QStringList statements = {
        QStringLiteral("DELETE FROM pile_power_logs WHERE pile_id = %1").arg(pileId),
        QStringLiteral("DELETE FROM pile_health_metrics WHERE pile_id = %1").arg(pileId),
        QStringLiteral("DELETE FROM orders WHERE pile_id = %1").arg(pileId),
        QStringLiteral("DELETE FROM piles WHERE id = %1").arg(pileId)
    };
    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }

    return recalculateStationStats(stationId, error);
}

bool DatabaseManager::setPileState(int pileId, int state, QString *error)
{
    int stationId = -1;
    QSqlQuery lookup(m_db);
    lookup.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
    lookup.addBindValue(pileId);
    if (!lookup.exec() || !lookup.next()) {
        if (error) {
            *error = QStringLiteral("未找到要修改的电桩");
        }
        return false;
    }
    stationId = lookup.value(0).toInt();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE piles SET state = ? WHERE id = ?"));
    query.addBindValue(state);
    query.addBindValue(pileId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    return recalculateStationStats(stationId, error);
}

bool DatabaseManager::remoteRestartPile(int pileId, QString *message, QString *error)
{
    QSqlQuery lookup(m_db);
    lookup.prepare(QStringLiteral("SELECT code FROM piles WHERE id = ?"));
    lookup.addBindValue(pileId);
    if (!lookup.exec() || !lookup.next()) {
        if (error) {
            *error = QStringLiteral("未找到要重启的电桩");
        }
        return false;
    }
    const QString code = lookup.value(0).toString();
    if (!setPileState(pileId, 0, error)) {
        return false;
    }
    if (message) {
        *message = QStringLiteral("已向 %1 发送远程重启指令，电桩已切回闲置").arg(code);
    }
    return true;
}

QString DatabaseManager::dbPath() const
{
    // P0 统一数据源：优先使用 main.cpp 注入的路径（与 StationStore 同一个库文件）
    if (!adminDbPathOverride().isEmpty()) {
        return adminDbPathOverride();
    }
    // 测试/演示可用环境变量指定独立数据库，避免污染用户数据（默认不变）
    const QByteArray envPath = qgetenv("PCSERVER_DB_PATH");
    if (!envPath.isEmpty()) {
        return QString::fromLocal8Bit(envPath);
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/ChargingPilePlatform");
    }
    QDir dir(base);
    if (!dir.exists()) {
        QDir().mkpath(base);
    }
    return dir.filePath(QStringLiteral("chargingpile-platform.db"));
}

bool DatabaseManager::openDatabase(QString *error)
{
    const QString path = dbPath();
    const QFileInfo dbInfo(path);
    QDir().mkpath(dbInfo.absolutePath());
    QFile dbFile(dbInfo.absoluteFilePath());
    if (!dbFile.exists()) {
        if (!dbFile.open(QIODevice::WriteOnly)) {
            if (error) {
                *error = QStringLiteral("无法创建数据库文件 %1：%2")
                             .arg(dbInfo.absoluteFilePath(), dbFile.errorString());
            }
            return false;
        }
        dbFile.close();
    }
    if (QSqlDatabase::contains(QString::fromLatin1(kConnectionName))) {
        m_db = QSqlDatabase::database(QString::fromLatin1(kConnectionName));
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QString::fromLatin1(kConnectionName));
    }
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    return true;
}

bool DatabaseManager::runStatements(const QStringList &statements, QString *error) const
{
    for (const QString &sql : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }
    return true;
}

bool DatabaseManager::ensureSchema(QString *error)
{
pcserver::StationStore schema;
return schema.open(m_db.databaseName(),error);
}

int DatabaseManager::scalarInt(const QString &sql, const QVariantList &binds, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

double DatabaseManager::scalarDouble(const QString &sql, const QVariantList &binds, QString *error) const
{
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return 0.0;
    }
    if (!query.next()) {
        return 0.0;
    }
    return query.value(0).toDouble();
}

int DatabaseManager::insertRowId(const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &bind : binds) {
        query.addBindValue(bind);
    }
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return -1;
    }
    return query.lastInsertId().toInt();
}

QVector<PileInfo> DatabaseManager::currentPileInfos(QString *error) const
{
    QVector<PileInfo> infos;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
        "SELECT p.id, p.station_id, p.code, p.type, p.power_kw "
        "FROM piles p ORDER BY p.id ASC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return infos;
    }
    while (query.next()) {
        PileInfo info;
        info.id = query.value(0).toInt();
        info.stationId = query.value(1).toInt();
        info.code = query.value(2).toString();
        info.type = query.value(3).toString();
        info.powerKw = query.value(4).toDouble();
        infos.push_back(info);
    }
    return infos;
}

bool DatabaseManager::recalculateStationStats(int stationId, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(CASE WHEN state <> 2 THEN 1 ELSE 0 END), 0) "
        "FROM piles WHERE station_id = ?"));
    query.addBindValue(stationId);
    if (!query.exec() || !query.next()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    const int total = query.value(0).toInt();
    const int online = query.value(1).toInt();
    const double onlineRate = total > 0 ? (online * 100.0 / total) : 0.0;

    QSqlQuery update(m_db);
    update.prepare(QStringLiteral("UPDATE stations SET total_piles = ?, online_rate = ? WHERE id = ?"));
    update.addBindValue(total);
    update.addBindValue(onlineRate);
    update.addBindValue(stationId);
    if (!update.exec()) {
        if (error) {
            *error = update.lastError().text();
        }
        return false;
    }
    return true;
}

bool DatabaseManager::recalculateAllStationStats(QString *error)
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id FROM stations ORDER BY id ASC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    while (query.next()) {
        if (!recalculateStationStats(query.value(0).toInt(), error)) {
            return false;
        }
    }
    return true;
}    QHash<int, double> DatabaseManager::stationBasePriceMap(QString *error) const
{
    QHash<int, double> map;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, base_price FROM stations ORDER BY id ASC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return map;
    }
    while (query.next()) {
        map.insert(query.value(0).toInt(), query.value(1).toDouble());
    }
    return map;
}


}
