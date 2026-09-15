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
    QVERIFY(store.open(makeTestDatabasePath(QStringLiteral("socket-order-v3.db")), &error));
    QVERIFY(store.seedDemoIfEmpty(&error));
    const auto target = store.listPiles(store.listStations().first().id).first();
    QVERIFY(store.setPileState(target.id, cp::PileState::Idle, &error));
    MainWindow window(&store);
    cp::NetClient client;
    QList<QJsonObject> replies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16, const QByteArray &body) { replies.append(jsonObjectOf(body)); });
    client.connectToServer(QStringLiteral("127.0.0.1"), cp::kServerPort);
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);
    const auto send = [&](int type, const QJsonObject &request) {
        return client.sendPacket(type, QJsonDocument(request).toJson(QJsonDocument::Compact));
    };
    QVERIFY(send(10, {{"phone", "13800000001"}}));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 0);
    QVERIFY(send(4, {{"request_id", "start"}, {"pile_code", target.code}}));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 2, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 0);
    const int order = replies.last().value("order_id").toInt();
    QVERIFY(order > 0);
    const QJsonObject settlement{{"request_id", "settle"}, {"order_id", order}, {"kwh", 0}};
    QVERIFY(send(2, settlement));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 3, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 0);
    QVERIFY(replies.last().value("received").toBool());
    QCOMPARE(replies.last().value("order_id").toInt(), order);
    QVERIFY(send(2, settlement));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 4, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 0);
    QSqlQuery q(QSqlDatabase::database(store.connectionName()));
    QVERIFY(q.exec(QStringLiteral("SELECT state,charge_count FROM piles WHERE id=%1").arg(target.id)));
    QVERIFY(q.next()); QCOMPARE(q.value(0).toInt(), 0); QCOMPARE(q.value(1).toInt(), 1);
}

void TstSocketBiz::orderReportRejectsUnknownPileAndFaultPile()
{
    QString error;
    StationStore store;
    QVERIFY(store.open(makeTestDatabasePath(QStringLiteral("socket-order-reject.db")), &error));
    QVERIFY(store.seedDemoIfEmpty(&error));
    MainWindow window(&store);
    cp::NetClient client;
    QList<QJsonObject> replies;
    connect(&client, &cp::NetClient::packetReceived, this,
            [&](quint16, const QByteArray &body) { replies.append(jsonObjectOf(body)); });
    client.connectToServer(QStringLiteral("127.0.0.1"), cp::kServerPort);
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);
    QVERIFY(client.sendPacket(2, R"({"request_id":"unauth","order_id":1,"kwh":0})"));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 1, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 401);
    QVERIFY(client.sendPacket(10, R"({"phone":"13800000002"})"));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 2, 5000);
    QVERIFY(client.sendPacket(2, R"({"request_id":"missing","order_id":99999,"kwh":0})"));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 3, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 404);
    QVERIFY(client.sendPacket(30, R"({"request_id":"foreign","phone":"13800000001","amount":10})"));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 4, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 403);
    QVERIFY(client.sendPacket(498, "{}"));
    QTRY_COMPARE_WITH_TIMEOUT(replies.size(), 5, 5000);
    QCOMPARE(replies.last().value("code").toInt(), 400);
}

QTEST_MAIN(TstSocketBiz)

#include "tst_socketbiz.moc"
