#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>

#include "common.h"
#include "net_client.h"
#include "net_server.h"
#include "packet_assembler.h"

namespace {

constexpr int kWaitTimeoutMs = 5000;

/** 事件循环驱动等待：直到所有 QSignalSpy 均已捕获到信号或超时 */
bool waitForAllSpies(const QList<QSignalSpy *> &spies, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    auto allCaught = [&spies]() {
        for (QSignalSpy *spy : spies) {
            if (spy->count() == 0)
                return false;
        }
        return true;
    };

    while (!allCaught() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return allCaught();
}

/** 取一个本机空闲端口（先让系统分配，关闭后由被测服务端监听） */
quint16 reserveFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return static_cast<quint16>(cp::kServerPort);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

} // namespace

/** 需求 NO.19 NetClient / NetServer 端到端自动化联调测试 */
class TestNetSocket : public QObject
{
    Q_OBJECT
public:
    TestNetSocket()
    {
        // QTcpSocket* 会出现在 NetServer 信号参数中，提前注册便于 QSignalSpy 记录
        qRegisterMetaType<QTcpSocket *>("QTcpSocket*");
    }

private slots:
    void init();
    void serverListenAndClientConnect();
    void clientHeartbeatReachesServer();
    void serverResponseReachesClient();
    void burstAndStickyPacketsRoundTrip();
    void clientDisconnectReleasesResources();

private:
    /** 建连并装配收发收集器；等待两端连接信号后返回 */
    bool connectPair(cp::NetServer &server, cp::NetClient &client,
                     quint16 port);

    QTcpSocket *m_serverClient = nullptr; // 借用指针：生命周期由 NetServer 托管
    int m_clientConnected = 0;
    int m_clientDisconnected = 0;
    int m_serverClientConnected = 0;
    int m_serverClientDisconnected = 0;
    QList<quint16> m_serverTypes;
    QList<QByteArray> m_serverBodies;
    QList<quint16> m_clientTypes;
    QList<QByteArray> m_clientBodies;
};

void TestNetSocket::init()
{
    m_serverClient = nullptr;
    m_clientConnected = 0;
    m_clientDisconnected = 0;
    m_serverClientConnected = 0;
    m_serverClientDisconnected = 0;
    m_serverTypes.clear();
    m_serverBodies.clear();
    m_clientTypes.clear();
    m_clientBodies.clear();
}

bool TestNetSocket::connectPair(cp::NetServer &server, cp::NetClient &client,
                                quint16 port)
{
    init();

    connect(&server, &cp::NetServer::clientConnected, this,
            [this](QTcpSocket *clientSocket) {
                ++m_serverClientConnected;
                m_serverClient = clientSocket;
            });
    connect(&server, &cp::NetServer::clientDisconnected, this,
            [this](QTcpSocket *) { ++m_serverClientDisconnected; });
    connect(&server, &cp::NetServer::packetReceived, this,
            [this](QTcpSocket *, quint16 msgType, const QByteArray &body) {
                m_serverTypes.append(msgType);
                m_serverBodies.append(body);
            });

    connect(&client, &cp::NetClient::connected, this,
            [this]() { ++m_clientConnected; });
    connect(&client, &cp::NetClient::disconnected, this,
            [this]() { ++m_clientDisconnected; });
    connect(&client, &cp::NetClient::packetReceived, this,
            [this](quint16 msgType, const QByteArray &body) {
                m_clientTypes.append(msgType);
                m_clientBodies.append(body);
            });

    // QSignalSpy 记录信号；事件循环轮询等待两端连接均建立
    QSignalSpy clientConnSpy(&client, &cp::NetClient::connected);
    QSignalSpy serverConnSpy(&server, &cp::NetServer::clientConnected);

    client.connectToServer(QStringLiteral("127.0.0.1"), port);
    if (!waitForAllSpies({&clientConnSpy, &serverConnSpy}, kWaitTimeoutMs))
        return false;
    return m_clientConnected == 1 && m_serverClientConnected == 1
        && m_serverClient != nullptr && client.isConnected();
}

void TestNetSocket::serverListenAndClientConnect()
{
    const quint16 port = reserveFreePort();

    // 端口被占用时 startServer 应返回 false（监听失败路径）
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::LocalHost, port));
    cp::NetServer blockedServer;
    QVERIFY(!blockedServer.startServer(port));
    blocker.close();

    cp::NetServer server;
    QVERIFY(server.startServer(port));
    QVERIFY(server.startServer(port)); // 已监听时重启监听同样应成功

    cp::NetClient client;
    QVERIFY2(connectPair(server, client, port),
             "client should establish connection within timeout");
    QCOMPARE(client.isConnected(), true);
    QCOMPARE(m_clientConnected, 1);
    QCOMPARE(m_serverClientConnected, 1);
    QCOMPARE(m_serverClient->state(), QAbstractSocket::ConnectedState);
}

