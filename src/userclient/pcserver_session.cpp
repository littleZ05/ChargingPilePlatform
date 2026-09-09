#include "pcserver_session.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QTimer>

#include "common.h"
#include "net_client.h"

namespace userclient {

namespace {

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

PcServerSession::PcServerSession(QObject *parent)
    : QObject(parent)
    , m_port(static_cast<quint16>(cp::kServerPort))
    , m_reconnectDelayMs(m_reconnectBaseIntervalMs)
{
    m_netClient = new cp::NetClient(this);

    connect(m_netClient, &cp::NetClient::connected,
            this, &PcServerSession::onConnected);
    connect(m_netClient, &cp::NetClient::disconnected,
            this, &PcServerSession::onDisconnected);
    connect(m_netClient, &cp::NetClient::socketError,
            this, &PcServerSession::onSocketError);
    connect(m_netClient, &cp::NetClient::packetReceived,
            this, &PcServerSession::onPacketReceived);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout,
            this, &PcServerSession::onHeartbeatTick);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &PcServerSession::onReconnectTick);
}

PcServerSession::~PcServerSession()
{
    stop();
}

void PcServerSession::start()
{
    if (m_running)
        return;

    m_running = true;
    m_connected = false;
    m_awaitingPong = false;
    m_missedAcks = 0;
    m_stationQueryPending = false;
    m_reconnectScheduled = false;
    m_reconnectDelayMs = m_reconnectBaseIntervalMs;
    m_heartbeatTimer->stop();
    m_reconnectTimer->stop();

    connectNow();
}

void PcServerSession::stop()
{
    m_running = false;
    m_connected = false;
    m_heartbeatTimer->stop();
    m_reconnectTimer->stop();
    m_reconnectScheduled = false;
    m_awaitingPong = false;
    m_netClient->disconnectFromServer();
}

