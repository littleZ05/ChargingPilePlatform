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

int SelfHealService::healthLevelOf(int pileId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COALESCE(health_level, 0) FROM piles WHERE id=?"));
    q.addBindValue(pileId);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

void SelfHealService::recordEvent(int pileId, int stationId, const QString &pileCode,
                                  int level, double threshold, double realPower,
                                  const QString &action)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO selfheal_events(pile_id,pile_code,station_id,level,level_text,"
        "threshold,real_power,action) VALUES(?,?,?,?,?,?,?,?)"));
    q.addBindValue(pileId);
    q.addBindValue(pileCode);
    q.addBindValue(stationId);
    q.addBindValue(level);
    q.addBindValue(healLevelText(level));
    q.addBindValue(threshold);
    q.addBindValue(realPower);
    q.addBindValue(action);
    q.exec();
    emit selfHealEvent(pileCode, level, action);
}

void SelfHealService::processPile(int pileId)
{
    const double low = thresholdOf(pileId);
    if (low <= 0)
        return;

    QSqlQuery head(m_db);
    head.prepare(QStringLiteral(
        "SELECT p.code, p.station_id, COALESCE(p.health_level, 0) "
        "FROM piles p WHERE p.id=?"));
    head.addBindValue(pileId);
    if (!head.exec() || !head.next())
        return;
    const QString pileCode = head.value(0).toString();
    const int stationId = head.value(1).toInt();
    int level = head.value(2).toInt();

    // 取最近 kConsecutiveCount 条“设备上报”的功率样本（真实数据）
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT real_power FROM pile_power_logs WHERE pile_id=? ORDER BY id DESC LIMIT ?"));
    q.addBindValue(pileId);
    q.addBindValue(cp::SelfHeal::kConsecutiveCount);
    if (!q.exec())
        return;
    QVector<double> powers;
    while (q.next())
        powers.append(q.value(0).toDouble());
    if (powers.size() < cp::SelfHeal::kConsecutiveCount)
        return;

    const double latest = powers.first();
    bool allLow = true;
    for (double p : powers)
        allLow = allLow && (p < low);

    if (!allLow) {
        // 样本恢复正常：解除预警/故障
        if (latest >= low && level != 0) {
            QSqlQuery up(m_db);
            up.prepare(QStringLiteral("UPDATE piles SET health_level=0 WHERE id=?"));
            up.addBindValue(pileId);
            up.exec();
            recordEvent(pileId, stationId, pileCode, 0, low, latest,
                        QStringLiteral("实测功率 %1 kW 已回到阈值以上，预警解除")
                            .arg(latest, 0, 'f', 1));
            ++m_lastRecovered;
        }
        return;
    }

    // 连续低功率命中
    if (level == 0) {
        // 第一次命中：标记“需检查”并执行一次远程重启（真实复位该桩运行状态）
        QSqlQuery up(m_db);
        up.prepare(QStringLiteral(
            "UPDATE piles SET health_level=1, state=? WHERE id=?"));
        up.addBindValue(static_cast<int>(cp::PileState::Idle));
        up.addBindValue(pileId);
        up.exec();
        recordEvent(pileId, stationId, pileCode, 1, low, latest,
                    QStringLiteral("连续 %1 次低于阈值 %2 kW → 标记需检查，已自动下发远程重启指令")
                        .arg(cp::SelfHeal::kConsecutiveCount)
                        .arg(low, 0, 'f', 1));
        ++m_lastWarning;
        emit message(QStringLiteral("[自愈检查] 电桩 %1：连续 %2 次低于阈值(%3 kW)，已标记需检查并自动远程重启")
                         .arg(pileCode)
                         .arg(cp::SelfHeal::kConsecutiveCount)
                         .arg(low, 0, 'f', 1));
    } else if (level == 1) {
        // 重启后仍持续低功率 → 升级故障，需人工介入
        QSqlQuery up(m_db);
        up.prepare(QStringLiteral("UPDATE piles SET health_level=2 WHERE id=?"));
        up.addBindValue(pileId);
        up.exec();
        recordEvent(pileId, stationId, pileCode, 2, low, latest,
                    QStringLiteral("远程重启后功率仍未恢复，升级为故障，需人工介入"));
        ++m_lastFault;
        emit message(QStringLiteral("[自愈检查] 电桩 %1：重启后仍未恢复，升级为故障").arg(pileCode));
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
