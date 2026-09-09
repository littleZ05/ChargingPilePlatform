#include "servergateway.h"

#include "common.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

ServerGateway::ServerGateway(QObject *parent)
    : QObject(parent)
{
    // 网络事件 -> 上层信号
    connect(&m_client, &cp::NetClient::connected, this, &ServerGateway::onConnected);
    connect(&m_client, &cp::NetClient::disconnected, this, &ServerGateway::onDisconnected);
    connect(&m_client, &cp::NetClient::socketError, this, &ServerGateway::onError);
    connect(&m_client, &cp::NetClient::packetReceived, this, &ServerGateway::onPacket);
}

void ServerGateway::connectTo(const QString &host, quint16 port)
{
    emit logMessage(QStringLiteral("正在连接服务器 %1:%2 ...").arg(host).arg(port));
    m_client.connectToServer(host, port);
}

void ServerGateway::disconnectFrom()
{
    m_client.disconnectFromServer();
}

bool ServerGateway::isConnected() const
{
    return m_client.isConnected();
}

void ServerGateway::sendHeartbeat()
{
    QJsonObject o;
    o.insert(QStringLiteral("client_id"), QStringLiteral("userclient"));
    o.insert(QStringLiteral("ts"),
             QDateTime::currentSecsSinceEpoch());
    m_client.sendPacket(cp::MsgType::kHeartbeat,
                        QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void ServerGateway::requestStations(int limit)
{
    QJsonObject o;
    o.insert(QStringLiteral("limit"), limit);
    m_client.sendPacket(cp::MsgType::kStationQuery,
                        QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void ServerGateway::reportOrder(const QString &orderNo, const QString &pileCode,
                                double kwh, double amount)
{
    QJsonObject o;
    o.insert(QStringLiteral("order_no"), orderNo);
    o.insert(QStringLiteral("pile_code"), pileCode);
    o.insert(QStringLiteral("kwh"), kwh);
    o.insert(QStringLiteral("amount"), amount);
    m_client.sendPacket(cp::MsgType::kOrderReport,
                        QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void ServerGateway::onConnected()
{
    emit stateChanged(true);
    emit logMessage(QStringLiteral("已连接服务器"));
}

void ServerGateway::onDisconnected()
{
    emit stateChanged(false);
    emit logMessage(QStringLiteral("连接已断开"));
}

void ServerGateway::onError(const QString &errorString)
{
    emit logMessage(QStringLiteral("网络错误：%1").arg(errorString));
}

void ServerGateway::onPacket(quint16 msgType, const QByteArray &body)
{
    const QJsonObject obj =
        QJsonDocument::fromJson(body).object();
    const int code = obj.value(QStringLiteral("code")).toInt(-1);
    const QString message =
        obj.value(QStringLiteral("msg")).toString();

    if (msgType == cp::MsgType::kHeartbeat) {
        emit logMessage(code == 0
                            ? QStringLiteral("心跳 OK")
                            : QStringLiteral("心跳异常：%1").arg(message));
    } else if (msgType == cp::MsgType::kStationQuery) {
        if (code != 0) {
            emit logMessage(QStringLiteral("电站查询失败(%1)：%2").arg(code).arg(message));
            return;
        }
        const QJsonArray stations = obj.value(QStringLiteral("stations")).toArray();
        int onSale = 0;
        for (const auto &v : stations) {
            if (v.toObject().value(QStringLiteral("is_discount")).toBool(false))
                ++onSale;
        }
        emit stationsReceived(stations.size(), onSale);
        emit logMessage(QStringLiteral("收到 %1 座电站，其中闲时特惠 %2 座")
                            .arg(stations.size()).arg(onSale));
    } else if (msgType == cp::MsgType::kOrderReport) {
        const bool received =
            obj.value(QStringLiteral("received")).toBool(false);
        emit orderReported(code, message, received);
        emit logMessage(code == 0 && received
                            ? QStringLiteral("订单上报成功：%1").arg(message)
                            : QStringLiteral("订单上报失败(%1)：%2").arg(code).arg(message));
    }
}
