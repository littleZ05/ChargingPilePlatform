#include "stationstore.h"
#include "loadforecast.h"

#include <QDir>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTime>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace {

/** NO.6：手机号自动注册时赠送的演示初始余额（元），保证新用户可完成一次充电结算 */


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

/** 当前整点（小时桶起点），本地时间 */
QDateTime currentHourStart()
{
    const QDateTime now = QDateTime::currentDateTime();
    return QDateTime(now.date(), QTime(now.time().hour(), 0));
}

/** 确定性日负荷曲线系数：夜间低谷、早高峰 9-11 点、晚高峰 17-19 点 */
double demoHourFactor(int hourOfDay)
{
    static const double kFactors[24] = {
        0.08, 0.06, 0.05, 0.05, 0.06, 0.10, 0.16, 0.24,
        0.34, 0.44, 0.50, 0.54, 0.50, 0.46, 0.44, 0.48,
        0.56, 0.66, 0.72, 0.68, 0.58, 0.46, 0.34, 0.22
    };
    return kFactors[((hourOfDay % 24) + 24) % 24];
}

double roundLoad(double value)
{
    return qRound(value * 10.0) / 10.0;
}

/** Missing hours are estimates, not measured zero: interpolate interior gaps,
 * carry the nearest measured value at edges, preserve measured zeros. */
QVector<double> interpolateHours(const QHash<qint64,double> &observations,
                                const QDateTime &start, int hours, int *imputed)
{
    QVector<double> values(hours,0.0);
    QVector<int> measured;
    for(int i=0;i<hours;++i) {
        const auto key=start.addSecs(i*3600).toMSecsSinceEpoch();
        if(observations.contains(key)) {
            measured.append(i);
            values[i]=roundLoad(observations.value(key));
        }
    }
    if(imputed) *imputed=hours-measured.size();
    if(measured.isEmpty()) return values;
    for(int i=0;i<measured.first();++i) values[i]=values[measured.first()];
    for(int n=1;n<measured.size();++n) {
        const int left=measured[n-1], right=measured[n];
        for(int i=left+1;i<right;++i)
            values[i]=roundLoad(values[left]+(values[right]-values[left])*(i-left)/(right-left));
    }
    for(int i=measured.last()+1;i<hours;++i) values[i]=values[measured.last()];
    return values;
}

/** 全平台电桩功率合计（stateFilter<0 表示全部，否则按状态过滤）；失败返回 -1 */
double sumPilesPower(const QSqlDatabase &db, int stateFilter, QString *errorText)
{
    QSqlQuery q(db);
    if (stateFilter >= 0) {
        q.prepare(QStringLiteral(
            "SELECT COALESCE(SUM(power_kw), 0) FROM piles WHERE state = ?"));
        q.addBindValue(stateFilter);
    } else {
        q.prepare(QStringLiteral(
            "SELECT COALESCE(SUM(power_kw), 0) FROM piles"));
    }
    if (!q.exec()) {
        if (errorText) *errorText = queryError(q);
        return -1.0;
    }
    q.next();
    return q.value(0).toDouble();
}

/** 单日订单聚合（大屏 7 日营收/订单用） */
struct DayOrderAgg
{
    int    orders = 0;
    double revenue = 0.0;
    double energyKwh = 0.0;
};

/**
 * 仿真采样曲线（确定性、无随机数）：
 * capacity × 时段系数 + 按电站/时段固定的微波动，最近小时优先对齐实时负荷。
 */
