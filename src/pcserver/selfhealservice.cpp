#include "selfhealservice.h"
#include "../common/common.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

namespace pcserver {

SelfHealService *&activeSelfHealService()
{
    static SelfHealService *instance = nullptr;
    return instance;
}

QString healLevelText(int level)
{
    switch (level) {
    case 0: return QStringLiteral("正常");
    case 1: return QStringLiteral("预警（需检查）");
    case 2: return QStringLiteral("故障");
    default: return QStringLiteral("未知");
    }
}

SelfHealService::SelfHealService(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    m_conn = QStringLiteral("selfheal_%1").arg(quintptr(this));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dbPath);
}

SelfHealService::~SelfHealService()
{
    stop();
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_conn);
}

bool SelfHealService::start(int intervalMs, QString *err)
{
    if (!m_db.open()) {
        if (err) *err = m_db.lastError().text();
        return false;
    }
    QSqlQuery busy(m_db);
    busy.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    busy.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    connect(&m_timer, &QTimer::timeout, this, &SelfHealService::runOnce);
    m_timer.start(intervalMs);
    activeSelfHealService() = this;
    emit message(QStringLiteral("[自愈检查] 自动服务已启动，周期 %1 秒").arg(intervalMs / 1000));
    runOnce();
    return true;
}

void SelfHealService::stop()
{
    if (m_timer.isActive()) m_timer.stop();
    if (activeSelfHealService() == this) activeSelfHealService() = nullptr;
}

double SelfHealService::thresholdOf(int pileId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT low_threshold FROM pile_health_metrics WHERE pile_id=?"));
    q.addBindValue(pileId);
    if (q.exec() && q.next() && q.value(0).toDouble() > 0)
        return q.value(0).toDouble();

    // 阈值缺失时用该桩历史实功率均值推算（仍是真实数据，不做任何伪造）
    q.prepare(QStringLiteral("SELECT AVG(real_power) FROM pile_power_logs WHERE pile_id=?"));
    q.addBindValue(pileId);
    if (q.exec() && q.next()) {
        const double avg = q.value(0).toDouble();
        if (avg > 0)
            return avg * (1.0 - cp::SelfHeal::kLowPowerRatio);
    }
    return 0.0;
}

