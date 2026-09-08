#include <QtTest/QtTest>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "../stationstore.h"
#include "test_dbpath.h"

using namespace pcserver;

namespace {

bool execOk(QSqlQuery &q, const QString &sql)
{
    return q.exec(sql);
}

QStringList indexNames(QSqlDatabase db, const QString &table)
{
    QStringList names;
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA index_list(%1)").arg(table));
    while (q.next())
        names << q.value(1).toString();
    return names;
}

QStringList indexColumns(QSqlDatabase db, const QString &index)
{
    QStringList columns;
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA index_info(%1)").arg(index));
    while (q.next())
        columns << q.value(2).toString();
    return columns;
}

} // namespace

class TstSchema : public QObject
{
    Q_OBJECT

private slots:
    void coreTablesExistWithPrimaryKey();
    void coreColumnsAndTypes();
    void foreignKeysAreDeclared();
    void uniqueConstraintsRejectDuplicates();
    void checkConstraintsRejectInvalidValues();
    void orderPileStationConsistencyTrigger();
    void deleteStationCascadesToPiles();
    void performanceIndexesExist();
    void explainPlanUsesStationStartIndex();
};

void TstSchema::coreTablesExistWithPrimaryKey()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_core.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery tables(db);
    QVERIFY(tables.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")));
    QStringList names;
    while (tables.next())
        names << tables.value(0).toString();

    const QStringList core = {
        QStringLiteral("admins"), QStringLiteral("users"),
        QStringLiteral("stations"), QStringLiteral("piles"), QStringLiteral("orders")
    };
    for (const QString &table : core)
        QVERIFY2(names.contains(table), qPrintable(table + QStringLiteral(" 表缺失")));

    // 每张核心表都以自增 INTEGER 主键 id 开头
    for (const QString &table : core) {
        QSqlQuery pk(db);
        pk.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
        QVERIFY2(pk.next(), qPrintable(table));
        QCOMPARE(pk.value(0).toInt(), 0);   // cid
        QCOMPARE(pk.value(1).toString(), QStringLiteral("id"));
        QCOMPARE(pk.value(5).toInt(), 1);   // pk 标记
    }
}

void TstSchema::coreColumnsAndTypes()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_types.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());

    struct Expect { QString table; QString column; QString type; bool notNull; };
    const QVector<Expect> expects = {
        { QStringLiteral("admins"),   QStringLiteral("username"),    QStringLiteral("TEXT"),    true  },
        { QStringLiteral("admins"),   QStringLiteral("password"),    QStringLiteral("TEXT"),    true  },
        { QStringLiteral("users"),    QStringLiteral("phone"),       QStringLiteral("TEXT"),    true  },
        { QStringLiteral("users"),    QStringLiteral("balance"),     QStringLiteral("REAL"),    true  },
        { QStringLiteral("users"),    QStringLiteral("status"),      QStringLiteral("INTEGER"), true  },
        { QStringLiteral("stations"), QStringLiteral("longitude"),   QStringLiteral("REAL"),    true  },
        { QStringLiteral("stations"), QStringLiteral("latitude"),    QStringLiteral("REAL"),    true  },
        { QStringLiteral("stations"), QStringLiteral("online_rate"), QStringLiteral("REAL"),    true  },
        { QStringLiteral("piles"),    QStringLiteral("station_id"),  QStringLiteral("INTEGER"), true  },
        { QStringLiteral("piles"),    QStringLiteral("code"),        QStringLiteral("TEXT"),    true  },
        { QStringLiteral("piles"),    QStringLiteral("state"),       QStringLiteral("INTEGER"), true  },
        { QStringLiteral("orders"),   QStringLiteral("user_id"),     QStringLiteral("INTEGER"), true  },
        { QStringLiteral("orders"),   QStringLiteral("amount"),      QStringLiteral("REAL"),    true  },
        { QStringLiteral("orders"),   QStringLiteral("state"),       QStringLiteral("INTEGER"), true  },
    };

    for (const Expect &e : expects) {
        QSqlQuery column(db);
        column.exec(QStringLiteral("PRAGMA table_info(%1)").arg(e.table));
        bool found = false;
        while (column.next()) {
            if (column.value(1).toString() != e.column)
                continue;
            found = true;
            QCOMPARE(column.value(2).toString(), e.type);
            QCOMPARE(column.value(3).toInt(), e.notNull ? 1 : 0);
        }
        QVERIFY2(found, qPrintable(QStringLiteral("%1.%2 列缺失")
                                        .arg(e.table, e.column)));
    }
}

