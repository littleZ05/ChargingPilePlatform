#include "pricingservice.h"
#include "../common/common.h"

#include <QSqlQuery>
#include <QSqlError>

namespace pcserver {
PricingService::PricingService(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    m_conn = QStringLiteral("pricing_%1").arg(quintptr(this));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dbPath);
}
PricingService::~PricingService()
{
    stop();
    if (m_db.isOpen()) m_db.close();
    QSqlDatabase::removeDatabase(m_conn);
}
bool PricingService::start(int intervalMs, QString *err)
{
    if (!m_db.open()) {
        if (err) *err = m_db.lastError().text();
        return false;
    }
    QSqlQuery busy(m_db);
    busy.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    connect(&m_timer, &QTimer::timeout, this, &PricingService::runOnce);
    m_timer.start(intervalMs);
    emit message(QStringLiteral("[价格策略] 自动引擎已启动，周期 %1 秒").arg(intervalMs / 1000));
    runOnce();
    return true;
}
void PricingService::stop()
{
    if (m_timer.isActive()) m_timer.stop();
}
void PricingService::setIdleRateProvider(std::function<double(int)> provider)
{
    m_provider = std::move(provider);
}
double PricingService::currentIdleRate(int stationId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(CASE WHEN state=0 THEN 1 ELSE 0 END),0) "
        "FROM piles WHERE station_id=?"));
    q.addBindValue(stationId);
    if (!q.exec() || !q.next() || q.value(0).toInt() == 0)
        return 0.0;
    return q.value(1).toInt() * 100.0 / q.value(0).toInt();
}
void PricingService::applyStrategy(int stationId, double rate)
{
    QSqlQuery q(m_db);
    const bool discount = rate > cp::Pricing::kIdleRateThreshold * 100.0;
    if (discount) {
        // 兼容无 UNIQUE(station_id) 约束的表结构：先更新，无行则插入
        q.prepare(QStringLiteral(
            "UPDATE marketing_strategy SET discount=?, rule_desc=?, is_active=1 "
            "WHERE station_id=?"));
        q.addBindValue(stationId);
        q.addBindValue(cp::Pricing::kDiscount);
        q.addBindValue(QStringLiteral("自动引擎：空闲率>60% 闲时特惠"));
        bool ok = q.exec();
        if (ok && q.numRowsAffected() == 0) {
            // 兼容无 UNIQUE 约束：插入前清理该站旧策略，保证每站仅一条生效记录
            QSqlQuery clean(m_db);
            clean.prepare(QStringLiteral("DELETE FROM marketing_strategy WHERE station_id=?"));
            clean.addBindValue(stationId);
            clean.exec();
            q.prepare(QStringLiteral(
                "INSERT INTO marketing_strategy(station_id,base_price,discount,rule_desc,is_active) "
                "VALUES(?, (SELECT base_price FROM stations WHERE id=?), ?, ?, 1)"));
            q.addBindValue(stationId);
            q.addBindValue(stationId);
            q.addBindValue(cp::Pricing::kDiscount);
            q.addBindValue(QStringLiteral("自动引擎：空闲率>60% 闲时特惠"));
            ok = q.exec();
        }
        if (ok)
            emit message(QStringLiteral("[价格策略] 电站 %1：空闲率 %2%>60%，8 折生效")
                             .arg(stationId).arg(rate, 0, 'f', 1));
    } else {
        q.prepare(QStringLiteral("UPDATE marketing_strategy SET is_active=0 WHERE station_id=?"));
        q.addBindValue(stationId);
        q.exec();
        emit message(QStringLiteral("[价格策略] 电站 %1：空闲率 %2%，恢复基础价")
                         .arg(stationId).arg(rate, 0, 'f', 1));
    }
}
void PricingService::runOnce()
{
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id FROM stations ORDER BY id"))) return;
    while (q.next()) {
        const int sid = q.value(0).toInt();
        const double rate = m_provider ? m_provider(sid) : currentIdleRate(sid);
        applyStrategy(sid, rate);
    }
}
}