QVector<double> demoHourlyLoadSeries(int stationId, int hours,
                                     const QDateTime &anchorHour,
                                     double capacityKw, double currentKw)
{
    constexpr double kPi = 3.14159265358979323846;
    QVector<double> out;
    out.reserve(hours);
    for (int k = 0; k < hours; ++k) {
        const QDateTime bucketStart = anchorHour.addSecs((k - hours + 1) * 3600);
        const int hod = bucketStart.time().hour();
        const double ripple = 0.03 * capacityKw
                              * std::sin((hod * 2 + stationId * 3) * kPi / 12.0);
        double kw = capacityKw * demoHourFactor(hod) + ripple;
        if (k == hours - 1 && currentKw > 0.0)
            kw = currentKw;
        out.push_back(roundLoad(std::max(0.0, kw)));
    }
    return out;
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
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 3000"));
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

    // Schema changes are atomic: a conflicting old order must leave the old DB intact.
    if (!m_db.transaction()) {
        if (error) *error = m_db.lastError().text();
        return false;
    }
    const auto fail = [&](const QString &message) {
        m_db.rollback();
        if (error) *error = message;
        return false;
    };
    const QStringList statements = splitSqlStatements(sql);
    for (const QString &statement : statements) {
        QSqlQuery q(m_db);
        if (!q.exec(statement))
            return fail(QStringLiteral("数据库升级失败，已回滚；请检查重复活动订单或表结构：%1")
                        .arg(queryError(q)));
    }
    struct Column { const char *table; const char *name; const char *definition; };
    const Column columns[] = {
        {"marketing_strategy", "predicted_idle_rate", "REAL NOT NULL DEFAULT 0"},
        {"marketing_strategy", "decided_at", "TEXT"},
        {"piles", "health_level", "INTEGER NOT NULL DEFAULT 0"}
    };
    for (const auto &column : columns) {
        QSqlQuery fields(m_db);
        if (!fields.exec(QStringLiteral("PRAGMA table_info(%1)").arg(column.table)))
            return fail(queryError(fields));
        bool present = false;
        while (fields.next())
            present = present || fields.value(1).toString() == QLatin1String(column.name);
        fields.finish();
        if (!present) {
            QSqlQuery alter(m_db);
            if (!alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                            .arg(column.table, column.name, column.definition)))
                return fail(queryError(alter));
        }
    }
    QSqlQuery version(m_db);
    if (!version.exec(QStringLiteral("PRAGMA user_version=3")))
        return fail(queryError(version));
    if (!m_db.commit())
        return fail(m_db.lastError().text());
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
        { QStringLiteral("中关村软件园充电站"), QStringLiteral("北京市海淀区东北旺西路8号"),
          116.297000, 40.047000, 10 },
        { QStringLiteral("望京SOHO充电站"), QStringLiteral("北京市朝阳区望京街10号"),
          116.481000, 39.996000, 12 },
        { QStringLiteral("国贸CBD充电站"), QStringLiteral("北京市朝阳区建国门外大街1号"),
          116.461000, 39.908000, 6 },
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

double StationStore::ratedCapacityKw(int stationId, QString *error) const
{
    if (stationId <= 0) {
        if (error) *error = QStringLiteral("电站ID非法");
        return 0.0;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(power_kw), 0) FROM piles WHERE station_id = ?"));
    q.addBindValue(stationId);
    if (!q.exec() || !q.next()) {
        if (error) *error = queryError(q);
        return 0.0;
    }
    return q.value(0).toDouble();
}

double StationStore::currentLoadKw(int stationId, QString *error) const
{
    if (stationId <= 0) {
        if (error) *error = QStringLiteral("电站ID非法");
        return 0.0;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(power_kw), 0) FROM piles "
        "WHERE station_id = ? AND state = ?"));
    q.addBindValue(stationId);
    q.addBindValue(static_cast<int>(cp::PileState::Charging));
    if (!q.exec() || !q.next()) {
        if (error) *error = queryError(q);
        return 0.0;
    }
    return q.value(0).toDouble();
}

