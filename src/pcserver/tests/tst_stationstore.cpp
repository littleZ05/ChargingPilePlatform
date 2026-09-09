#include <QtTest/QtTest>

#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "../stationstore.h"
#include "test_dbpath.h"

using namespace pcserver;

namespace {

StationInfo stationById(const QVector<StationInfo> &stations, int id)
{
    for (const StationInfo &s : stations) {
        if (s.id == id)
            return s;
    }
    return StationInfo();
}

PileInfo pileById(const QVector<PileInfo> &piles, int id)
{
    for (const PileInfo &p : piles) {
        if (p.id == id)
            return p;
    }
    return PileInfo();
}

} // namespace

class TstStationStore : public QObject
{
    Q_OBJECT

private slots:
    void schemaCreatesContractTables();
    void addStationCreatesStationAndSimulatedPiles();
    void listStationsReportsOnlineRate();
    void listStationsReportsIdleCountAndCurrentPrice();
    void findPileByCode();
    void invalidInputIsRejected();
    void setPileStateRefreshesOnlineRate();
    void seedDemoIfEmpty_data();
    void seedDemoIfEmpty();
    void pileStateTextMapping();
    void nextSimulatedStateCyclesStates();
    void runInTransactionRollsBackOnFailure();
    void integrityCheckAndBackup();
    void preparedStatementPreventsSqlInjection();
    void settleChargingOrderCompletesAndDeducts();
    void userLoginAutoRegistersAndReturnsExisting();
};

void TstStationStore::schemaCreatesContractTables()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("contract.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QVERIFY(db.isOpen());

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT name FROM sqlite_master "
        "WHERE type='table' AND name IN ('stations', 'piles')")));

    QSet<QString> tables;
    while (query.next())
        tables.insert(query.value(0).toString());
    QVERIFY2(tables.contains(QStringLiteral("stations")), "缺少 stations 表");
    QVERIFY2(tables.contains(QStringLiteral("piles")), "缺少 piles 表");

    // 公共契约：stations.online_rate 与 piles.state 字段必须存在
    QSqlQuery columnQuery(db);
    QVERIFY(columnQuery.exec(QStringLiteral("PRAGMA table_info(stations)")));
    QSet<QString> stationColumns;
    while (columnQuery.next())
        stationColumns.insert(columnQuery.value(1).toString());
    QVERIFY2(stationColumns.contains(QStringLiteral("online_rate")), "stations 缺少 online_rate");
}

void TstStationStore::addStationCreatesStationAndSimulatedPiles()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("add.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    const int stationId = store.addStation(
        QStringLiteral(" 测试站 A "), QStringLiteral(" 沈阳市测试路 1 号 "),
        123.456789, 41.123456, 4, &error);
    QVERIFY2(stationId > 0, qPrintable(error));

    const auto stations = store.listStations();
    QCOMPARE(stations.size(), 1);
    const StationInfo info = stationById(stations, stationId);
    QCOMPARE(info.name, QStringLiteral("测试站 A"));      // 站名已去除首尾空格
    QCOMPARE(info.address, QStringLiteral("沈阳市测试路 1 号"));
    QCOMPARE(info.totalPiles, 4);
    QCOMPARE(info.onlinePiles, 4);

    const auto piles = store.listPiles(stationId);
    QCOMPARE(piles.size(), 4);
    for (int i = 0; i < piles.size(); ++i) {
        QCOMPARE(piles.at(i).stationId, stationId);
        const QString expectCode =
            QStringLiteral("S%1-P%2")
                .arg(stationId, 3, 10, QLatin1Char('0'))
                .arg(i + 1, 2, 10, QLatin1Char('0'));
        QCOMPARE(piles.at(i).code, expectCode);
        QVERIFY(piles.at(i).type == QStringLiteral("快充")
                || piles.at(i).type == QStringLiteral("慢充"));
        QVERIFY(piles.at(i).powerKw > 0.0);
    }

}

void TstStationStore::listStationsReportsOnlineRate()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("rate.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    // 10 根桩的初始固定分布：1 根故障、3 根充电中、6 根闲置 => 在线率 90%
    const int stationId = store.addStation(
        QStringLiteral("在线率测试站"), QStringLiteral("沈阳市测试路 2 号"),
        123.100000, 41.700000, 10, &error);
    QVERIFY2(stationId > 0, qPrintable(error));

    const StationInfo info = stationById(store.listStations(), stationId);
    QCOMPARE(info.totalPiles, 10);
    QCOMPARE(info.onlinePiles, 9);
    QVERIFY2(qAbs(info.onlineRate - 90.0) < 1e-6, qPrintable(QString::number(info.onlineRate)));

}

