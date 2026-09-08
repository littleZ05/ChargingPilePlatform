#include "selfhealservice.h"
#include "../common/common.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QTimer>

namespace pcserver {
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
    emit message(QStringLiteral("[自愈检查] 自动服务已启动，周期 %1 秒").arg(intervalMs / 1000));
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
    if (low <= 0) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT real_power FROM pile_power_logs WHERE pile_id=? ORDER BY id DESC LIMIT ?"));
    q.addBindValue(pileId);
    q.addBindValue(cp::SelfHeal::kConsecutiveCount);
    if (!q.exec()) return;
    QList<double> powers;
    while (q.next()) powers << q.value(0).toDouble();
    if (powers.size() < cp::SelfHeal::kConsecutiveCount) return;
    bool allLow = true;
    for (double p : powers) allLow = allLow && (p < low);
    if (!allLow) return;
    q.prepare(QStringLiteral("UPDATE piles SET state=2 WHERE id=?"));
    q.addBindValue(pileId);
    q.exec();
    emit message(QStringLiteral("[自愈检查] 电桩 %1：连续 %2 次低于阈值(%3 kW)，触发模拟远程重启")
                     .arg(pileId).arg(cp::SelfHeal::kConsecutiveCount).arg(low, 0, 'f', 1));
    QTimer::singleShot(800, this, [this, pileId, low]() {
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral("UPDATE piles SET state=0 WHERE id=?"));
        u.addBindValue(pileId);
        u.exec();
        u.prepare(QStringLiteral(
            "INSERT INTO pile_power_logs(pile_id,real_power) VALUES(?,?)"));
        u.addBindValue(pileId);
        u.addBindValue(low * 1.2);
        u.exec();
        emit message(QStringLiteral("[自愈检查] 电桩 %1 重启完成，状态恢复正常").arg(pileId));
    });
}
void SelfHealService::runOnce()
{
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id FROM piles ORDER BY id"))) return;
    while (q.next()) processPile(q.value(0).toInt());
}
}
