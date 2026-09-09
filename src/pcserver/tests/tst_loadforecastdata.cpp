#include <QtTest/QtTest>

#include <cmath>

#include "../../common/loadforecast.h"
#include "../stationstore.h"
#include "test_dbpath.h"

using namespace pcserver;

/** NO.17 数据源策略（真实聚合优先 + 仿真兜底）与算法联调测试（负责人：吴羽桐） */
class TstLoadForecastData : public QObject
{
    Q_OBJECT
private slots:
    void demoFallbackSamplesFeedEngine();
    void realLogAggregationTakesPriority();
};

namespace {

bool near(double a, double b, double eps = 1e-6)
{
    return qAbs(a - b) <= eps;
}

} // namespace

void TstLoadForecastData::demoFallbackSamplesFeedEngine()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("loadforecast_demo.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const auto stations = store.listStations();
    QVERIFY(!stations.isEmpty());
    const int stationId = stations.first().id;
    const double capacityKw = store.ratedCapacityKw(stationId, &error);
    QVERIFY(capacityKw > 0.0);
    QVERIFY(store.currentLoadKw(stationId, &error) > 0.0);  // 种子桩含充电中

    bool usedDemo = false;
    const QVector<double> history =
        store.hourlyLoadSamples(stationId, 24, &usedDemo, &error);
    QCOMPARE(history.size(), 24);
    QVERIFY(usedDemo);  // 空库无功率日志 → 仿真兜底
    for (double v : history) {
        QVERIFY(std::isfinite(v));
        QVERIFY(v >= 0.0);
        QVERIFY(v <= capacityKw + 1e-6);
    }

    cp::LoadForecastInput input;
    input.historyKw = history;
    input.horizonHours = 6;
    input.capacityKw = capacityKw;
    const cp::LoadForecastResult result = cp::forecastLoad(input);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.forecastKw.size(), 6);
    for (double v : result.forecastKw) {
        QVERIFY(std::isfinite(v));
        QVERIFY(v >= 0.0);
        QVERIFY(v <= capacityKw + 1e-6);
    }
}

void TstLoadForecastData::realLogAggregationTakesPriority()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("loadforecast_real.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const auto stations = store.listStations();
    const int stationId = stations.first().id;
    const auto piles = store.listPiles(stationId);
    QVERIFY(!piles.isEmpty());
    const int pileId = piles.first().id;

    // 最近 4 个完整小时各写入一条功率日志（12h 窗口需要 ≥4 个有效桶）
    const QDateTime now = QDateTime::currentDateTime();
    for (int i = 1; i <= 4; ++i) {
        const QDateTime when = now.addSecs(-i * 3600);
        const double powerKw = 25.0 + i * 5.0;  // 30/35/40/45
        const QVariantList binds = {
            pileId, powerKw, when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        };
        QVERIFY2(store.execPrepared(
                     QStringLiteral(
                         "INSERT INTO pile_power_logs(pile_id, real_power, logged_at) "
                         "VALUES(?, ?, ?)"),
                     binds, &error),
                 qPrintable(error));
    }

    bool usedDemo = true;
    const QVector<double> history =
        store.hourlyLoadSamples(stationId, 12, &usedDemo, &error);
    QCOMPARE(history.size(), 12);
    QVERIFY(!usedDemo);  // 有效样本足够 → 真实聚合优先

    int nonZero = 0;
    for (double v : history) {
        QVERIFY(std::isfinite(v));
        QVERIFY(v >= 0.0);
        if (v > 0.0)
            ++nonZero;
    }
    QVERIFY(nonZero >= 4);

    // 四条日志的值应原样出现在对应小时桶（单桶单日志 → 值相等）
    bool seen30 = false;
    bool seen35 = false;
    bool seen40 = false;
    bool seen45 = false;
    for (double v : history) {
        seen30 = seen30 || near(v, 30.0);
        seen35 = seen35 || near(v, 35.0);
        seen40 = seen40 || near(v, 40.0);
        seen45 = seen45 || near(v, 45.0);
    }
    QVERIFY(seen30);
    QVERIFY(seen35);
    QVERIFY(seen40);
    QVERIFY(seen45);
}

QTEST_APPLESS_MAIN(TstLoadForecastData)
#include "tst_loadforecastdata.moc"