void TstStationStore::listStationsReportsIdleCountAndCurrentPrice()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("price.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    // 10 根桩固定分布：i=9 故障、i=1/4/7 充电中、其余 6 根闲置
    const int stationId = store.addStation(
        QStringLiteral("价格测试站"), QStringLiteral("沈阳市测试路 4 号"),
        123.300000, 41.900000, 10, &error);
    QVERIFY2(stationId > 0, qPrintable(error));

    StationInfo info = stationById(store.listStations(), stationId);
    QCOMPARE(info.totalPiles, 10);
    QCOMPARE(info.idlePiles, 6);
    QCOMPARE(info.onlinePiles, 9);
    QCOMPARE(info.basePrice, 1.0);
    QVERIFY(qAbs(info.currentPrice - 1.0) < 1e-9); // 无营销策略=基础价

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT INTO marketing_strategy(station_id, base_price, discount, "
        "rule_desc, is_active) VALUES(?, ?, ?, ?, 1)"));
    insert.addBindValue(stationId);
    insert.addBindValue(1.0);
    insert.addBindValue(0.8);
    insert.addBindValue(QStringLiteral("测试：生效 8 折"));
    QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));

    info = stationById(store.listStations(), stationId);
    QVERIFY(qAbs(info.currentPrice - 0.8) < 1e-9); // 基础价 × 0.8

    QSqlQuery deactivate(db);
    deactivate.prepare(QStringLiteral(
        "UPDATE marketing_strategy SET is_active = 0 WHERE station_id = ?"));
    deactivate.addBindValue(stationId);
    QVERIFY(deactivate.exec());

    info = stationById(store.listStations(), stationId);
    QVERIFY(qAbs(info.currentPrice - 1.0) < 1e-9); // 停用后恢复基础价
}

void TstStationStore::findPileByCode()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("pilecode.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    const int stationId = store.addStation(
        QStringLiteral("查桩测试站"), QStringLiteral("沈阳市测试路 5 号"),
        123.400000, 41.500000, 4, &error);
    QVERIFY2(stationId > 0, qPrintable(error));

    const auto piles = store.listPiles(stationId);
    QCOMPARE(piles.size(), 4);

    PileInfo found;
    QVERIFY(store.findPileByCode(piles.at(2).code, &found));
    QCOMPARE(found.id, piles.at(2).id);
    QCOMPARE(found.stationId, stationId);
    QCOMPARE(found.code, piles.at(2).code);
    QCOMPARE(found.type, piles.at(2).type);
    QCOMPARE(found.state, piles.at(2).state);

    // 编码唯一精确匹配，编码首尾空白容忍
    QVERIFY(store.findPileByCode(
        QStringLiteral("  %1  ").arg(piles.first().code)));
    QVERIFY(!store.findPileByCode(QStringLiteral("NO-SUCH-PILE")));
}

void TstStationStore::invalidInputIsRejected()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("invalid.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    QVERIFY(!store.validateInput(QString(), QStringLiteral("地址"), 0, 0, 5, &error));
    QVERIFY(!store.validateInput(QStringLiteral("站名"), QString(), 0, 0, 5, &error));
    QVERIFY(!store.validateInput(QStringLiteral("站名"), QStringLiteral("地址"), 181, 0, 5, &error));
    QVERIFY(!store.validateInput(QStringLiteral("站名"), QStringLiteral("地址"), 0, -91, 5, &error));
    QVERIFY(!store.validateInput(QStringLiteral("站名"), QStringLiteral("地址"), 0, 0, 0, &error));
    QVERIFY(!store.validateInput(QStringLiteral("站名"), QStringLiteral("地址"), 0, 0, 101, &error));

    const int badId = store.addStation(QString(), QStringLiteral("地址"), 0, 0, 5, &error);
    QCOMPARE(badId, -1);
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.listStations().size(), 0);

    // 边界值应被接受：经度 ±180、纬度 ±90、桩数 1/100
    QVERIFY(store.validateInput(QStringLiteral("站名"), QStringLiteral("地址"),
                                -180.0, -90.0, 1, &error));
    QVERIFY(store.validateInput(QStringLiteral("站名"), QStringLiteral("地址"),
                                180.0, 90.0, 100, &error));

}

