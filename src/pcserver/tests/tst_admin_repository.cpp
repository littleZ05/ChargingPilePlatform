#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include "admin_repository.h"
#include "stationstore.h"
class AdminRepositoryTest : public QObject
{
    Q_OBJECT
private slots:
    void sharedContextAndMaintenanceGuards();
};
void AdminRepositoryTest::sharedContextAndMaintenanceGuards()
{
    QTemporaryDir dir; pcserver::StationStore store; QString error;
    QVERIFY(store.open(dir.filePath("admin.db"),&error));
    auto &repository=pcserver_admin::DatabaseManager::instance();
    QVERIFY(repository.attach(store,&error));
    QVERIFY(repository.initialize(&error));
    QCOMPARE(repository.users().size(),0); // No hidden demo users on ordinary startup.
    QVERIFY(store.execPrepared("INSERT INTO stations(id,name,address) VALUES(1,'站','地址')",{},&error));
    QVERIFY(store.execPrepared("INSERT INTO users(id,phone) VALUES(1,'13800138001')",{},&error));
    QCOMPARE(repository.users().size(),1);
    QVERIFY(repository.setUserStatus(1,1,&error));
    QSqlQuery q(QSqlDatabase::database(store.connectionName()));
    QVERIFY(q.exec("SELECT status FROM users WHERE id=1")); QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(),1); q.finish();
    int pile=0;
    QVERIFY(repository.addPile(1,"P01",QStringLiteral("快充"),60,0,&pile,&error));
    QVERIFY(!repository.setPileState(pile,1,&error));
    QVERIFY(store.execPrepared("INSERT INTO orders(user_id,pile_id,station_id,state) VALUES(1,?,1,0)",{pile},&error));
    QVERIFY(store.execPrepared("UPDATE piles SET state=1 WHERE id=?",{pile},&error));
    QVERIFY(!repository.remoteRestartPile(pile,nullptr,&error));
    QVERIFY(!repository.setPileState(pile,0,&error));
    QVERIFY(!repository.updatePile(pile,1,"P02",QStringLiteral("快充"),80,0,&error));
    QVERIFY(!repository.deletePile(pile,&error));
    QVERIFY(q.exec("SELECT state FROM orders")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),0); q.finish();
    QVERIFY(store.execPrepared("UPDATE orders SET state=1",{},&error));
    QVERIFY(!repository.deletePile(pile,&error)); // Completed accounting history is immutable.
    QVERIFY(repository.remoteRestartPile(pile,nullptr,&error));
    int empty=0;
    QVERIFY(repository.addPile(1,"P03",QStringLiteral("慢充"),7,0,&empty,&error));
    // Fail last step of a delete: earlier deletes must roll back too.
    QVERIFY(store.execPrepared("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(?,5)",{empty},&error));
    QVERIFY(store.execPrepared("CREATE TRIGGER reject_delete BEFORE DELETE ON piles BEGIN SELECT RAISE(ABORT,'test'); END",{},&error));
    QVERIFY(!repository.deletePile(empty,&error));
    QVERIFY(q.exec("SELECT COUNT(*) FROM pile_power_logs")); QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(),1); q.finish();
    repository.detach(store.connectionName());
}
QTEST_GUILESS_MAIN(AdminRepositoryTest)
#include "tst_admin_repository.moc"
