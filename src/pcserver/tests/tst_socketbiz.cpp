#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "common.h"
#include "net_client.h"
#include "net_server.h"

#include "../mainwindow.h"
#include "../stationstore.h"
#include "test_dbpath.h"

using namespace pcserver;

namespace {

QJsonObject jsonObjectOf(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return QJsonObject();
    return document.object();
}

} // namespace

/**
 * PcServer Socket 业务协议集成测试（docs/socket-protocol.md 配套）：
 * 启动真实 MainWindow 内置的 cp::NetServer（端口 cp::kServerPort=9999），
 * 用 cp::NetClient 走完整二进制帧链路验证 JSON 应答。
 */
class TstSocketBiz : public QObject
{
    Q_OBJECT

private slots:
    void heartbeatAckRoundTrip();
    void heartbeatInvalidBodyReturns400();
    void stationQueryReturnsExpectedJson();
    void stationQueryByStationIdAndMissing();
    void orderReportFreesChargingPile();
    void orderReportRejectsUnknownPileAndFaultPile();
};

void TstSocketBiz::heartbeatAckRoundTrip()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("socket-hb.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    MainWindow window(&store);
    cp::NetClient client;
    QList<quint16> types;
    QList<QByteArray> bodies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16 msgType, const QByteArray &body) {
                types.append(msgType);
                bodies.append(body);
            });

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           static_cast<quint16>(cp::kServerPort));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

    const QByteArray request =
        QByteArrayLiteral("{\"client_id\":\"tst-pc\",\"ts\":1750000000}");
    QVERIFY(client.sendPacket(
        static_cast<quint16>(cp::MsgType::kHeartbeat), request));

    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 1, 5000);
    QCOMPARE(types.first(), static_cast<quint16>(cp::MsgType::kHeartbeat));

    const QJsonObject response = jsonObjectOf(bodies.first());
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 0);
    QCOMPARE(response.value(QStringLiteral("message")).toString(),
             QStringLiteral("pong"));
    QCOMPARE(response.value(QStringLiteral("client_id")).toString(),
             QStringLiteral("tst-pc"));
    QCOMPARE(response.value(QStringLiteral("ts")).toDouble(), 1750000000.0);
    QVERIFY(!response.value(QStringLiteral("server_time")).toString().isEmpty());
}

void TstSocketBiz::heartbeatInvalidBodyReturns400()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("socket-hb-bad.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    MainWindow window(&store);
    cp::NetClient client;
    QList<quint16> types;
    QList<QByteArray> bodies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16 msgType, const QByteArray &body) {
                types.append(msgType);
                bodies.append(body);
            });

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           static_cast<quint16>(cp::kServerPort));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

    QVERIFY(client.sendPacket(
        static_cast<quint16>(cp::MsgType::kHeartbeat),
        QByteArrayLiteral("not-json")));

    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 1, 5000);
    const QJsonObject response = jsonObjectOf(bodies.first());
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 400);
}

void TstSocketBiz::stationQueryReturnsExpectedJson()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("socket-station.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const QVector<StationInfo> expected = store.listStations();
    QVERIFY(!expected.isEmpty());

    MainWindow window(&store);
    cp::NetClient client;
    QList<quint16> types;
    QList<QByteArray> bodies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16 msgType, const QByteArray &body) {
                types.append(msgType);
                bodies.append(body);
            });

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           static_cast<quint16>(cp::kServerPort));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

    QVERIFY(client.sendPacket(
        static_cast<quint16>(cp::MsgType::kStationQuery),
        QByteArrayLiteral("{\"limit\":200}")));

    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 1, 5000);
    QCOMPARE(types.first(), static_cast<quint16>(cp::MsgType::kStationQuery));

    const QJsonObject response = jsonObjectOf(bodies.first());
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 0);
    QCOMPARE(response.value(QStringLiteral("total")).toInt(-1),
             expected.size());

    const QJsonArray stations =
        response.value(QStringLiteral("stations")).toArray();
    QCOMPARE(stations.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        const QJsonObject item = stations.at(i).toObject();
        QCOMPARE(item.value(QStringLiteral("id")).toInt(-1),
                 expected.at(i).id);
        QCOMPARE(item.value(QStringLiteral("name")).toString(),
                 expected.at(i).name);
        QCOMPARE(item.value(QStringLiteral("total_piles")).toInt(-1),
                 expected.at(i).totalPiles);
        QCOMPARE(item.value(QStringLiteral("idle_piles")).toInt(-1),
                 expected.at(i).idlePiles);
        QVERIFY(item.value(QStringLiteral("price")).toDouble() > 0.0);
        QVERIFY(!item.value(QStringLiteral("server_time")).isDouble()); // 仅根级
        QVERIFY(!item.contains(QStringLiteral("code")));                // 仅根级
    }
    QCOMPARE(stations.at(0).toObject().value(QStringLiteral("name")).toString(),
             expected.first().name);
}