void TstStationStore::setPileStateRefreshesOnlineRate()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("state.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    const int stationId = store.addStation(
        QStringLiteral("状态测试站"), QStringLiteral("沈阳市测试路 3 号"),
        123.200000, 41.800000, 10, &error);
    QVERIFY2(stationId > 0, qPrintable(error));

    // 第 1 根桩（下标 0）初始为闲置；改为故障后在线率 90% -> 80%
    const auto piles = store.listPiles(stationId);
    const int firstPileId = piles.at(0).id;
    QVERIFY2(store.setPileState(firstPileId, cp::PileState::Fault, &error),
             qPrintable(error));

    QCOMPARE(pileById(store.listPiles(stationId), firstPileId).state, cp::PileState::Fault);
    const StationInfo after = stationById(store.listStations(), stationId);
    QCOMPARE(after.onlinePiles, 8);
    QVERIFY(qAbs(after.onlineRate - 80.0) < 1e-6);

    // 恢复闲置后在线率回到 90%
    QVERIFY2(store.setPileState(firstPileId, cp::PileState::Idle, &error),
             qPrintable(error));
    const StationInfo recovered = stationById(store.listStations(), stationId);
    QVERIFY(qAbs(recovered.onlineRate - 90.0) < 1e-6);

    // 数据库中 stations.online_rate 也应同步更新（字段一致性）
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery check(db);
    check.prepare(QStringLiteral("SELECT online_rate FROM stations WHERE id = ?"));
    check.addBindValue(stationId);
    QVERIFY(check.exec() && check.next());
    QVERIFY(qAbs(check.value(0).toDouble() - 90.0) < 1e-6);
}

void TstStationStore::seedDemoIfEmpty_data()
{
    QTest::addColumn<bool>("secondCall");
    QTest::newRow("调用一次") << false;
    QTest::newRow("重复调用不应重复插入") << true;
}

