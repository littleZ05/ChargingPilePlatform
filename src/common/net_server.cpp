#include "net_server.h"

#include "packet_assembler.h"

#include <QTcpServer>
#include <QTcpSocket>

namespace cp {

NetServer::NetServer(QObject *parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &NetServer::handleNewConnection);
}

NetServer::~NetServer()
{
    stopServer();
}

bool NetServer::startServer(quint16 port)
{
    if (m_server->isListening())
        m_server->close();

    const bool ok = m_server->listen(QHostAddress::Any, port);
    if (!ok)
        return false;
    return true;
}

void NetServer::stopServer()
{
    if (m_server && m_server->isListening())
        m_server->close();

    // 回收全部客户端连接（先断开连接，避免重复进入断开流程）
    const QList<QTcpSocket *> clients = m_clients.keys();
    for (QTcpSocket *client : clients)
        releaseClient(client);
}

bool NetServer::sendPacket(QTcpSocket *client, quint16 msgType,
                           const QByteArray &body)
{
    if (!client || !m_clients.contains(client))
        return false;
    if (client->state() != QAbstractSocket::ConnectedState)
        return false;

    const QByteArray frame = PacketAssembler::pack(msgType, body);
    return client->write(frame) == static_cast<qint64>(frame.size());
}

void NetServer::broadcastPacket(quint16 msgType, const QByteArray &body)
{
    const QList<QTcpSocket *> clients = m_clients.keys();
    for (QTcpSocket *client : clients)
        sendPacket(client, msgType, body);
}

void NetServer::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        if (!client)
            continue;

        auto *assembler = new PacketAssembler();
        m_clients.insert(client, assembler);

        connect(client, &QTcpSocket::readyRead,
                this, [this, client]() { handleClientReadyRead(client); });
        connect(client, &QTcpSocket::disconnected,
                this, [this, client]() { handleClientDisconnected(client); });

        emit clientConnected(client);
    }
}

void NetServer::handleClientReadyRead(QTcpSocket *client)
{
    auto it = m_clients.find(client);
    if (it == m_clients.end())
        return; // 断开清理竞态窗口内残留的 readyRead，直接忽略

    PacketAssembler *assembler = it.value();
    assembler->feed(client->readAll());

    quint16 msgType = 0;
    QByteArray body;
    while (assembler->nextPacket(msgType, body))
        emit packetReceived(client, msgType, body);
}

void NetServer::handleClientDisconnected(QTcpSocket *client)
{
    if (!m_clients.contains(client))
        return; // 防重复触发
    releaseClient(client);
}

void NetServer::releaseClient(QTcpSocket *client)
{
    if (!client)
        return;

    auto it = m_clients.find(client);
    if (it != m_clients.end()) {
        delete it.value(); // PacketAssembler：仅本类持有并释放
        m_clients.erase(it);
        emit clientDisconnected(client);
    }

    // 断开连接；若对端已断开，直接回收句柄
    client->disconnect(this); // 解除本类注册的回调，避免递归清理
    if (client->state() != QAbstractSocket::UnconnectedState)
        client->disconnectFromHost();
    if (client->state() != QAbstractSocket::UnconnectedState)
        client->abort();
    client->deleteLater();
}

} // namespace cp