QVector<double> StationStore::hourlyLoadSamples(int stationId, int hours,
                                                bool *usedDemoFallback,
                                                QString *error, int *imputedHours) const
{
    QVector<double> result;
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return result;
    }
    if (stationId <= 0) {
        if (error) *error = QStringLiteral("电站ID非法");
        return result;
    }

    hours = std::max(1, std::min(hours, 48));
    const QDateTime anchorHour = currentHourStart();
    const QDateTime windowStart = anchorHour.addSecs(-(hours - 1) * 3600);
    const QDateTime windowEnd = anchorHour.addSecs(3600);

    // 1) 真实聚合优先：各桩小时内平均功率，再按站求和（采样次数不充当功率权重）
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT strftime('%Y-%m-%d %H:00:00',l.logged_at), AVG(l.real_power) "
        "FROM pile_power_logs l "
        "JOIN piles p ON p.id = l.pile_id "
        "WHERE p.station_id = ? AND l.real_power >= 0 "
        "  AND l.logged_at >= ? AND l.logged_at < ? "
        "GROUP BY p.id,strftime('%Y-%m-%d %H:00:00',l.logged_at)"));
    q.addBindValue(stationId);
    q.addBindValue(windowStart.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    q.addBindValue(windowEnd.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (!q.exec()) {
        if (error) *error = queryError(q);
        return result;
    }

    QHash<qint64, double> loadByHour;
    while (q.next()) {
        const QDateTime logged = QDateTime::fromString(q.value(0).toString(),
                                                       QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!logged.isValid())
            continue;
        const QDateTime bucketStart(logged.date(), QTime(logged.time().hour(), 0));
        loadByHour[bucketStart.toMSecsSinceEpoch()] += q.value(1).toDouble();
    }

    const double capacityKw = ratedCapacityKw(stationId, error);
    const double currentKw = currentLoadKw(stationId, error);
    const int observedSamples = loadByHour.size();
    result = interpolateHours(loadByHour,windowStart,hours,imputedHours);

    const int needReal = std::max(3, hours / 3);
    if (observedSamples >= needReal) {
        if (usedDemoFallback) *usedDemoFallback = false;
        return result;
    }

    // 2) 样本不足 → 仿真采样兜底（确定性，答辩演示稳定）
    if (usedDemoFallback) *usedDemoFallback = true;
    return demoHourlyLoadSeries(stationId, hours, anchorHour, capacityKw, currentKw);
}

QVector<double> StationStore::platformHourlyLoadSamples(int hours,
                                                        bool *usedDemoFallback,
                                                        QString *error, int *imputedHours) const
{
    QVector<double> result;
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return result;
    }

    hours = std::max(1, std::min(hours, 48));
    const QDateTime anchorHour = currentHourStart();
    const QDateTime windowStart = anchorHour.addSecs(-(hours - 1) * 3600);
    const QDateTime windowEnd = anchorHour.addSecs(3600);

    // 1) 真实聚合优先：各桩小时内平均功率，再按平台求和
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT strftime('%Y-%m-%d %H:00:00',l.logged_at), AVG(l.real_power) "
        "FROM pile_power_logs l "
        "JOIN piles p ON p.id = l.pile_id "
        "WHERE l.real_power >= 0 "
        "  AND l.logged_at >= ? AND l.logged_at < ? "
        "GROUP BY p.id,strftime('%Y-%m-%d %H:00:00',l.logged_at)"));
    q.addBindValue(windowStart.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    q.addBindValue(windowEnd.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (!q.exec()) {
        if (error) *error = queryError(q);
        return result;
    }

    QHash<qint64, double> loadByHour;
    while (q.next()) {
        const QDateTime logged = QDateTime::fromString(q.value(0).toString(),
                                                       QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!logged.isValid())
            continue;
        const QDateTime bucketStart(logged.date(), QTime(logged.time().hour(), 0));
        loadByHour[bucketStart.toMSecsSinceEpoch()] += q.value(1).toDouble();
    }

    QString capError;
    QString curError;
    const double capacityKw =
        sumPilesPower(m_db, -1, &capError);
    const double currentKw =
        sumPilesPower(m_db, static_cast<int>(cp::PileState::Charging), &curError);
    if (capacityKw < 0.0 || currentKw < 0.0) {
        if (error)
            *error = QStringLiteral("统计平台功率失败：%1%2")
                         .arg(capError, curError);
        return result;
    }

    const int observedSamples = loadByHour.size();
    result = interpolateHours(loadByHour,windowStart,hours,imputedHours);

    const int needReal = std::max(3, hours / 3);
    if (observedSamples >= needReal) {
        if (usedDemoFallback) *usedDemoFallback = false;
        return result;
    }

    // 2) 样本不足 → 全平台仿真采样兜底（确定性，答辩演示稳定）
    if (usedDemoFallback) *usedDemoFallback = true;
    return demoHourlyLoadSeries(0, hours, anchorHour, capacityKw, currentKw);
}

bool StationStore::dashboardSnapshot(DashboardSnapshot *out, int forecastHorizon,
                                     QString *error) const
{
    if (!out) {
        if (error) *error = QStringLiteral("输出参数为空");
        return false;
    }
    if (!isOpen()) {
        if (error) *error = QStringLiteral("数据库未打开");
        return false;
    }

    DashboardSnapshot snap;

    // 1) 桩状态总量与在线率（真实库，与充电站管理同源）
    QSqlQuery pilesQ(m_db);
    if (!pilesQ.exec(QStringLiteral(
            "SELECT COUNT(*), "
            "       COALESCE(SUM(CASE WHEN state = 0 THEN 1 ELSE 0 END), 0), "
            "       COALESCE(SUM(CASE WHEN state = 1 THEN 1 ELSE 0 END), 0), "
            "       COALESCE(SUM(CASE WHEN state = 2 THEN 1 ELSE 0 END), 0) "
            "FROM piles"))) {
        if (error) *error = queryError(pilesQ);
        return false;
    }
    pilesQ.next();
    snap.totalPiles     = pilesQ.value(0).toInt();
    snap.idlePiles      = pilesQ.value(1).toInt();
    snap.chargingPiles  = pilesQ.value(2).toInt();
    snap.faultPiles     = pilesQ.value(3).toInt();
    if (snap.totalPiles > 0) {
        snap.onlineRate = qRound(100.0 * (snap.totalPiles - snap.faultPiles)
                                 / snap.totalPiles * 10.0) / 10.0;
    }

    // 2) 平台额定容量与实时总负荷
    QString powerError;
    snap.totalCapacityKw = sumPilesPower(m_db, -1, &powerError);
    if (snap.totalCapacityKw < 0.0) {
        if (error) *error = QStringLiteral("统计平台额定容量失败：%1").arg(powerError);
        return false;
    }
    powerError.clear();
    snap.currentLoadKw =
        sumPilesPower(m_db, static_cast<int>(cp::PileState::Charging), &powerError);
    if (snap.currentLoadKw < 0.0) {
        if (error) *error = QStringLiteral("统计实时总负荷失败：%1").arg(powerError);
        return false;
    }

    // 3) 今日与近 7 日已完成订单（营收/订单数/电量，缺日补 0）
    const QDate today = QDate::currentDate();
    const QDate firstDay = today.addDays(-6);
    QSqlQuery orderQ(m_db);
    orderQ.prepare(QStringLiteral(
        "SELECT substr(end_time, 1, 10) AS day, COUNT(*) AS cnt, "
        "       COALESCE(SUM(amount), 0) AS revenue, "
        "       COALESCE(SUM(kwh), 0) AS energy "
        "FROM orders "
        "WHERE state = ? AND end_time IS NOT NULL AND end_time <> '' "
        "  AND substr(end_time, 1, 10) BETWEEN ? AND ? "
        "GROUP BY day"));
    orderQ.addBindValue(static_cast<int>(cp::OrderState::Finished));
    orderQ.addBindValue(firstDay.toString(QStringLiteral("yyyy-MM-dd")));
    orderQ.addBindValue(today.toString(QStringLiteral("yyyy-MM-dd")));
    if (!orderQ.exec()) {
        if (error) *error = queryError(orderQ);
        return false;
    }

    QHash<QString, DayOrderAgg> aggByDay;
    while (orderQ.next()) {
        DayOrderAgg agg;
        agg.orders   = orderQ.value(1).toInt();
        agg.revenue  = orderQ.value(2).toDouble();
        agg.energyKwh = orderQ.value(3).toDouble();
        aggByDay.insert(orderQ.value(0).toString(), agg);
    }

    snap.revenue7d.reserve(7);
    snap.order7d.reserve(7);
    for (int offset = 0; offset < 7; ++offset) {
        const QDate day = firstDay.addDays(offset);
        const QString key = day.toString(QStringLiteral("yyyy-MM-dd"));
        const DayOrderAgg agg = aggByDay.value(key);
        snap.revenue7d.push_back(qRound(agg.revenue * 100.0) / 100.0);
        snap.order7d.push_back(agg.orders);
        if (day == today) {
            snap.todayOrders = agg.orders;
            snap.todayRevenueYuan = qRound(agg.revenue * 100.0) / 100.0;
            snap.todayEnergyKwh = qRound(agg.energyKwh * 100.0) / 100.0;
        }
    }

    // 4) 近 24h 平台负荷（真实聚合优先 + 仿真兜底）与 NO.17 预测
    QString loadError;
    bool usedDemoFallback = false;
    snap.load24hKw = platformHourlyLoadSamples(24, &usedDemoFallback, &loadError, &snap.loadImputedHours);
    if (!loadError.isEmpty()) {
        if (error) *error = loadError;
        return false;
    }
    snap.loadUsedDemoFallback = usedDemoFallback;

    if (!snap.load24hKw.isEmpty()) {
        forecastHorizon = std::max(1, std::min(
            forecastHorizon, cp::LoadForecast::kMaxHorizonHours));
        cp::LoadForecastInput input;
        input.historyKw = snap.load24hKw;
        input.horizonHours = forecastHorizon;
        input.capacityKw = snap.totalCapacityKw;
        input.model = cp::ForecastModel::OLS;
        const cp::LoadForecastResult result = cp::forecastLoad(input);

        snap.forecastOk = result.ok;
        snap.forecastError = result.error;
        snap.forecastKw = result.forecastKw;
        snap.forecastHorizon = result.forecastKw.size();
        snap.forecastModelName = result.modelName;
        snap.forecastTrendText = cp::loadTrendText(result.trend);
        snap.forecastPeakKw = result.peakForecastKw;
        snap.forecastPeakHour = result.peakHourOffset;
        snap.forecastSamplesUsed = result.samplesUsed;
    }

    *out = snap;
    return true;
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

} // namespace pcserver