void TestNetSocket::clientHeartbeatReachesServer()
{
    const quint16 port = reserveFreePort();
    cp::NetServer server;
    QVERIFY(server.startServer(port));

    cp::NetClient client;
    QVERIFY2(connectPair(server, client, port),
             "client should establish connection within timeout");

    const auto type = static_cast<quint16>(cp::MsgType::kHeartbeat);
    const QByteArray body =
        QByteArrayLiteral("{\"alive\":true,\"ts\":1750000000}");
    QVERIFY(client.sendPacket(type, body));

    QTRY_COMPARE_WITH_TIMEOUT(m_serverBodies.size(), 1, kWaitTimeoutMs);
    QCOMPARE(m_serverTypes.size(), 1);
    QCOMPARE(m_serverTypes.first(), type);
    QCOMPARE(m_serverBodies.first(), body);

    // 未连接时发送应返回 false，不得产生脏数据
    cp::NetClient offline;
    QVERIFY(!offline.sendPacket(type, body));
    QVERIFY(!offline.isConnected());
}

void TestNetSocket::serverResponseReachesClient()
{
    const quint16 port = reserveFreePort();
    cp::NetServer server;
    QVERIFY(server.startServer(port));

    cp::NetClient client;
    QVERIFY2(connectPair(server, client, port),
             "client should establish connection within timeout");

    // 客户端请求 -> 服务器按指定连接回传响应包
    const auto hb = static_cast<quint16>(cp::MsgType::kHeartbeat);
    const QByteArray request = QByteArrayLiteral("{\"cmd\":\"ping\"}");
    QVERIFY(client.sendPacket(hb, request));
    QTRY_COMPARE_WITH_TIMEOUT(m_serverBodies.size(), 1, kWaitTimeoutMs);
    QCOMPARE(m_serverBodies.first(), request);

    const auto respType = static_cast<quint16>(cp::MsgType::kStationQuery);
    const QByteArray response =
        QByteArrayLiteral("{\"stations\":[{\"id\":1,\"name\":\"A站\"}]}");
    QVERIFY(server.sendPacket(m_serverClient, respType, response));
    QTRY_COMPARE_WITH_TIMEOUT(m_clientBodies.size(), 1, kWaitTimeoutMs);
    QCOMPARE(m_clientTypes.size(), 1);
    QCOMPARE(m_clientTypes.first(), respType);
    QCOMPARE(m_clientBodies.first(), response);

    // 广播包：当前唯一在线客户端同样应完整收到
    const QByteArray broadcastBody = QByteArrayLiteral("{\"notice\":\"maintenance\"}");
    server.broadcastPacket(hb, broadcastBody);
    QTRY_COMPARE_WITH_TIMEOUT(m_clientBodies.size(), 2, kWaitTimeoutMs);
    QCOMPARE(m_clientTypes.at(1), hb);
    QCOMPARE(m_clientBodies.at(1), broadcastBody);

    // 未托管连接 / 空指针不能发送
    QVERIFY(!server.sendPacket(nullptr, hb, QByteArray()));
    QTcpSocket foreignSocket;
    QVERIFY(!server.sendPacket(&foreignSocket, hb, QByteArrayLiteral("x")));
}