void TstSchema::foreignKeysAreDeclared()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_fk.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());

    QSqlQuery pileFk(db);
    QVERIFY(pileFk.exec(QStringLiteral("PRAGMA foreign_key_list(piles)")));
    bool foundPileStation = false;
    while (pileFk.next()) {
        if (pileFk.value(2).toString() == QStringLiteral("stations")
            && pileFk.value(3).toString() == QStringLiteral("station_id")
            && pileFk.value(4).toString() == QStringLiteral("id")) {
            foundPileStation = true;
            QCOMPARE(pileFk.value(6).toString(), QStringLiteral("CASCADE"));   // on_delete
        }
    }
    QVERIFY2(foundPileStation, "piles.station_id -> stations.id 外键缺失");

    QSqlQuery orderFk(db);
    QVERIFY(orderFk.exec(QStringLiteral("PRAGMA foreign_key_list(orders)")));
    QStringList orderTables;
    while (orderFk.next())
        orderTables << orderFk.value(2).toString() + QLatin1Char(':')
                           + orderFk.value(3).toString();
    QVERIFY(orderTables.contains(QStringLiteral("users:user_id")));
    QVERIFY(orderTables.contains(QStringLiteral("piles:pile_id")));
    QVERIFY(orderTables.contains(QStringLiteral("stations:station_id")));
}

void TstSchema::uniqueConstraintsRejectDuplicates()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_unique.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());

    QSqlQuery q(db);
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO admins(username, password) VALUES ('admin', '123456')")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO admins(username, password) VALUES ('admin', '654321')")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO admins(username, password) VALUES ('ADMIN', '654321')"))); // NOCASE 唯一

    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO users(phone, nickname) VALUES ('13800138000', '用户A')")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO users(phone, nickname) VALUES ('13800138000', '用户B')")));

    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude, total_piles, online_rate) "
        "VALUES ('站A', '地址A', 123.0, 41.0, 2, 100.0)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P01', '快充', 120.0, 0)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P01', '慢充', 7.0, 0)")));

    QSqlQuery count(db);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM admins")) && count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TstSchema::checkConstraintsRejectInvalidValues()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_check.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery q(db);

    // users：手机号位数/纯数字、余额非负、状态枚举
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO users(phone) VALUES ('1380013800')")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO users(phone) VALUES ('1380013800a')")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO users(phone, balance) VALUES ('13800138000', -1)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO users(phone, status) VALUES ('13800138000', 2)")));

    // stations：经纬度/总桩数/在线率
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude) "
        "VALUES ('站', '地址', 181, 0)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude, online_rate) "
        "VALUES ('站A', '地址A', 123, 41, 101)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude, total_piles) "
        "VALUES ('站A', '地址A', 123, 41, -1)")));

    // piles：类型/功率/状态
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude) "
        "VALUES ('站B', '地址B', 123, 41)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P01', '直流', 120, 0)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P02', '快充', 0, 0)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P03', '快充', 120, 9)")));

    // orders：需要合法的 user/pile/station；金额与状态约束
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO users(phone) VALUES ('13900139000')")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P04', '快充', 120, 0)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO orders(user_id, pile_id, station_id, amount, state) "
        "VALUES (1, 1, 1, -0.01, 0)")));
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO orders(user_id, pile_id, station_id, amount, state) "
        "VALUES (1, 1, 1, 0, 3)")));
}

void TstSchema::orderPileStationConsistencyTrigger()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_trigger.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery q(db);

    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude) VALUES ('站1', '地址1', 123, 41)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude) VALUES ('站2', '地址2', 124, 42)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P01', '快充', 120, 0)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (2, 'S002-P01', '快充', 120, 0)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO users(phone) VALUES ('13700137000')")));

    // 桩属于站1，订单却记到站2 -> 拒绝
    QVERIFY(!execOk(q, QStringLiteral(
        "INSERT INTO orders(user_id, pile_id, station_id) VALUES (1, 1, 2)")));

    // 正确归属 -> 通过
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO orders(user_id, pile_id, station_id) VALUES (1, 1, 1)")));

    // 把订单电桩改成站2的桩但电站仍为站1 -> 拒绝
    QVERIFY(!execOk(q, QStringLiteral(
        "UPDATE orders SET pile_id = 2 WHERE id = 1")));
    // 把订单电站改成站2但电桩仍为站1的桩 -> 拒绝
    QVERIFY(!execOk(q, QStringLiteral(
        "UPDATE orders SET station_id = 2 WHERE id = 1")));
    // 同时把电桩与电站都改成站2 -> 通过
    QVERIFY(execOk(q, QStringLiteral(
        "UPDATE orders SET pile_id = 2, station_id = 2 WHERE id = 1")));
}