void TstSocketBiz::stationQueryByStationIdAndMissing()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("socket-station-id.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const QVector<StationInfo> expected = store.listStations();
    QVERIFY(!expected.isEmpty());
    const int firstId = expected.first().id;

    MainWindow window(&store);
    cp::NetClient client;
    QList<quint16> types;
    QList<QByteArray> bodies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16 msgType, const QByteArray &body) {
                types.append(msgType);
                bodies.append(body);
            });

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           static_cast<quint16>(cp::kServerPort));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

    const auto queryType = static_cast<quint16>(cp::MsgType::kStationQuery);
    QVERIFY(client.sendPacket(
        queryType,
        QStringLiteral("{\"station_id\":%1}").arg(firstId).toUtf8()));
    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 1, 5000);
    QJsonObject response = jsonObjectOf(bodies.first());
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 0);
    QCOMPARE(response.value(QStringLiteral("total")).toInt(-1), 1);
    QCOMPARE(response.value(QStringLiteral("stations")).toArray().at(0)
                 .toObject()
                 .value(QStringLiteral("id"))
                 .toInt(-1),
             firstId);

    QVERIFY(client.sendPacket(
        queryType, QByteArrayLiteral("{\"station_id\":999999}")));
    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 2, 5000);
    response = jsonObjectOf(bodies.at(1));
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 404);
}

void TstSocketBiz::orderReportFreesChargingPile()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("socket-order.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const QVector<StationInfo> stations = store.listStations();
    QVERIFY(!stations.isEmpty());
    QVector<PileInfo> piles = store.listPiles(stations.first().id);
    QVERIFY(!piles.isEmpty());
    const PileInfo target = piles.first();
    QVERIFY2(store.setPileState(target.id, cp::PileState::Charging, &error),
             qPrintable(error));

    // 业务规则：结算只受理“充电中”订单；按真实联调链路先建单再上报
    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery q(db);
    const QStringList setup = {
        QStringLiteral("INSERT INTO users(id,phone,nickname,balance,status) "
                       "VALUES(1,'13800000001','结算测试用户',100,0)"),
        QStringLiteral("INSERT INTO orders(user_id,pile_id,station_id,"
                       "start_time,state) "
                       "VALUES(1,%1,%2,datetime('now','-30 minutes','localtime'),0)")
            .arg(target.id)
            .arg(target.stationId)
    };
    for (const QString &statement : setup) {
        QVERIFY2(q.exec(statement), q.lastError().text().toUtf8());
    }

    MainWindow window(&store);
    cp::NetClient client;
    QList<quint16> types;
    QList<QByteArray> bodies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16 msgType, const QByteArray &body) {
                types.append(msgType);
                bodies.append(body);
            });

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           static_cast<quint16>(cp::kServerPort));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

    const auto reportType =
        static_cast<quint16>(cp::MsgType::kOrderReport);
    const QString request =
        QStringLiteral("{\"order_no\":\"NO-TEST-001\",\"pile_code\":\"%1\","
                       "\"kwh\":12.5,\"amount\":25.00}")
            .arg(target.code);
    QVERIFY(client.sendPacket(reportType, request.toUtf8()));

    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 1, 5000);
    QCOMPARE(types.first(), reportType);
    const QJsonObject response = jsonObjectOf(bodies.first());
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 0);
    QCOMPARE(response.value(QStringLiteral("order_no")).toString(),
             QStringLiteral("NO-TEST-001"));
    QCOMPARE(response.value(QStringLiteral("pile_code")).toString(),
             target.code);
    QCOMPARE(response.value(QStringLiteral("pile_id")).toInt(-1), target.id);
    QCOMPARE(response.value(QStringLiteral("station_id")).toInt(-1),
             target.stationId);
    QCOMPARE(response.value(QStringLiteral("received")).toBool(), true);
    QCOMPARE(response.value(QStringLiteral("pile_freed")).toBool(), true);

    piles = store.listPiles(stations.first().id);
    for (const PileInfo &pile : piles) {
        if (pile.id == target.id)
            QCOMPARE(pile.state, cp::PileState::Idle);
    }
}

void TstSocketBiz::orderReportRejectsUnknownPileAndFaultPile()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("socket-order-bad.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    // seedDemoIfEmpty 的固定分布含故障桩（约 1/10）
    PileInfo faultPile;
    bool foundFault = false;
    for (const StationInfo &station : store.listStations()) {
        const QVector<PileInfo> piles = store.listPiles(station.id);
        for (const PileInfo &pile : piles) {
            if (pile.state == cp::PileState::Fault) {
                faultPile = pile;
                foundFault = true;
                break;
            }
        }
        if (foundFault)
            break;
    }
    QVERIFY2(foundFault, "演示数据应包含故障桩");

    MainWindow window(&store);
    cp::NetClient client;
    QList<quint16> types;
    QList<QByteArray> bodies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16 msgType, const QByteArray &body) {
                types.append(msgType);
                bodies.append(body);
            });

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           static_cast<quint16>(cp::kServerPort));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);

    const auto reportType =
        static_cast<quint16>(cp::MsgType::kOrderReport);
    QVERIFY(client.sendPacket(
        reportType,
        QByteArrayLiteral("{\"order_no\":\"NO-404\",\"pile_code\":\"NOPE-99\","
                          "\"kwh\":1.0,\"amount\":2.0}")));
    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 1, 5000);
    QJsonObject response = jsonObjectOf(bodies.first());
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 404);
    QCOMPARE(response.value(QStringLiteral("received")).toBool(), false);

    QVERIFY(client.sendPacket(
        reportType,
        QStringLiteral("{\"order_no\":\"NO-409\",\"pile_code\":\"%1\","
                       "\"kwh\":3.0,\"amount\":6.0}")
            .arg(faultPile.code)
            .toUtf8()));
    QTRY_COMPARE_WITH_TIMEOUT(bodies.size(), 2, 5000);
    response = jsonObjectOf(bodies.at(1));
    QCOMPARE(response.value(QStringLiteral("code")).toInt(-1), 409);
    QCOMPARE(response.value(QStringLiteral("received")).toBool(), false);
}

QTEST_MAIN(TstSocketBiz)

#include "tst_socketbiz.moc"
