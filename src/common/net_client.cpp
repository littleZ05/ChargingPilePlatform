#include "net_client.h"

#include <QTcpSocket>

namespace cp {

NetClient::NetClient(QObject *parent)
    : QObject(parent)
{
    ensureSocket();
}

NetClient::~NetClient()
{
    if (m_socket) {
        // 主动断开，避免析构时底层句柄在事件循环内仍持有连接
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void NetClient::connectToServer(const QString &host, quint16 port)
{
    ensureSocket();
    // 若上一次连接尚未结束，先复位到可重连状态
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
        m_assembler.clear();
    }
    m_socket->connectToHost(host, port);
}

void NetClient::disconnectFromServer()
{
    if (!m_socket)
        return;
    // 连接建立过程中断开会自动中止握手；已连接则走四次挥手
    m_socket->disconnectFromHost();
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        m_assembler.clear();
}

bool NetClient::sendPacket(quint16 msgType, const QByteArray &body)
{
    if (!isConnected() || !m_socket)
        return false;
    const QByteArray frame = PacketAssembler::pack(msgType, body);
    return m_socket->write(frame) == static_cast<qint64>(frame.size());
}

bool NetClient::isConnected() const
{
    return m_socket
        && m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetClient::ensureSocket()
{
    if (m_socket)
        return;

    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, [this]() { emit connected(); });

    connect(m_socket, &QTcpSocket::disconnected,
            this, [this]() {
        // 连接已关闭，丢弃残留半包，避免污染下一次连接
        m_assembler.clear();
        emit disconnected();
    });

    connect(m_socket, &QTcpSocket::readyRead,
            this, [this]() {
        m_assembler.feed(m_socket->readAll());
        quint16 msgType = 0;
        QByteArray body;
        while (m_assembler.nextPacket(msgType, body))
            emit packetReceived(msgType, body);
    });

    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError) {
        emit socketError(m_socket->errorString());
    });
}

} // namespace cp
