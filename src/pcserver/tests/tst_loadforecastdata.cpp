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
    void repeatedSamplesDoNotMultiplyLoad();
    void measuredZeroIsNotMissing();
    void missingHoursAreMarkedAndInterpolated();
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

void TstLoadForecastData::repeatedSamplesDoNotMultiplyLoad()
{
    StationStore store; QString error;
    QVERIFY(store.open(makeTestDatabasePath(QStringLiteral("repeated-load.db")),&error));
    QVERIFY(store.seedDemoIfEmpty(&error));
    const auto station = store.listStations().first();
    const auto pile = store.listPiles(station.id).first();
    for (int hour=1; hour<=4; ++hour) {
        const auto when=QDateTime::currentDateTime().addSecs(-hour*3600).toString("yyyy-MM-dd HH:mm:ss");
        for (int repeat=0; repeat<10; ++repeat)
            QVERIFY(store.execPrepared("INSERT INTO pile_power_logs(pile_id,real_power,logged_at) VALUES(?,?,?)",
                                       {pile.id,30.0,when},&error));
    }
    bool demo=true;
    const auto values=store.hourlyLoadSamples(station.id,12,&demo,&error);
    QVERIFY(!demo);
    for (int hour=1; hour<=4; ++hour) QCOMPARE(values[11-hour],30.0);
}
void TstLoadForecastData::measuredZeroIsNotMissing()
{
    StationStore store; QString error;
    QVERIFY(store.open(makeTestDatabasePath(QStringLiteral("zero-load.db")),&error));
    QVERIFY(store.seedDemoIfEmpty(&error));
    QVERIFY(store.execPrepared("UPDATE piles SET state=0",{},&error));
    const auto station=store.listStations().first();
    const auto pile=store.listPiles(station.id).first();
    for(int hour=0;hour<12;++hour)
        QVERIFY(store.execPrepared("INSERT INTO pile_power_logs(pile_id,real_power,logged_at) VALUES(?,?,?)",
            {pile.id,0.0,QDateTime::currentDateTime().addSecs(-hour*3600).toString("yyyy-MM-dd HH:mm:ss")},&error));
    bool demo=true;
    const auto values=store.hourlyLoadSamples(station.id,12,&demo,&error);
    QVERIFY(!demo); QCOMPARE(values,QVector<double>(12,0));
    const auto platform=store.platformHourlyLoadSamples(12,&demo,&error);
    QVERIFY(!demo); QCOMPARE(platform,QVector<double>(12,0));
}

void TstLoadForecastData::missingHoursAreMarkedAndInterpolated()
{
    StationStore store; QString error;
    QVERIFY(store.open(makeTestDatabasePath(QStringLiteral("gapped-load.db")),&error));
    QVERIFY(store.seedDemoIfEmpty(&error));
    const auto station=store.listStations().first();
    const auto pile=store.listPiles(station.id).first();
    // Observations at offsets -5,-3,-2,-1: the missing -4 bucket interpolates to 20.
    const int offsets[]={5,3,2,1};
    const double power[]={10,30,0,0};
    for(int i=0;i<4;++i)
        QVERIFY(store.execPrepared("INSERT INTO pile_power_logs(pile_id,real_power,logged_at) VALUES(?,?,?)",
            {pile.id,power[i],QDateTime::currentDateTime().addSecs(-offsets[i]*3600).toString("yyyy-MM-dd HH:mm:ss")},&error));
    bool demo=true; int imputed=-1;
    const auto values=store.hourlyLoadSamples(station.id,12,&demo,&error,&imputed);
    QVERIFY(!demo); QCOMPARE(imputed,8);
    QCOMPARE(values[7],20.0);
    QCOMPARE(values[0],10.0); // Leading edge carries nearest observation, never invented zero.
    QCOMPARE(values[9],0.0); // Measured zero stays zero.
    QCOMPARE(values[11],0.0);
}

QTEST_APPLESS_MAIN(TstLoadForecastData)
#include "tst_loadforecastdata.moc"
