#ifndef CP_PACKET_ASSEMBLER_H
#define CP_PACKET_ASSEMBLER_H

/**
 * PacketAssembler：通信底层二进制协议组包 / 解包模块（需求 NO.19）
 * 维护人：吴羽桐（feat/shimmer-ywt）
 *
 * 线上帧格式（全部为二进制，字段间无分隔符）：
 *   [Magic:2B]        0x43 0x50（即 'C' 'P'）
 *   [MsgType:2B]      大端序 quint16，取值对应 cp::MsgType（0 心跳/1 电站查询/2 订单上报）
 *   [BodyLen:4B]      大端序 quint32，Body 负载字节数
 *   [Body:N]          JSON 字符串或二进制负载
 *
 * 使用示例：
 *   QByteArray frame = cp::PacketAssembler::pack(
 *       static_cast<quint16>(cp::MsgType::kHeartbeat), body);
 *
 *   cp::PacketAssembler asm_;
 *   asm_.feed(recvData);
 *   quint16 msgType; QByteArray body;
 *   while (asm_.nextPacket(msgType, body)) { ... }
 *
 * 说明：
 * - nextPacket() 为一次性消费语义：成功返回一个完整包并立即从内部缓冲移除；
 *   缓冲区仍可能残留后续半包/粘包数据，可继续调用直至返回 false。
 * - 解包器具备流式容错：半包会缓存等待后续数据；头部魔数不匹配或声明的
 *   负载长度超过 kPacketMaxBodySize 时，会逐字节向后滑动重新扫描对齐，
 *   直到找到合法帧头或数据耗尽。
 */
#include <QByteArray>
#include <QtGlobal>

namespace cp {

/** 帧头魔数（线上字节序：0x43 'C'，0x50 'P'） */
inline constexpr quint8 kPacketMagic0 = 0x43;
inline constexpr quint8 kPacketMagic1 = 0x50;

/** 帧头长度 = Magic(2) + MsgType(2) + BodyLen(4) */
inline constexpr int kPacketHeaderSize = 2 + 2 + 4;

/** BodyLen 防呆上限：超出视为损坏帧，丢弃并重新对齐（负载为 JSON 消息，远小于该值） */
inline constexpr quint32 kPacketMaxBodySize = 64u * 1024u * 1024u;

/** 组包器 / 解包器 */
class PacketAssembler
{
public:
    /** 组包：按大端序写入消息类型与负载长度，返回完整二进制帧 */
    static QByteArray pack(quint16 msgType, const QByteArray &body);

    PacketAssembler() = default;

    /** 追加一段网络原始数据流（可任意切分；粘包、半包均由内部缓冲处理） */
    void feed(const QByteArray &data);

    /**
     * 提取一个完整数据包。
     * @return true  已取出一个完整包，msgType/body 被填充；
     *         false 尚无完整包（半包等待更多数据 / 数据耗尽），输出参数不变。
     */
    bool nextPacket(quint16 &outMsgType, QByteArray &outBody);

    /** 清空内部缓冲（如连接断开 / 重连时调用） */
    void clear();

private:
    /** 丢弃不可能成为帧头的头部垃圾字节（保留可能是 Magic 前缀的字节） */
    void dropInvalidPrefix();

    QByteArray m_buffer;
};

} // namespace cp

#endif // CP_PACKET_ASSEMBLER_H