void TestNetSocket::burstAndStickyPacketsRoundTrip()
{
    const quint16 port = reserveFreePort();
    cp::NetServer server;
    QVERIFY(server.startServer(port));

    cp::NetClient client;
    QVERIFY2(connectPair(server, client, port),
             "client should establish connection within timeout");

    const auto hb = static_cast<quint16>(cp::MsgType::kHeartbeat);
    constexpr int kN = 30;
    QList<QByteArray> sent;
    for (int i = 0; i < kN; ++i) {
        QByteArray body;
        if (i != 0) {
            body = QByteArrayLiteral("seq=") + QByteArray::number(i)
                 + QByteArrayLiteral("|payload=")
                 + QByteArray(i % 9, char('A' + (i % 26)));
        }
        sent.append(body);
    }

    // 1) 客户端连续快速发送多包（TCP 底层可能粘包），服务器逐包解析
    for (const QByteArray &body : sent)
        QVERIFY(client.sendPacket(hb, body));
    QTRY_COMPARE_WITH_TIMEOUT(m_serverBodies.size(), kN, kWaitTimeoutMs);
    QCOMPARE(m_serverTypes.size(), kN);
    QCOMPARE(m_serverBodies.size(), kN);
    for (int i = 0; i < kN; ++i) {
        QCOMPARE(m_serverTypes.at(i), hb);
        QCOMPARE(m_serverBodies.at(i), sent.at(i));
    }

    // 2) 服务器逐包回显，客户端收到与发送完全一致的包序列
    for (const QByteArray &body : sent)
        QVERIFY(server.sendPacket(m_serverClient, hb, body));
    QTRY_COMPARE_WITH_TIMEOUT(m_clientBodies.size(), kN, kWaitTimeoutMs);
    QCOMPARE(m_clientTypes.size(), kN);
    QCOMPARE(m_clientBodies.size(), kN);
    for (int i = 0; i < kN; ++i) {
        QCOMPARE(m_clientTypes.at(i), hb);
        QCOMPARE(m_clientBodies.at(i), sent.at(i));
    }

    // 3) 用一个“粘包客户端”单次写入多条完整帧，模拟数据整批到达，
    //    服务器应逐条解出且与写入内容完全一致
    QTcpSocket stickyClient;
    QSignalSpy stickyConnSpy(&stickyClient, &QTcpSocket::connected);
    stickyClient.connectToHost(QStringLiteral("127.0.0.1"), port);
    QVERIFY2(waitForAllSpies({&stickyConnSpy}, kWaitTimeoutMs),
             "sticky client should connect within timeout");

    constexpr int kStickyCount = 20;
    const auto reportType = static_cast<quint16>(cp::MsgType::kOrderReport);
    QByteArray stream;
    QList<QByteArray> stickyBodies;
    for (int i = 0; i < kStickyCount; ++i) {
        const QByteArray body =
            QByteArrayLiteral("sticky=") + QByteArray::number(i);
        stickyBodies.append(body);
        stream += cp::PacketAssembler::pack(reportType, body);
    }
    QCOMPARE(stickyClient.write(stream), qint64(stream.size()));

    QTRY_COMPARE_WITH_TIMEOUT(m_serverBodies.size(), kN + kStickyCount,
                              kWaitTimeoutMs);
    QCOMPARE(m_serverTypes.size(), kN + kStickyCount);
    for (int i = 0; i < kStickyCount; ++i) {
        const int index = kN + i;
        QCOMPARE(m_serverTypes.at(index), reportType);
        QCOMPARE(m_serverBodies.at(index), stickyBodies.at(i));
    }
}

void TestNetSocket::clientDisconnectReleasesResources()
{
    const quint16 port = reserveFreePort();
    cp::NetServer server;
    QVERIFY(server.startServer(port));

    cp::NetClient client;
    QVERIFY2(connectPair(server, client, port),
             "client should establish connection within timeout");

    QSignalSpy clientDiscSpy(&client, &cp::NetClient::disconnected);
    QSignalSpy serverDiscSpy(&server, &cp::NetServer::clientDisconnected);

    client.disconnectFromServer();
    QVERIFY2(waitForAllSpies({&clientDiscSpy, &serverDiscSpy},
                             kWaitTimeoutMs),
             "both disconnect signals should be emitted within timeout");
    QCOMPARE(m_clientDisconnected, 1);
    QCOMPARE(m_serverClientDisconnected, 1);
    QCOMPARE(client.isConnected(), false);

    // 连接已从服务器托管映射释放：原指针不能再发送数据
    QTcpSocket *released = m_serverClient;
    const auto hb = static_cast<quint16>(cp::MsgType::kHeartbeat);
    QVERIFY(!server.sendPacket(released, hb, QByteArrayLiteral("late")));
    QCOMPARE(m_clientBodies.size(), 0);

    // 服务器仍可正常接收新客户端（证明按连接资源已释放干净、无残留阻塞）
    cp::NetClient client2;
    QSignalSpy client2ConnSpy(&client2, &cp::NetClient::connected);
    QSignalSpy server2ConnSpy(&server, &cp::NetServer::clientConnected);
    client2.connectToServer(QStringLiteral("127.0.0.1"), port);
    QVERIFY2(waitForAllSpies({&client2ConnSpy, &server2ConnSpy},
                             kWaitTimeoutMs),
             "second client should connect within timeout");
    QVERIFY(client2.isConnected());
    QVERIFY(m_serverClient != nullptr);
    QVERIFY(m_serverClient != released); // 新连接分配新会话资源

    // stopServer 回收剩余连接与监听；随后端口可再次成功监听
    server.stopServer();
    QVERIFY(server.startServer(port));
}

QTEST_GUILESS_MAIN(TestNetSocket)
#include "tst_net_socket.moc"
