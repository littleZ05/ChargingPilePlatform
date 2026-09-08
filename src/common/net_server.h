#ifndef CP_NET_SERVER_H
#define CP_NET_SERVER_H

/**
 * NetServer：通用异步非阻塞 Socket 服务端封装（需求 NO.19）
 * 维护人：吴羽桐（feat/shimmer-ywt）
 *
 * 设计说明：
 * - QTcpServer 事件驱动监听，每个接入客户端持有一个独立的
 *   cp::PacketAssembler 收流缓冲，自动处理粘包 / 半包；
 *   同一客户端 TCP 流严格按到达顺序解析，客户端之间互不干扰。
 * - 全程单线程事件循环、信号槽驱动，内部不创建裸线程，
 *   不持有任何 QMutex/std::mutex（零锁方案）。
 * - 客户端连接对象（QTcpSocket*）生命周期由本类托管：
 *   断开时自动回收并从映射移除，调用方仅可借用指针，不得自行 delete。
 */
#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace cp {

class PacketAssembler;

class NetServer : public QObject
{
    Q_OBJECT
public:
    explicit NetServer(QObject *parent = nullptr);
    ~NetServer() override;

    /** 开始监听；默认端口与 cp::kServerPort(9999) 保持一致 */
    bool startServer(quint16 port = 9999);

    /** 停止监听并回收全部客户端连接资源 */
    void stopServer();

    /** 向指定客户端发送业务数据包（client 必须为本服务端当前托管的连接） */
    bool sendPacket(QTcpSocket *client, quint16 msgType, const QByteArray &body);

    /** 向全部在线客户端广播业务数据包 */
    void broadcastPacket(quint16 msgType, const QByteArray &body);

signals:
    void clientConnected(QTcpSocket *client);
    void clientDisconnected(QTcpSocket *client);
    void packetReceived(QTcpSocket *client, quint16 msgType,
                        const QByteArray &body);

private:
    void handleNewConnection();
    void handleClientReadyRead(QTcpSocket *client);
    void handleClientDisconnected(QTcpSocket *client);
    void releaseClient(QTcpSocket *client);

    QTcpServer *m_server = nullptr;          // 持有：监听器
    QMap<QTcpSocket *, PacketAssembler *> m_clients; // 持有：每连接组包器
};

} // namespace cp

#endif // CP_NET_SERVER_H
