#ifndef CP_NET_CLIENT_H
#define CP_NET_CLIENT_H

/**
 * NetClient：通用异步非阻塞 Socket 客户端封装（需求 NO.19）
 * 维护人：吴羽桐（feat/shimmer-ywt）
 *
 * 设计说明：
 * - 基于 QTcpSocket 的 Qt 信号槽异步事件驱动模型，全程单线程事件循环，
 *   内部不创建裸线程、不持有任何 QMutex/std::mutex（零锁方案）。
 * - 收流数据交由 cp::PacketAssembler 组包，自动处理粘包 / 半包；
 *   每解析出一个完整包即发射 packetReceived。
 * - 发送统一走 PacketAssembler::pack 二进制帧：
 *   [Magic:2B][MsgType:2B 大端][BodyLen:4B 大端][Body:N]。
 */
#include <QByteArray>
#include <QObject>
#include <QString>

#include "packet_assembler.h"

class QTcpSocket;

namespace cp {

class NetClient : public QObject
{
    Q_OBJECT
public:
    explicit NetClient(QObject *parent = nullptr);
    ~NetClient() override;

    /** 异步发起连接；结果通过 connected() / socketError() 通知 */
    void connectToServer(const QString &host, quint16 port);

    /** 主动断开（若仍在连接中会自动中止握手），断开完成发射 disconnected() */
    void disconnectFromServer();

    /**
     * 发送一个业务数据包（仅本端已连接时有效）。
     * @return true 帧已完整写入 Qt 发送缓冲，不保证对端已收到。
     */
    bool sendPacket(quint16 msgType, const QByteArray &body);

    /** 当前是否处于已连接状态 */
    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void packetReceived(quint16 msgType, const QByteArray &body);
    void socketError(const QString &errorString);

private:
    void ensureSocket();

    QTcpSocket *m_socket = nullptr;      // 持有：QTcpSocket（本类构造/析构管理）
    PacketAssembler m_assembler;          // 持有：收流组包器（值语义，零手动释放）
};

} // namespace cp

#endif // CP_NET_CLIENT_H
