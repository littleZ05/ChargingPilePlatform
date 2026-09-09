#include <QtTest/QtTest>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>

#include "common.h"
#include "net_client.h"
#include "net_server.h"
#include "pcserver_session.h"

namespace {

quint16 reserveFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return static_cast<quint16>(cp::kServerPort);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

bool parseJsonObject(const QByteArray &body, QJsonObject *out)
{
    if (!out)
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *out = document.object();
    return true;
}

QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

/**
 * 测试用“伪 PcServer”：cp::NetServer 监听临时端口，按 docs/socket-protocol.md
 * 应答心跳 ACK 与固定电站列表，可模拟不回心跳/服务端重启/错误码应答。
 */
class FakePcServer : public QObject
{
    Q_OBJECT
public:
    explicit FakePcServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &cp::NetServer::clientConnected, this,
                [this](QTcpSocket *client) { m_client = client; });
        connect(&m_server, &cp::NetServer::packetReceived, this,
                [this](QTcpSocket *client, quint16 msgType,
                       const QByteArray &body) {
                    handlePacket(client, msgType, body);
                });
    }

    bool start(quint16 port) { return m_server.startServer(port); }
    void stop()
    {
        m_server.stopServer();
        m_client = nullptr;
    }

    int heartbeatCount() const { return m_heartbeatCount; }
    QByteArray lastHeartbeatBody() const { return m_lastHeartbeatBody; }
    QByteArray lastStationRequestBody() const { return m_lastStationRequestBody; }

    void setReplyHeartbeats(bool enabled) { m_replyHeartbeats = enabled; }

    void setStationError(int code, const QString &message)
    {
        m_stationError = true;
        m_stationErrorCode = code;
        m_stationErrorMessage = message;
    }

    void clearStationError()
    {
        m_stationError = false;
        m_stationErrorCode = 0;
        m_stationErrorMessage.clear();
    }