void PcServerSession::setServerAddress(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void PcServerSession::setClientId(const QString &clientId)
{
    m_clientId = clientId.trimmed();
    if (m_clientId.isEmpty())
        m_clientId = QStringLiteral("userclient-01");
}

void PcServerSession::setHeartbeatIntervalMs(int intervalMs)
{
    if (intervalMs <= 0)
        return;
    m_heartbeatIntervalMs = intervalMs;
    if (m_heartbeatTimer)
        m_heartbeatTimer->setInterval(intervalMs);
}

void PcServerSession::setAckTimeoutHeartbeats(int count)
{
    m_ackTimeoutHeartbeats = qMax(1, count);
}

void PcServerSession::setReconnectBaseIntervalMs(int intervalMs)
{
    if (intervalMs <= 0)
        return;
    m_reconnectBaseIntervalMs = intervalMs;
    m_reconnectMaxIntervalMs =
        qMax(intervalMs, 30000);
    m_reconnectDelayMs = intervalMs;
}

bool PcServerSession::isConnected() const
{
    return m_running && m_connected && m_netClient && m_netClient->isConnected();
}

bool PcServerSession::queryStations(int stationId, int limit)
{
    if (!m_running || !isConnected() || m_stationQueryPending)
        return false;

    stationId = qMax(0, stationId);
    limit = qBound(1, limit, 200);

    QJsonObject request;
    if (stationId > 0)
        request.insert(QStringLiteral("station_id"), stationId);
    request.insert(QStringLiteral("limit"), limit);

    const auto msgType = static_cast<quint16>(cp::MsgType::kStationQuery);
    if (!m_netClient->sendPacket(msgType, compactJson(request))) {
        qWarning() << "[userclient][net] kStationQuery 发送失败";
        return false;
    }

    m_stationQueryPending = true;
    return true;
}

void PcServerSession::connectNow()
{
    if (!m_running)
        return;

    m_reconnectScheduled = false;
    ++m_reconnectAttemptCount;
    qInfo().noquote()
        << QStringLiteral("[userclient][net] 正在连接 %1:%2（第 %3 次）")
               .arg(m_host)
               .arg(m_port)
               .arg(m_reconnectAttemptCount);
    m_netClient->connectToServer(m_host, m_port);
}

void PcServerSession::scheduleReconnect()
{
    if (!m_running || m_reconnectScheduled)
        return;

    m_heartbeatTimer->stop();
    m_reconnectScheduled = true;
    m_reconnectTimer->start(m_reconnectDelayMs);

    qWarning().noquote()
        << QStringLiteral("[userclient][net] %1 ms 后进行自动重连")
               .arg(m_reconnectDelayMs);
    m_reconnectDelayMs =
        qMin(m_reconnectDelayMs * 2, m_reconnectMaxIntervalMs);
}

void PcServerSession::onConnected()
{
    if (!m_running) {
        m_netClient->disconnectFromServer();
        return;
    }

    m_connected = true;
    m_awaitingPong = false;
    m_missedAcks = 0;
    m_reconnectScheduled = false;
    m_reconnectTimer->stop();
    m_reconnectDelayMs = m_reconnectBaseIntervalMs;

    qInfo().noquote()
        << QStringLiteral("[userclient][net] 已连接 PcServer %1:%2")
               .arg(m_host)
               .arg(m_port);
    emit connectedChanged(true);

    m_heartbeatTimer->start();
    sendHeartbeat();
}

void PcServerSession::onDisconnected()
{
    if (!m_running)
        return;

    const bool wasConnected = m_connected;
    m_connected = false;
    m_awaitingPong = false;

    if (m_stationQueryPending)
        notifyQueryFailed(QStringLiteral("连接已断开，查询未完成"));

    if (wasConnected)
        emit connectedChanged(false);
    scheduleReconnect();
}

void PcServerSession::onSocketError(const QString &errorString)
{
    if (!m_running)
        return;

    const bool wasConnected = m_connected;
    m_connected = false;
    m_awaitingPong = false;

    qWarning().noquote()
        << QStringLiteral("[userclient][net] Socket 错误：%1").arg(errorString);

    if (m_stationQueryPending)
        notifyQueryFailed(QStringLiteral("连接错误：%1").arg(errorString));

    if (wasConnected)
        emit connectedChanged(false);
    scheduleReconnect();
}

void PcServerSession::onPacketReceived(quint16 msgType, const QByteArray &body)
{
    switch (msgType) {
    case static_cast<quint16>(cp::MsgType::kHeartbeat):
        handleHeartbeatPacket(body);
        break;
    case static_cast<quint16>(cp::MsgType::kStationQuery):
        handleStationQueryPacket(body);
        break;
    default:
        qWarning() << "[userclient][net] 收到未注册 MsgType:" << msgType;
        break;
    }
}

void PcServerSession::sendHeartbeat()
{
    if (!m_running || !isConnected())
        return;

    QJsonObject request;
    request.insert(QStringLiteral("client_id"), m_clientId);
    request.insert(QStringLiteral("ts"),
                   QDateTime::currentSecsSinceEpoch());

    const auto msgType = static_cast<quint16>(cp::MsgType::kHeartbeat);
    if (!m_netClient->sendPacket(msgType, compactJson(request))) {
        qWarning() << "[userclient][net] kHeartbeat 发送失败，触发重连";
        m_netClient->disconnectFromServer();
        scheduleReconnect();
        return;
    }

    ++m_heartbeatSentCount;
    m_awaitingPong = true;
}

void PcServerSession::onHeartbeatTick()
{
    if (!m_running || !isConnected())
        return;

    if (m_awaitingPong) {
        ++m_missedAcks;
        if (m_missedAcks >= m_ackTimeoutHeartbeats) {
            qWarning().noquote()
                << QStringLiteral(
                       "[userclient][net] 连续 %1 个心跳周期无 pong ACK，判定失联并重连")
                       .arg(m_missedAcks);
            m_netClient->disconnectFromServer();
            scheduleReconnect();
            return;
        }
    }

    sendHeartbeat();
}

void PcServerSession::onReconnectTick()
{
    connectNow();
}

void PcServerSession::handleHeartbeatPacket(const QByteArray &body)
{
    QJsonObject response;
    if (!parseJsonObject(body, &response)) {
        qWarning() << "[userclient][net] 心跳应答不是 JSON 对象";
        return;
    }

    const int code = response.value(QStringLiteral("code")).toInt(-1);
    const QString message = response.value(QStringLiteral("message")).toString();
    if (code != 0 || message != QStringLiteral("pong")) {
        qWarning().noquote()
            << QStringLiteral("[userclient][net] 心跳应答异常 code=%1 message=%2")
                   .arg(code)
                   .arg(message);
        return;
    }

    m_awaitingPong = false;
    m_missedAcks = 0;
    ++m_pongAckCount;
    emit heartbeatPongReceived(
        response.value(QStringLiteral("server_time")).toString());
}

void PcServerSession::handleStationQueryPacket(const QByteArray &body)
{
    if (!m_stationQueryPending) {
        qWarning() << "[userclient][net] 收到非预期的 kStationQuery 应答";
        return;
    }
    m_stationQueryPending = false;

    QJsonObject response;
    if (!parseJsonObject(body, &response)) {
        emit stationListReceived(
            -1, QStringLiteral("电站查询应答不是 JSON 对象"),
            QVector<ServerStation>());
        return;
    }

    const int code = response.value(QStringLiteral("code")).toInt(-1);
    const QString message = response.value(QStringLiteral("message")).toString();

    QVector<ServerStation> stations;
    if (code == 0) {
        const QJsonArray array =
            response.value(QStringLiteral("stations")).toArray();
        stations.reserve(array.size());
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            ServerStation station;
            station.id          = object.value(QStringLiteral("id")).toInt();
            station.name        = object.value(QStringLiteral("name")).toString();
            station.address     = object.value(QStringLiteral("address")).toString();
            station.longitude   = object.value(QStringLiteral("longitude")).toDouble();
            station.latitude    = object.value(QStringLiteral("latitude")).toDouble();
            station.totalPiles  = object.value(QStringLiteral("total_piles")).toInt();
            station.idlePiles   = object.value(QStringLiteral("idle_piles")).toInt();
            station.onlinePiles = object.value(QStringLiteral("online_piles")).toInt();
            station.onlineRate  = object.value(QStringLiteral("online_rate")).toDouble();
            station.basePrice   = object.value(QStringLiteral("base_price")).toDouble();
            station.price       = object.value(QStringLiteral("price")).toDouble();
            stations.append(station);
        }
    }

    emit stationListReceived(code, message, stations);
}

void PcServerSession::notifyQueryFailed(const QString &reason)
{
    if (!m_stationQueryPending)
        return;
    m_stationQueryPending = false;
    emit stationListReceived(-1, reason, QVector<ServerStation>());
}

} // namespace userclient