void TstSchema::deleteStationCascadesToPiles()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_cascade.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery q(db);

    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO stations(name, address, longitude, latitude, total_piles) "
        "VALUES ('级联站', '地址', 123, 41, 2)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P01', '快充', 120, 0)")));
    QVERIFY(execOk(q, QStringLiteral(
        "INSERT INTO piles(station_id, code, type, power_kw, state) "
        "VALUES (1, 'S001-P02', '慢充', 7, 1)")));
    QVERIFY(execOk(q, QStringLiteral("DELETE FROM stations WHERE id = 1")));

    QSqlQuery count(db);
    QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM piles")) && count.next());
    QCOMPARE(count.value(0).toInt(), 0);
}

void TstSchema::performanceIndexesExist()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_index.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());

    for (const QString &table : { QStringLiteral("piles"), QStringLiteral("orders"),
                                  QStringLiteral("users"), QStringLiteral("stations") }) {
        QSqlQuery q(db);
        QVERIFY2(q.exec(QStringLiteral("PRAGMA index_list(%1)").arg(table)),
                 qPrintable(table));
    }

    QStringList pileIndexes = indexNames(db, QStringLiteral("piles"));
    QVERIFY(pileIndexes.contains(QStringLiteral("idx_piles_station")));
    QVERIFY(pileIndexes.contains(QStringLiteral("idx_piles_state")));
    QCOMPARE(indexColumns(db, QStringLiteral("idx_piles_station")),
             QStringList{ QStringLiteral("station_id") });
    QCOMPARE(indexColumns(db, QStringLiteral("idx_piles_state")),
             QStringList{ QStringLiteral("state") });

    QStringList orderIndexes = indexNames(db, QStringLiteral("orders"));
    QVERIFY(orderIndexes.contains(QStringLiteral("idx_orders_user")));
    QVERIFY(orderIndexes.contains(QStringLiteral("idx_orders_pile")));
    QVERIFY(orderIndexes.contains(QStringLiteral("idx_orders_station_start")));
    QVERIFY(orderIndexes.contains(QStringLiteral("idx_orders_state")));
    QCOMPARE(indexColumns(db, QStringLiteral("idx_orders_station_start")),
             (QStringList{ QStringLiteral("station_id"), QStringLiteral("start_time") }));

    QStringList stationIndexes = indexNames(db, QStringLiteral("stations"));
    QVERIFY(stationIndexes.contains(QStringLiteral("idx_stations_geo")));
    QCOMPARE(indexColumns(db, QStringLiteral("idx_stations_geo")),
             (QStringList{ QStringLiteral("latitude"), QStringLiteral("longitude") }));

    QStringList userIndexes = indexNames(db, QStringLiteral("users"));
    QVERIFY(userIndexes.contains(QStringLiteral("idx_users_status")));
}

void TstSchema::explainPlanUsesStationStartIndex()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("schema_plan.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error),
             qPrintable(error));
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());

    // 空表也走索引（INDEXED BY 强制），用于验证索引列设计可命中常用销售查询
    QSqlQuery run(db);
    const QString sql = QStringLiteral(
        "SELECT id FROM orders INDEXED BY idx_orders_station_start "
        "WHERE station_id = 1 AND start_time >= '2026-01-01' "
        "ORDER BY start_time LIMIT 10");
    QVERIFY2(execOk(run, sql), qPrintable(run.lastError().text()));

    QSqlQuery plan(db);
    QVERIFY(plan.exec(QStringLiteral("EXPLAIN QUERY PLAN ") + sql));
    QString detail;
    while (plan.next())
        detail += plan.value(3).toString();
    QVERIFY2(detail.contains(QStringLiteral("idx_orders_station_start")),
             qPrintable(detail));
}

QTEST_MAIN(TstSchema)

#include "tst_schema.moc"