private:
    void handlePacket(QTcpSocket *client, quint16 msgType,
                      const QByteArray &body)
    {
        if (msgType == static_cast<quint16>(cp::MsgType::kHeartbeat)) {
            ++m_heartbeatCount;
            m_lastHeartbeatBody = body;
            if (m_replyHeartbeats)
                sendHeartbeatAck(client, body);
            return;
        }
        if (msgType == static_cast<quint16>(cp::MsgType::kStationQuery)) {
            m_lastStationRequestBody = body;
            sendStationResponse(client);
            return;
        }
    }

    void sendHeartbeatAck(QTcpSocket *client, const QByteArray &body)
    {
        QJsonObject request;
        if (!parseJsonObject(body, &request)) {
            QJsonObject response;
            response.insert(QStringLiteral("code"), 400);
            response.insert(QStringLiteral("message"),
                            QStringLiteral("心跳负载必须是 JSON 对象"));
            response.insert(QStringLiteral("server_time"),
                            QDateTime::currentDateTime().toString(
                                QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            m_server.sendPacket(
                client, static_cast<quint16>(cp::MsgType::kHeartbeat),
                compactJson(response));
            return;
        }

        QJsonObject response;
        response.insert(QStringLiteral("code"), 0);
        response.insert(QStringLiteral("message"), QStringLiteral("pong"));
        response.insert(QStringLiteral("server_time"),
                        QDateTime::currentDateTime().toString(
                            QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        const QJsonValue clientId =
            request.value(QStringLiteral("client_id"));
        if (clientId.isString())
            response.insert(QStringLiteral("client_id"),
                            clientId.toString());
        const QJsonValue ts = request.value(QStringLiteral("ts"));
        if (ts.isDouble())
            response.insert(QStringLiteral("ts"), ts.toDouble());
        m_server.sendPacket(
            client, static_cast<quint16>(cp::MsgType::kHeartbeat),
            compactJson(response));
    }

    void sendStationResponse(QTcpSocket *client)
    {
        QJsonObject root;
        if (m_stationError) {
            root.insert(QStringLiteral("code"), m_stationErrorCode);
            root.insert(QStringLiteral("message"), m_stationErrorMessage);
            root.insert(QStringLiteral("server_time"),
                        QDateTime::currentDateTime().toString(
                            QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            root.insert(QStringLiteral("total"), 0);
            root.insert(QStringLiteral("stations"), QJsonArray());
        } else {
            QJsonArray stations;
            QJsonObject first;
            first.insert(QStringLiteral("id"), 11);
            first.insert(QStringLiteral("name"),
                         QStringLiteral("东软软件园充电站"));
            first.insert(QStringLiteral("address"),
                         QStringLiteral("沈阳市浑南区智慧二街100号"));
            first.insert(QStringLiteral("longitude"), 123.4958);
            first.insert(QStringLiteral("latitude"), 41.7152);
            first.insert(QStringLiteral("total_piles"), 8);
            first.insert(QStringLiteral("idle_piles"), 5);
            first.insert(QStringLiteral("online_piles"), 8);
            first.insert(QStringLiteral("online_rate"), 100.0);
            first.insert(QStringLiteral("base_price"), 1.20);
            first.insert(QStringLiteral("price"), 1.20);
            stations.append(first);

            QJsonObject second;
            second.insert(QStringLiteral("id"), 12);
            second.insert(QStringLiteral("name"),
                         QStringLiteral("浑南奥体充电站"));
            second.insert(QStringLiteral("address"),
                         QStringLiteral("沈阳市浑南区营盘北街"));
            second.insert(QStringLiteral("longitude"), 123.4350);
            second.insert(QStringLiteral("latitude"), 41.7450);
            second.insert(QStringLiteral("total_piles"), 16);
            second.insert(QStringLiteral("idle_piles"), 11);
            second.insert(QStringLiteral("online_piles"), 15);
            second.insert(QStringLiteral("online_rate"), 93.8);
            second.insert(QStringLiteral("base_price"), 1.10);
            second.insert(QStringLiteral("price"), 1.10);
            stations.append(second);

            root.insert(QStringLiteral("code"), 0);
            root.insert(QStringLiteral("message"), QStringLiteral("ok"));
            root.insert(QStringLiteral("server_time"),
                        QDateTime::currentDateTime().toString(
                            QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            root.insert(QStringLiteral("total"), stations.size());
            root.insert(QStringLiteral("stations"), stations);
        }

        m_server.sendPacket(
            client, static_cast<quint16>(cp::MsgType::kStationQuery),
            compactJson(root));
    }

    cp::NetServer m_server;
    QTcpSocket *m_client = nullptr;
    int m_heartbeatCount = 0;
    bool m_replyHeartbeats = true;
    bool m_stationError = false;
    int m_stationErrorCode = 0;
    QString m_stationErrorMessage;
    QByteArray m_lastHeartbeatBody;
    QByteArray m_lastStationRequestBody;
};

/** PcServerSession 单元测试：真实 Socket 通道 + 临时端口伪服务器 */
class TstPcServerSession : public QObject
{
    Q_OBJECT

private slots:
    void heartbeatRoundTripAck();
    void autoReconnectsAfterServerRestart();
    void reconnectBackoffAvoidsBurstAndStopHolds();
    void silentHeartbeatTriggersReconnect();
    void stationQueryParsesAndResetsOnError();
};

void TstPcServerSession::heartbeatRoundTripAck()
{
    const quint16 port = reserveFreePort();
    FakePcServer server;
    QVERIFY2(server.start(port), "FakePcServer 应能监听临时端口");

    userclient::PcServerSession session;
    int connectedChanges = 0;
    int pongCount = 0;
    QString lastServerTime;
    connect(&session, &userclient::PcServerSession::connectedChanged, this,
            [&](bool connected) {
                if (connected)
                    ++connectedChanges;
            });
    connect(&session, &userclient::PcServerSession::heartbeatPongReceived, this,
            [&](const QString &serverTime) {
                ++pongCount;
                lastServerTime = serverTime;
            });

    session.setServerAddress(QStringLiteral("127.0.0.1"), port);
    session.setClientId(QStringLiteral("uc-test-01"));
    session.setHeartbeatIntervalMs(50);
    session.setAckTimeoutHeartbeats(3);
    session.start();

    QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(pongCount >= 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        server.heartbeatCount() >= 2
            && session.heartbeatSentCount() >= 2,
        3000);

    QCOMPARE(connectedChanges, 1); // 全程不应发生意外重连
    QVERIFY(session.pongAckCount() >= 1);
    QVERIFY(!lastServerTime.isEmpty());

    QJsonObject heartbeatRequest;
    QVERIFY2(parseJsonObject(server.lastHeartbeatBody(), &heartbeatRequest),
             "服务器收到的心跳必须是 JSON 对象");
    QCOMPARE(heartbeatRequest.value(QStringLiteral("client_id")).toString(),
             QStringLiteral("uc-test-01"));
    QVERIFY(heartbeatRequest.value(QStringLiteral("ts")).isDouble());

    // 未启动的会话不允许查询
    userclient::PcServerSession idle;
    QVERIFY(!idle.queryStations());

    session.stop();
    server.stop();
}

void TstPcServerSession::autoReconnectsAfterServerRestart()
{
    const quint16 port = reserveFreePort();
    FakePcServer server;
    QVERIFY2(server.start(port), "FakePcServer 应能监听临时端口");

    userclient::PcServerSession session;
    int connectedChanges = 0;
    connect(&session, &userclient::PcServerSession::connectedChanged, this,
            [&](bool connected) {
                if (connected)
                    ++connectedChanges;
            });

    session.setServerAddress(QStringLiteral("127.0.0.1"), port);
    session.setHeartbeatIntervalMs(50);
    session.setReconnectBaseIntervalMs(80);
    session.start();

    QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(server.heartbeatCount() >= 1, 3000);
    const int heartbeatsBeforeRestart = server.heartbeatCount();
    const int attemptsBeforeRestart = session.reconnectAttemptCount();

    // 服务端掉线 -> 客户端应进入离线与重连流程
    server.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(), 3000);

    // 同一端口重启服务端 -> 客户端应按退避策略自动连回
    QVERIFY2(server.start(port), "重启后的 FakePcServer 应能重新监听");
    QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        server.heartbeatCount() > heartbeatsBeforeRestart, 3000);
    QVERIFY(session.reconnectAttemptCount() > attemptsBeforeRestart);
    QVERIFY(connectedChanges >= 2); // 断开 + 重连成功

    session.stop();
    server.stop();
}

void TstPcServerSession::reconnectBackoffAvoidsBurstAndStopHolds()
{
    const quint16 port = reserveFreePort(); // 故意不启动服务端

    userclient::PcServerSession session;
    session.setServerAddress(QStringLiteral("127.0.0.1"), port);
    session.setReconnectBaseIntervalMs(80);
    session.start();

    QTest::qWait(700);
    const int attemptsWhileOffline = session.reconnectAttemptCount();
    QVERIFY2(attemptsWhileOffline >= 1,
             "服务端不存在时也应至少完成一次连接尝试");
    QVERIFY2(attemptsWhileOffline <= 8,
             "退避策略下短窗口内连接尝试次数必须有限");

    // stop() 后即使继续等待，也不得再发起任何重连
    session.stop();
    const int frozen = session.reconnectAttemptCount();
    QTest::qWait(300);
    QCOMPARE(session.reconnectAttemptCount(), frozen);
}

void TstPcServerSession::silentHeartbeatTriggersReconnect()
{
    const quint16 port = reserveFreePort();
    FakePcServer server;
    QVERIFY2(server.start(port), "FakePcServer 应能监听临时端口");

    userclient::PcServerSession session;
    session.setServerAddress(QStringLiteral("127.0.0.1"), port);
    session.setHeartbeatIntervalMs(50);
    session.setAckTimeoutHeartbeats(2);
    session.setReconnectBaseIntervalMs(80);
    session.start();

    QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(session.pongAckCount() >= 1, 3000);

    const int attemptsBeforeSilent = session.reconnectAttemptCount();
    server.setReplyHeartbeats(false); // TCP 不断开但不回 pong

    QTRY_VERIFY_WITH_TIMEOUT(
        session.reconnectAttemptCount() > attemptsBeforeSilent, 5000);

    // 服务恢复应答后应能重新拿到 pong ACK
    const int pongsBeforeRecover = session.pongAckCount();
    server.setReplyHeartbeats(true);
    QTRY_VERIFY_WITH_TIMEOUT(
        session.pongAckCount() > pongsBeforeRecover, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 3000);

    session.stop();
    server.stop();
}

void TstPcServerSession::stationQueryParsesAndResetsOnError()
{
    struct QueryCapture {
        int code = -1;
        QString message;
        QVector<userclient::ServerStation> stations;
    };

    const quint16 port = reserveFreePort();
    FakePcServer server;
    QVERIFY2(server.start(port), "FakePcServer 应能监听临时端口");

    userclient::PcServerSession session;
    QVector<QueryCapture> captures;
    connect(&session, &userclient::PcServerSession::stationListReceived, this,
            [&](int code, const QString &message,
                const QVector<userclient::ServerStation> &stations) {
                captures.append({ code, message, stations });
            });

    session.setServerAddress(QStringLiteral("127.0.0.1"), port);
    session.setHeartbeatIntervalMs(10000);
    session.start();
    QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 3000);

    QVERIFY2(session.queryStations(11, 200),
             "已连接状态下应能发送 kStationQuery");
    QTRY_COMPARE_WITH_TIMEOUT(captures.size(), 1, 3000);

    const QueryCapture &ok = captures.first();
    QCOMPARE(ok.code, 0);
    QCOMPARE(ok.message, QStringLiteral("ok"));
    QCOMPARE(ok.stations.size(), 2);

    const userclient::ServerStation &first = ok.stations.first();
    QCOMPARE(first.id, 11);
    QCOMPARE(first.name, QStringLiteral("东软软件园充电站"));
    QCOMPARE(first.address, QStringLiteral("沈阳市浑南区智慧二街100号"));
    QCOMPARE(first.longitude, 123.4958);
    QCOMPARE(first.latitude, 41.7152);
    QCOMPARE(first.totalPiles, 8);
    QCOMPARE(first.idlePiles, 5);
    QCOMPARE(first.onlinePiles, 8);
    QCOMPARE(first.onlineRate, 100.0);
    QCOMPARE(first.basePrice, 1.20);
    QCOMPARE(first.price, 1.20);

    QJsonObject stationRequest;
    QVERIFY2(parseJsonObject(server.lastStationRequestBody(), &stationRequest),
             "服务器收到的查询必须是 JSON 对象");
    QCOMPARE(stationRequest.value(QStringLiteral("station_id")).toInt(), 11);
    QCOMPARE(stationRequest.value(QStringLiteral("limit")).toInt(), 200);

    // 错误码应答：透传 code/message、stations 为空，且请求状态复位可再次查询
    server.setStationError(503, QStringLiteral("电站数据服务未就绪"));
    QVERIFY(session.queryStations());
    QTRY_COMPARE_WITH_TIMEOUT(captures.size(), 2, 3000);
    QCOMPARE(captures.at(1).code, 503);
    QCOMPARE(captures.at(1).message, QStringLiteral("电站数据服务未就绪"));
    QVERIFY(captures.at(1).stations.isEmpty());

    server.clearStationError();
    QVERIFY2(session.queryStations(), "上一次查询已复位，应允许再次发送");
    QTRY_COMPARE_WITH_TIMEOUT(captures.size(), 3, 3000);
    QCOMPARE(captures.at(2).code, 0);
    QCOMPARE(captures.at(2).stations.size(), 2);

    session.stop();
    server.stop();
}

QTEST_GUILESS_MAIN(TstPcServerSession)

#include "tst_pcserver_session.moc"