void TstStationStore::seedDemoIfEmpty()
{
    QFETCH(bool, secondCall);

    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("seed.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));
    if (secondCall)
        QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const auto stations = store.listStations();
    QCOMPARE(stations.size(), 3);
    int totalPiles = 0;
    for (const StationInfo &s : stations) {
        QVERIFY(s.totalPiles > 0);
        totalPiles += s.totalPiles;
        QCOMPARE(store.listPiles(s.id).size(), s.totalPiles);
        QVERIFY(s.onlinePiles >= 0 && s.onlinePiles <= s.totalPiles);
    }
    QCOMPARE(totalPiles, 28); // 10 + 12 + 6
}

void TstStationStore::pileStateTextMapping()
{
    QCOMPARE(StationStore::pileStateText(cp::PileState::Idle), QStringLiteral("闲置"));
    QCOMPARE(StationStore::pileStateText(cp::PileState::Charging), QStringLiteral("充电中"));
    QCOMPARE(StationStore::pileStateText(cp::PileState::Fault), QStringLiteral("故障"));
}

void TstStationStore::nextSimulatedStateCyclesStates()
{
    QCOMPARE(StationStore::nextSimulatedState(cp::PileState::Idle),
             cp::PileState::Charging);
    QCOMPARE(StationStore::nextSimulatedState(cp::PileState::Charging),
             cp::PileState::Idle);
    QCOMPARE(StationStore::nextSimulatedState(cp::PileState::Fault),
             cp::PileState::Idle);
}

void TstStationStore::runInTransactionRollsBackOnFailure()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("txn.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    bool executed = false;
    QVERIFY(!store.runInTransaction(
        [&](QSqlDatabase &db) {
            QSqlQuery insert(db);
            executed = insert.exec(QStringLiteral(
                "INSERT INTO stations(name, address, longitude, latitude) "
                "VALUES ('事务回滚站', '沈阳市事务路 1 号', 123.0, 41.0)"));
            return false; // 故意失败触发回滚
        },
        &error));
    QVERIFY2(executed, "事务回调未执行");
    QVERIFY(error.contains(QStringLiteral("回滚")));

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery count(db);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM stations")) && count.next());
    QCOMPARE(count.value(0).toInt(), 0);
}

void TstStationStore::integrityCheckAndBackup()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("backup_src.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    QString report;
    QVERIFY2(store.integrityCheck(&report), qPrintable(report));
    QCOMPARE(report, QStringLiteral("ok"));

    const QString backupPath = makeTestDatabasePath(QStringLiteral("backup_dst.db"));
    QVERIFY2(!backupPath.isEmpty(), "无法创建备份文件目录");
    QVERIFY2(store.backupTo(backupPath, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(backupPath));

    StationStore backupStore;
    QVERIFY2(backupStore.open(backupPath, &error), qPrintable(error));
    const auto stations = backupStore.listStations();
    QCOMPARE(stations.size(), 3);
    QCOMPARE(backupStore.listPiles(stations.first().id).size(),
             stations.first().totalPiles);

    // 危险路径必须拒绝，不允许 SQL 边界注入
    QVERIFY(!store.backupTo(QStringLiteral("/tmp/evil'name.db"), &error));
    QVERIFY(error.contains(QStringLiteral("单引号")));
}

void TstStationStore::preparedStatementPreventsSqlInjection()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("inject.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    const QString evil = QStringLiteral("x'); DROP TABLE stations; --");
    const QVariantList binds = {
        evil, QStringLiteral("注入测试地址"), 123.456, 41.789, 0, 0.0
    };
    QVERIFY2(store.execPrepared(
                 QStringLiteral(
                     "INSERT INTO stations(name, address, longitude, latitude, "
                     "total_piles, online_rate) VALUES(?, ?, ?, ?, ?, ?)"),
                 binds, &error),
             qPrintable(error));

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery tableCheck(db);
    QVERIFY(tableCheck.exec(QStringLiteral(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='stations'")));
    QVERIFY(tableCheck.next());
    QCOMPARE(tableCheck.value(0).toInt(), 1); // 表未被注入字符串删除/破坏

    const auto stations = store.listStations();
    QCOMPARE(stations.size(), 1);
    QCOMPARE(stations.first().name, evil); // 注入串只作为普通数据保存
}

void TstStationStore::settleChargingOrderCompletesAndDeducts()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("settle.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    // 造数：用户100元、电站、桩(充电中)、充电中订单
    QSqlQuery q(db);
    const QStringList setup = {
        QStringLiteral("INSERT INTO users(id,phone,nickname,balance,status) "
                       "VALUES(1,'13800000001','测试用户',100,0)"),
        QStringLiteral("INSERT INTO stations(id,name,address) VALUES(1,'测试站','地址')"),
        QStringLiteral("INSERT INTO piles(id,station_id,code,power_kw,state) "
                       "VALUES(1,1,'T-P01',60,1)"),
        QStringLiteral("INSERT INTO orders(id,user_id,pile_id,station_id,start_time,state) "
                       "VALUES(1,1,1,1,datetime('now','-30 minutes','localtime'),0)")
    };
    for (const QString &s : setup)
        QVERIFY2(q.exec(s), q.lastError().text().toUtf8());

    int orderId = -1;
    double balance = -1.0;
    QVERIFY2(store.settleChargingOrderByCode(
                 QStringLiteral("T-P01"), 20.0, 24.0, &orderId, &balance, &error),
             qPrintable(error));
    QCOMPARE(orderId, 1);
    QCOMPARE(balance, 76.0);   // 100 - 24

    QSqlQuery check(db);
    QVERIFY(check.exec(QStringLiteral(
        "SELECT o.state,o.kwh,o.amount,p.state,p.charge_count FROM orders o "
        "JOIN piles p ON p.id=o.pile_id WHERE o.id=1")));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toInt(), 1);          // 订单已完成
    QVERIFY(qAbs(check.value(1).toDouble() - 20.0) < 1e-6);
    QVERIFY(qAbs(check.value(2).toDouble() - 24.0) < 1e-6);
    QCOMPARE(check.value(3).toInt(), 0);          // 桩已释放
    QCOMPARE(check.value(4).toInt(), 1);          // 次数 +1

    // 再次对同一桩结算应失败（无充电中订单）
    error.clear();
    QVERIFY(!store.settleChargingOrderByCode(
                QStringLiteral("T-P01"), 1.0, 1.0, nullptr, nullptr, &error));
    QVERIFY2(error.contains(QStringLiteral("无充电中订单")), qPrintable(error));
}

void TstStationStore::userLoginAutoRegistersAndReturnsExisting()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("login.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    int userId = 0; QString nickname; double balance = -1; int status = -1;
    bool created = false;
    QVERIFY2(store.userLoginByPhone(QStringLiteral("13900009999"), &userId,
                                    &nickname, &balance, &status, &created, &error),
             qPrintable(error));
    QVERIFY(created);
    QCOMPARE(nickname, QStringLiteral("用户9999"));
    QCOMPARE(balance, 0.0);
    QCOMPARE(status, 0);
    const int firstId = userId;

    created = false;
    QVERIFY(store.userLoginByPhone(QStringLiteral("13900009999"), &userId,
                                   &nickname, &balance, &status, &created, &error));
    QVERIFY(!created);
    QCOMPARE(userId, firstId);
}

QTEST_MAIN(TstStationStore)

#include "tst_stationstore.moc"
