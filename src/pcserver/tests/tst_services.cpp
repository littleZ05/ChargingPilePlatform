#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "pricingservice.h"
#include "selfhealservice.h"

/** 创新点落地版服务测试（价格策略自动引擎 + 自愈检查自动服务） */
class TestServices : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void pricingAutoDiscount();
    void selfHealAutoRestart();
private:
    QTemporaryDir m_tmp;
    QString m_db;
    void exec(const QString &sql);
};

void TestServices::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    m_db = m_tmp.filePath(QStringLiteral("svc.db"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("setup"));
    db.setDatabaseName(m_db);
    QVERIFY(db.open());
    exec(QStringLiteral(
        "CREATE TABLE stations(id INTEGER PRIMARY KEY, name TEXT, base_price REAL DEFAULT 1.0);"
        "CREATE TABLE piles(id INTEGER PRIMARY KEY, station_id INTEGER, code TEXT,"
        " power_kw REAL DEFAULT 60, state INTEGER DEFAULT 0);"
        "CREATE TABLE marketing_strategy(id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " station_id INTEGER NOT NULL UNIQUE, base_price REAL, discount REAL,"
        " rule_desc TEXT, is_active INTEGER DEFAULT 1);"
        "CREATE TABLE pile_health_metrics(id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " pile_id INTEGER NOT NULL UNIQUE, avg_power REAL, low_threshold REAL,"
        " high_threshold REAL, sample_count INTEGER DEFAULT 0);"
        "CREATE TABLE pile_power_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " pile_id INTEGER NOT NULL, real_power REAL, logged_at TEXT DEFAULT '');"));
    exec(QStringLiteral("INSERT INTO stations(id,name) VALUES(1,'A站');"));
    exec(QStringLiteral("INSERT INTO piles(id,station_id,code,power_kw,state) "
                        "VALUES(1,1,'P1',60,0),(2,1,'P2',60,0),(3,1,'P3',60,1);"));
    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("setup"));
}

void TestServices::exec(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database(QStringLiteral("setup")));
    const QStringList stmts = sql.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &s : stmts)
        QVERIFY2(q.exec(s), q.lastError().text().toUtf8());
}

void TestServices::pricingAutoDiscount()
{
    pcserver::PricingService svc(m_db);
    QVERIFY(svc.start(60000));   // 定时器不触发，直接 runOnce 由测试驱动
    // 提供预测空闲率 80% > 60%
    svc.setIdleRateProvider([](int) { return 80.0; });
    svc.runOnce();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("chk"));
    db.setDatabaseName(m_db);
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("SELECT discount,is_active FROM marketing_strategy WHERE station_id=1")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toDouble(), 0.8);
    QCOMPARE(q.value(1).toInt(), 1);
    q.finish();
    // 空闲率回落 30% -> 自动恢复
    svc.setIdleRateProvider([](int) { return 30.0; });
    svc.runOnce();
    QVERIFY(q.exec(QStringLiteral("SELECT is_active FROM marketing_strategy WHERE station_id=1")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    q.finish();
    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("chk"));
}

void TestServices::selfHealAutoRestart()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("seed"));
    db.setDatabaseName(m_db);
    QVERIFY(db.open());
    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO pile_power_logs(pile_id,real_power) VALUES(1,10),(1,10),(1,10)")));
    db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("seed"));

    pcserver::SelfHealService svc(m_db);
    QVERIFY(svc.start(60000));
    svc.runOnce();
    QTest::qWait(1000);   // 等待 800ms 模拟重启回调

    QSqlDatabase db2 = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("chk2"));
    db2.setDatabaseName(m_db);
    QVERIFY(db2.open());
    QSqlQuery q2(db2);
    QVERIFY(q2.exec(QStringLiteral("SELECT state FROM piles WHERE id=1")));
    QVERIFY(q2.next());
    QCOMPARE(q2.value(0).toInt(), 0);   // 重启后恢复闲置
    q2.finish();
    db2.close();
    QSqlDatabase::removeDatabase(QStringLiteral("chk2"));
}

QTEST_MAIN(TestServices)
#include "tst_services.moc"
