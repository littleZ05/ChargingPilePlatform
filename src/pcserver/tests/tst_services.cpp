#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include "stationstore.h"
#include "pricingservice.h"
#include "selfhealservice.h"

class TestServices : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup() { store.close(); }
    void pricingAutoDiscount();
    void freshSamplesAndRestart();
    void activeOrderPreserved();
    void eventFailureRollsBack();
    void missingForecastDoesNotDiscount();
    void calibrateExcludesKnownAnomalies();
    void sparseMeasurementsCannotDrivePricing();
private:
    QTemporaryDir temporary;
    pcserver::StationStore store;
    QString path;
    void sql(const QString &s) {
        QSqlQuery q(QSqlDatabase::database(store.connectionName()));
        QVERIFY2(q.exec(s),qPrintable(q.lastError().text()));
    }
    int scalar(const QString &s) {
        QSqlQuery q(QSqlDatabase::database(store.connectionName()));
        if (!q.exec(s) || !q.next()) return -1;
        return q.value(0).toInt();
    }
    void low() { sql("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,10),(1,10),(1,10)"); }
};
void TestServices::init()
{
    path=temporary.filePath(QUuid::createUuid().toString()+".db");
    QString error; QVERIFY2(store.open(path,&error),qPrintable(error));
    sql("INSERT INTO stations(id,name,address,base_price) VALUES(1,'站','地址',1)");
    sql("INSERT INTO piles(id,station_id,code,power_kw) VALUES(1,1,'P01',60)");
    sql("INSERT INTO pile_health_metrics(pile_id,avg_power,low_threshold,sample_count) VALUES(1,50,40,12)");
}
void TestServices::pricingAutoDiscount()
{
    pcserver::PricingService service(path);
    service.setIdleRateProvider([](int){return 80.0;});
    QVERIFY(service.start(60000));
    QCOMPARE(scalar("SELECT CAST(discount*100 AS INTEGER) FROM marketing_strategy"),80);
    service.setIdleRateProvider([](int){return 30.0;}); service.runOnce();
    QCOMPARE(scalar("SELECT is_active FROM marketing_strategy"),0);
}
void TestServices::freshSamplesAndRestart()
{
    low();
    {
        pcserver::SelfHealService service(path); QVERIFY(service.start(60000));
        QCOMPARE(scalar("SELECT health_level FROM piles"),1);
        service.runOnce(); service.runOnce();
        QCOMPARE(scalar("SELECT health_level FROM piles"),1);
        QCOMPARE(scalar("SELECT COUNT(*) FROM selfheal_events"),1);
    }
    pcserver::SelfHealService restarted(path); QVERIFY(restarted.start(60000));
    QCOMPARE(scalar("SELECT health_level FROM piles"),1);
    sql("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,10)");
    restarted.runOnce(); QCOMPARE(scalar("SELECT health_level FROM piles"),1);
    sql("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,10),(1,10)");
    restarted.runOnce(); QCOMPARE(scalar("SELECT health_level FROM piles"),2);
    QCOMPARE(scalar("SELECT state FROM piles"),2);
    sql("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,50)");
    restarted.runOnce(); QCOMPARE(scalar("SELECT health_level FROM piles"),0);
    QCOMPARE(scalar("SELECT state FROM piles"),0);
    QCOMPARE(scalar("SELECT COUNT(*) FROM selfheal_events"),3);
}
void TestServices::activeOrderPreserved()
{
    sql("INSERT INTO users(id,phone) VALUES(1,'13800138001')");
    sql("INSERT INTO orders(user_id,pile_id,station_id,state) VALUES(1,1,1,0)");
    sql("UPDATE piles SET state=1"); low();
    pcserver::SelfHealService service(path); QVERIFY(service.start(60000));
    QCOMPARE(scalar("SELECT state FROM piles"),1);
    low(); service.runOnce();
    QCOMPARE(scalar("SELECT health_level FROM piles"),2);
    QCOMPARE(scalar("SELECT state FROM piles"),1);
    QCOMPARE(scalar("SELECT state FROM orders"),0);
}
void TestServices::eventFailureRollsBack()
{
    sql("CREATE TRIGGER reject_event BEFORE INSERT ON selfheal_events BEGIN SELECT RAISE(ABORT,'test'); END");
    low(); pcserver::SelfHealService service(path);
    QSignalSpy spy(&service,&pcserver::SelfHealService::selfHealEvent);
    QVERIFY(service.start(60000));
    QCOMPARE(scalar("SELECT health_level FROM piles"),0);
    QCOMPARE(scalar("SELECT COUNT(*) FROM selfheal_cursors"),0);
    QCOMPARE(spy.size(),0);
    sql("DROP TRIGGER reject_event"); service.runOnce();
    QCOMPARE(scalar("SELECT health_level FROM piles"),1);
    QCOMPARE(spy.size(),1);
}
void TestServices::missingForecastDoesNotDiscount()
{
    QCOMPARE(pcserver::predictIdleRatePercent(store,1),-1.0);
    pcserver::PricingService pricing(path);
    pricing.setIdleRateProvider([this](int id) { return pcserver::predictIdleRatePercent(store,id); });
    QVERIFY(pricing.start(60000));
    QCOMPARE(scalar("SELECT COUNT(*) FROM marketing_strategy WHERE is_active=1"),0);
    for(int hour=0;hour<12;++hour)
        QVERIFY(store.execPrepared("INSERT INTO pile_power_logs(pile_id,real_power,logged_at) VALUES(1,0,?)",
            {QDateTime::currentDateTime().addSecs(-hour*3600).toString("yyyy-MM-dd HH:mm:ss")}));
    QCOMPARE(pcserver::predictIdleRatePercent(store,1),100.0);
    pricing.runOnce();
    QCOMPARE(scalar("SELECT CAST(discount*100 AS INTEGER) FROM marketing_strategy WHERE is_active=1"),80);
}

void TestServices::calibrateExcludesKnownAnomalies()
{
    pcserver::SelfHealService service(path);
    QVERIFY(service.start(60000));
    QString error;
    QVERIFY(!service.rebuildThreshold(1,&error));
    QCOMPARE(scalar("SELECT low_threshold FROM pile_health_metrics"),40);
    sql("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,50),(1,50),(1,50),(1,50),(1,50),(1,50),(1,5)");
    QVERIFY2(service.rebuildThreshold(1,&error),qPrintable(error));
    QCOMPARE(scalar("SELECT sample_count FROM pile_health_metrics"),6);
    QCOMPARE(scalar("SELECT low_threshold FROM pile_health_metrics"),40);
    sql("UPDATE piles SET health_level=1");
    QVERIFY(!service.rebuildThreshold(1,&error));
}

void TestServices::sparseMeasurementsCannotDrivePricing()
{
    for(int hour=1;hour<=4;++hour)
        QVERIFY(store.execPrepared("INSERT INTO pile_power_logs(pile_id,real_power,logged_at) VALUES(1,0,?)",
            {QDateTime::currentDateTime().addSecs(-hour*3600).toString("yyyy-MM-dd HH:mm:ss")}));
    QCOMPARE(pcserver::predictIdleRatePercent(store,1),-1.0);
}

QTEST_GUILESS_MAIN(TestServices)
#include "tst_services.moc"
