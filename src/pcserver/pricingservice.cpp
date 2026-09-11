#include "pricingservice.h"
#include "../common/common.h"
#include "../common/loadforecast.h"
#include "stationstore.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>

#include <algorithm>

namespace pcserver {

PricingService *&activePricingService()
{
    static PricingService *instance = nullptr;
    return instance;
}

double predictIdleRatePercent(StationStore &store, int stationId)
{
    if (!store.isOpen())
        return -1.0;

    QString err;
    const double capacityKw = store.ratedCapacityKw(stationId, &err);
    if (capacityKw <= 0.0)
        return -1.0;

    bool usedDemoFallback = false;
    const QVector<double> history =
        store.hourlyLoadSamples(stationId, 12, &usedDemoFallback, &err);
    if (history.size() < cp::LoadForecast::kMinSamples)
        return -1.0;

    cp::LoadForecastInput input;
    input.historyKw = history;
    input.horizonHours = cp::Pricing::kPredictHours;
    input.capacityKw = capacityKw;
    const cp::LoadForecastResult result = cp::forecastLoad(input);
    if (!result.ok || result.forecastKw.isEmpty())
        return -1.0;

    const double predictedKw = result.forecastKw.first();
    return std::max(0.0, std::min(100.0, (1.0 - predictedKw / capacityKw) * 100.0));
}

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
    activePricingService() = this;   // 注册活动实例，供「价格策略」页即时重算
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
    const bool rateValid = rate >= 0.0;
    const double threshold = cp::Pricing::kIdleRateThreshold * 100.0;
    const bool discount = rateValid && rate > threshold;
    const QString now =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QSqlQuery exist(m_db);
    exist.prepare(QStringLiteral(
        "SELECT id FROM marketing_strategy WHERE station_id=? LIMIT 1"));
    exist.addBindValue(stationId);
    const bool hasRow = exist.exec() && exist.next();

    QSqlQuery q(m_db);
    if (discount) {
        const QString rule =
            QStringLiteral("预测空闲率 %1% > %2%：自动 %3 折（闲时特惠）")
                .arg(rate, 0, 'f', 1).arg(threshold, 0, 'f', 0)
                .arg(cp::Pricing::kDiscount * 10.0, 0, 'f', 1);
        if (hasRow) {
            // 每站只保留一条策略记录：命中折扣则就地更新（含定价依据与决策时间）
            q.prepare(QStringLiteral(
                "UPDATE marketing_strategy SET base_price=(SELECT base_price FROM stations WHERE id=?), "
                "discount=?, rule_desc=?, is_active=1, predicted_idle_rate=?, decided_at=? "
                "WHERE station_id=?"));
            q.addBindValue(stationId);
            q.addBindValue(cp::Pricing::kDiscount);
            q.addBindValue(rule);
            q.addBindValue(rate);
            q.addBindValue(now);
            q.addBindValue(stationId);
        } else {
            q.prepare(QStringLiteral(
                "INSERT INTO marketing_strategy(station_id,base_price,discount,rule_desc,"
                "is_active,predicted_idle_rate,decided_at) "
                "VALUES(?, (SELECT base_price FROM stations WHERE id=?), ?, ?, 1, ?, ?)"));
            q.addBindValue(stationId);
            q.addBindValue(stationId);
            q.addBindValue(cp::Pricing::kDiscount);
            q.addBindValue(rule);
            q.addBindValue(rate);
            q.addBindValue(now);
        }
        if (q.exec())
            emit message(QStringLiteral("[价格策略] 电站 %1：预测空闲率 %2% > %3%，%4 折生效")
                             .arg(stationId).arg(rate, 0, 'f', 1).arg(threshold, 0, 'f', 0)
                             .arg(cp::Pricing::kDiscount * 10.0, 0, 'f', 1));
    } else {
        const QString rule =
            rateValid
                ? QStringLiteral("预测空闲率 %1% ≤ %2%：恢复基础价")
                      .arg(rate, 0, 'f', 1).arg(threshold, 0, 'f', 0)
                : QStringLiteral("预测依据不可用（容量或样本不足）：维持基础价");
        q.prepare(QStringLiteral(
            "UPDATE marketing_strategy SET is_active=0, rule_desc=?, predicted_idle_rate=?, "
            "decided_at=? WHERE station_id=?"));
        q.addBindValue(rule);
        q.addBindValue(rateValid ? rate : 0.0);
        q.addBindValue(now);
        q.addBindValue(stationId);
        q.exec();
        emit message(QStringLiteral("[价格策略] 电站 %1：%2").arg(stationId).arg(rule));
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
