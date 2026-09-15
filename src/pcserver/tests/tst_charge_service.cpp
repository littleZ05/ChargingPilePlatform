#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include "charge_service.h"
#include "stationstore.h"

using namespace pcserver;
class ChargeServiceTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void identityAndOneActiveOrder();
    void snapshotAndReplayAcrossRestart();
    void insufficientBalanceRollsBackThenRecharge();
    void receiptFailureRollsBackEffects();
    void replayCannotSettleNewOrder();
    void profilePersistenceAndInvalidImage();
    void frozenUserCanSettleButCannotStart();
    void invalidEnergyRejected();
    void conflictingMigrationRollsBack();
private:
    QTemporaryDir temporary;
    StationStore store;
    QString path;
    void sql(const QString &text);
    double scalar(const QString &text);
    QJsonObject send(int type, QJsonObject r, const QString &phone="13800138001");
    QJsonObject start(const QString &id="start-1", const QString &pile="P1");
};
void ChargeServiceTest::sql(const QString &text)
{
    QSqlQuery q(QSqlDatabase::database(store.connectionName()));
    QVERIFY2(q.exec(text), qPrintable(q.lastError().text()));
}
double ChargeServiceTest::scalar(const QString &text)
{
    QSqlQuery q(QSqlDatabase::database(store.connectionName()));
    if (!q.exec(text) || !q.next())
        return -999;
    return q.value(0).toDouble();
}
QJsonObject ChargeServiceTest::send(int type, QJsonObject r, const QString &phone)
{
    return ChargeService(&store).execute(type, phone, r);
}
QJsonObject ChargeServiceTest::start(const QString &id, const QString &pile)
{
    return send(cp::MsgType::kStartCharge, {{"request_id",id},{"pile_code",pile}});
}
void ChargeServiceTest::init()
{
    path = temporary.filePath(QUuid::createUuid().toString()+".db");
    QString error;
    QVERIFY2(store.open(path,&error),qPrintable(error));
    sql("INSERT INTO users(id,phone,nickname,balance) VALUES(1,'13800138001','甲',100),(2,'13800138002','乙',100)");
    sql("INSERT INTO stations(id,name,address,base_price) VALUES(1,'测试站','地址',2)");
    sql("INSERT INTO piles(id,station_id,code,power_kw) VALUES(1,1,'P1x',60),(2,1,'P2x',60)");
    sql("UPDATE piles SET code='P01' WHERE id=1");
    sql("UPDATE piles SET code='P02' WHERE id=2");
}
void ChargeServiceTest::cleanup() { store.close(); }
void ChargeServiceTest::identityAndOneActiveOrder()
{
    QCOMPARE(send(4,{{"request_id","a"},{"pile_code","P01"}},"").value("code").toInt(),401);
    QCOMPARE(send(4,{{"request_id","a"},{"phone","13800138002"},{"pile_code","P01"}}).value("code").toInt(),403);
    auto first=start("a","P01"); QCOMPARE(first.value("code").toInt(),0);
    QCOMPARE(start("b","P02").value("code").toInt(),409);
    QCOMPARE(send(2,{{"request_id","c"},{"order_id",first.value("order_id")},{"kwh",0}},"13800138002").value("code").toInt(),404);
    QCOMPARE(send(6,{}).value("active_order").toObject().value("order_id"),first.value("order_id"));
    QCOMPARE(scalar("SELECT COUNT(*) FROM orders"),1.0);
}
void ChargeServiceTest::snapshotAndReplayAcrossRestart()
{
    auto first=start("a","P01"); QCOMPARE(first.value("code").toInt(),0);
    sql("UPDATE orders SET start_time=datetime('now','localtime','-1 hour')");
    sql("UPDATE stations SET base_price=99");
    const QJsonObject request{{"request_id","settle"},{"order_id",first.value("order_id")},{"kwh",10}};
    auto result=send(2,request); QCOMPARE(result.value("code").toInt(),0);
    QCOMPARE(result.value("amount").toDouble(),20.0);
    store.close(); QString err; QVERIFY2(store.open(path,&err),qPrintable(err));
    QCOMPARE(send(2,request).value("amount").toDouble(),20.0);
    QCOMPARE(scalar("SELECT balance FROM users WHERE id=1"),80.0);
    QCOMPARE(scalar("SELECT charge_count FROM piles WHERE id=1"),1.0);
    QCOMPARE(scalar("SELECT COUNT(*) FROM pile_power_logs"),1.0);
}
void ChargeServiceTest::insufficientBalanceRollsBackThenRecharge()
{
    auto first=start("a","P01");
    sql("UPDATE orders SET start_time=datetime('now','localtime','-1 hour')");
    sql("UPDATE users SET balance=1 WHERE id=1");
    QJsonObject request{{"request_id","s"},{"order_id",first.value("order_id")},{"kwh",10}};
    QCOMPARE(send(2,request).value("code").toInt(),409);
    QCOMPARE(scalar("SELECT state FROM orders"),0.0);
    QCOMPARE(scalar("SELECT state FROM piles WHERE id=1"),1.0);
    const QJsonObject recharge{{"request_id","r"},{"amount",50}};
    QCOMPARE(send(30,recharge).value("code").toInt(),0);
    QCOMPARE(send(30,recharge).value("balance").toDouble(),51.0);
    QCOMPARE(send(2,request).value("balance").toDouble(),31.0);
}
void ChargeServiceTest::receiptFailureRollsBackEffects()
{
    sql("CREATE TRIGGER reject_receipt BEFORE INSERT ON request_receipts BEGIN SELECT RAISE(ABORT,'test'); END");
    QCOMPARE(send(30,{{"request_id","r"},{"amount",50}}).value("code").toInt(),503);
    QCOMPARE(scalar("SELECT balance FROM users WHERE id=1"),100.0);
    QCOMPARE(start("a","P01").value("code").toInt(),503);
    QCOMPARE(scalar("SELECT COUNT(*) FROM orders"),0.0);
    QCOMPARE(scalar("SELECT state FROM piles WHERE id=1"),0.0);
}
void ChargeServiceTest::replayCannotSettleNewOrder()
{
    auto first=start("a","P01");
    QJsonObject request{{"request_id","s"},{"order_id",first.value("order_id")},{"kwh",0}};
    QCOMPARE(send(2,request).value("code").toInt(),0);
    auto second=start("b","P01"); QCOMPARE(second.value("code").toInt(),0);
    QCOMPARE(send(2,request).value("code").toInt(),0);
    request["request_id"]="another-settle";
    QCOMPARE(send(2,request).value("code").toInt(),0);
    QCOMPARE(scalar("SELECT COUNT(*) FROM orders WHERE state=0"),1.0);
    QCOMPARE(scalar("SELECT state FROM piles WHERE id=1"),1.0);
    QCOMPARE(start("a","P02").value("code").toInt(),409);
}
void ChargeServiceTest::profilePersistenceAndInvalidImage()
{
    QImage image(32,32,QImage::Format_RGB32); image.fill(Qt::red);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer,"PNG"));
    QCOMPARE(send(7,{{"request_id","p"},{"nickname","新昵称"},{"avatar",QString::fromLatin1(bytes.toBase64())}}).value("code").toInt(),0);
    store.close(); QString error; QVERIFY(store.open(path,&error));
    auto user=send(6,{}); QCOMPARE(user.value("nickname").toString(),QStringLiteral("新昵称"));
    QVERIFY(!user.value("avatar").toString().isEmpty());
    QCOMPARE(send(7,{{"request_id","bad"},{"nickname","不应保存"},{"avatar","not image"}}).value("code").toInt(),422);
    QCOMPARE(send(6,{}).value("nickname").toString(),QStringLiteral("新昵称"));
}
void ChargeServiceTest::frozenUserCanSettleButCannotStart()
{
    auto first=start("a","P01"); sql("UPDATE users SET status=1 WHERE id=1");
    QCOMPARE(send(2,{{"request_id","s"},{"order_id",first.value("order_id")},{"kwh",0}}).value("code").toInt(),0);
    QCOMPARE(start("b","P01").value("code").toInt(),403);
    QCOMPARE(send(30,{{"request_id","r"},{"amount",1}}).value("code").toInt(),0);
}
void ChargeServiceTest::invalidEnergyRejected()
{
    auto first=start("a","P01");
    QCOMPARE(send(2,{{"request_id","s"},{"order_id",first.value("order_id")},{"kwh",10000}}).value("code").toInt(),422);
    QCOMPARE(scalar("SELECT state FROM orders"),0.0);
}
void ChargeServiceTest::conflictingMigrationRollsBack()
{
    sql("DROP INDEX idx_one_active_order_user");
    sql("DROP TABLE request_receipts");
    sql("PRAGMA user_version=2");
    sql("INSERT INTO orders(user_id,pile_id,station_id,state) VALUES(1,1,1,0),(1,2,1,0)");
    store.close();
    QString error;
    QVERIFY(!store.open(path, &error));
    QVERIFY(error.contains(QStringLiteral("已回滚")));
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "migration-check");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        {
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM orders")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
            QVERIFY(q.exec("PRAGMA user_version")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), 2);
            QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master WHERE name='request_receipts'"));
            QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("migration-check");
}
QTEST_GUILESS_MAIN(ChargeServiceTest)
#include "tst_charge_service.moc"
