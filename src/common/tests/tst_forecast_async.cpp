#include <QtTest>
#include <QFutureWatcher>
#include <QThread>
#include "../forecast_async.h"
class ForecastAsyncTest : public QObject
{
    Q_OBJECT
private slots:
    void executesSnapshotOutsideGuiThread() {
        cp::LoadForecastInput input;
        input.historyKw={10,20,30}; input.horizonHours=24;
        QFutureWatcher<cp::ForecastCalculation> watcher;
        QSignalSpy finished(&watcher,&QFutureWatcher<cp::ForecastCalculation>::finished);
        watcher.setFuture(cp::forecastAsync(input));
        input.historyKw.fill(999); // Submitted snapshot must not change.
        QTRY_COMPARE(finished.size(),1);
        auto value=watcher.result();
        QVERIFY(value.result.ok);
        QVERIFY(value.threadId != reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QCOMPARE(value.result.forecastKw.first(),40.0);
        QCOMPARE(value.result.forecastKw.size(),24);
    }
};
QTEST_GUILESS_MAIN(ForecastAsyncTest)
#include "tst_forecast_async.moc"