void SelfHealService::processPile(int pileId)
{
    const double low = thresholdOf(pileId);
    if (low <= 0)
        return;
    // Cursor, state and event commit together; no event is emitted for a rollback.
    QSqlQuery begin(m_db);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE")))
        return;
    const auto rollback = [this] { m_db.rollback(); };
    QSqlQuery head(m_db);
    head.prepare(QStringLiteral(
        "SELECT p.code,p.station_id,p.health_level,p.state,"
        "COALESCE(c.last_sample_id,0),COALESCE(c.warning_sample_id,0) "
        "FROM piles p LEFT JOIN selfheal_cursors c ON c.pile_id=p.id WHERE p.id=?"));
    head.addBindValue(pileId);
    if (!head.exec() || !head.next()) { rollback(); return; }
    const QString code = head.value(0).toString();
    const int station = head.value(1).toInt();
    const int level = head.value(2).toInt();
    const int oldState = head.value(3).toInt();
    const qint64 last = head.value(4).toLongLong();
    qint64 warningSample = head.value(5).toLongLong();
    head.finish();
    QSqlQuery samples(m_db);
    samples.prepare(QStringLiteral(
        "SELECT id,real_power FROM pile_power_logs WHERE pile_id=? ORDER BY id DESC LIMIT ?"));
    samples.addBindValue(pileId);
    samples.addBindValue(cp::SelfHeal::kConsecutiveCount);
    if (!samples.exec()) { rollback(); return; }
    QVector<QPair<qint64,double>> powers;
    while (samples.next())
        powers.append({samples.value(0).toLongLong(),samples.value(1).toDouble()});
    samples.finish();
    if (powers.isEmpty() || powers.first().first <= last) { rollback(); return; }
    const qint64 newest = powers.first().first;
    const double latest = powers.first().second;
    bool allLow = powers.size() == cp::SelfHeal::kConsecutiveCount;
    bool afterRestart = allLow;
    for (const auto &sample : powers) {
        allLow = allLow && sample.second < low;
        afterRestart = afterRestart && sample.first > warningSample;
    }
    int nextLevel = level;
    QString action;
    if (latest >= low && level != 0) {
        nextLevel = 0;
        warningSample = 0;
        action = QStringLiteral("收到新的正常功率样本，解除自愈预警/故障");
    } else if (allLow && level == 0) {
        nextLevel = 1;
        warningSample = newest;
        action = QStringLiteral("连续3次功率低于阈值，标记需检查；执行模拟重启，保留活动订单占用");
    } else if (allLow && afterRestart && level == 1) {
        nextLevel = 2;
        action = QStringLiteral("模拟重启后又收到3条异常样本，升级为故障");
    }
    QSqlQuery cursor(m_db);
    cursor.prepare(QStringLiteral(
        "INSERT INTO selfheal_cursors(pile_id,last_sample_id,warning_sample_id) VALUES(?,?,?) "
        "ON CONFLICT(pile_id) DO UPDATE SET last_sample_id=excluded.last_sample_id,"
        "warning_sample_id=excluded.warning_sample_id"));
    cursor.addBindValue(pileId); cursor.addBindValue(newest); cursor.addBindValue(warningSample);
    if (!cursor.exec()) { rollback(); return; }
    if (nextLevel != level) {
        QSqlQuery active(m_db);
        active.prepare(QStringLiteral("SELECT COUNT(*) FROM orders WHERE pile_id=? AND state=0"));
        active.addBindValue(pileId);
        if (!active.exec() || !active.next()) { rollback(); return; }
        const bool occupied = active.value(0).toInt() > 0;
        active.finish();
        // A business order owns occupation; health failure only forbids new orders.
        const int state = occupied ? 1 : nextLevel == 2 ? 2
                          : (level == 2 ? 0 : oldState);
        QSqlQuery update(m_db);
        update.prepare(QStringLiteral("UPDATE piles SET health_level=?,state=? WHERE id=?"));
        update.addBindValue(nextLevel); update.addBindValue(state); update.addBindValue(pileId);
        if (!update.exec()) { rollback(); return; }
        QSqlQuery event(m_db);
        event.prepare(QStringLiteral(
            "INSERT INTO selfheal_events(pile_id,pile_code,station_id,level,level_text,"
            "threshold,real_power,action) VALUES(?,?,?,?,?,?,?,?)"));
        event.addBindValue(pileId); event.addBindValue(code); event.addBindValue(station);
        event.addBindValue(nextLevel); event.addBindValue(healLevelText(nextLevel));
        event.addBindValue(low); event.addBindValue(latest); event.addBindValue(action);
        if (!event.exec()) { rollback(); return; }
        QSqlQuery rate(m_db);
        rate.prepare(QStringLiteral(
            "UPDATE stations SET online_rate=COALESCE((SELECT "
            "100.0*SUM(CASE WHEN state<>2 AND health_level<>2 THEN 1 ELSE 0 END)/COUNT(*) "
            "FROM piles WHERE station_id=?),0) WHERE id=?"));
        rate.addBindValue(station); rate.addBindValue(station);
        if (!rate.exec()) { rollback(); return; }
    }
    if (!m_db.commit()) { rollback(); return; }
    if (nextLevel != level) {
        if (nextLevel == 0) ++m_lastRecovered;
        if (nextLevel == 1) ++m_lastWarning;
        if (nextLevel == 2) ++m_lastFault;
        emit selfHealEvent(code, nextLevel, action);
        emit message(QStringLiteral("[自愈检查] %1：%2").arg(code, action));
    }
}

void SelfHealService::runOnce()
{
    if (!m_db.isOpen())
        return;

    m_lastScanned = 0;
    m_lastWarning = 0;
    m_lastFault = 0;
    m_lastRecovered = 0;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id FROM piles ORDER BY id")))
        return;
    QVector<int> ids;
    while (q.next())
        ids.append(q.value(0).toInt());

    for (int id : ids) {
        ++m_lastScanned;
        processPile(id);
    }

    m_lastRunAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace pcserver
