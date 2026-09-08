#include "packet_assembler.h"

namespace cp {

namespace {

/** 小端字节序写 16 位大端字段 */
inline void appendBigEndian16(QByteArray &out, quint16 value)
{
    out.append(char((value >> 8) & 0xFF));
    out.append(char(value & 0xFF));
}

/** 小端字节序写 32 位大端字段 */
inline void appendBigEndian32(QByteArray &out, quint32 value)
{
    out.append(char((value >> 24) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char(value & 0xFF));
}

} // namespace

QByteArray PacketAssembler::pack(quint16 msgType, const QByteArray &body)
{
    QByteArray frame;
    frame.reserve(kPacketHeaderSize + body.size());
    frame.append(char(kPacketMagic0));
    frame.append(char(kPacketMagic1));
    appendBigEndian16(frame, msgType);
    appendBigEndian32(frame, static_cast<quint32>(body.size()));
    frame.append(body);
    return frame;
}

void PacketAssembler::feed(const QByteArray &data)
{
    m_buffer.append(data);
}

bool PacketAssembler::nextPacket(quint16 &outMsgType, QByteArray &outBody)
{
    while (!m_buffer.isEmpty()) {
        // 长度不足 8 字节时，先把确定不可能是 Magic 前缀的垃圾清掉
        dropInvalidPrefix();
        if (m_buffer.isEmpty())
            return false;
        if (m_buffer.size() < kPacketHeaderSize)
            return false; // 半包 / 半截 Magic，等待更多数据

        // 帧头魔数校验：不匹配则丢掉 1 字节，向后滑动重新扫描
        if (static_cast<quint8>(m_buffer.at(0)) != kPacketMagic0
            || static_cast<quint8>(m_buffer.at(1)) != kPacketMagic1) {
            m_buffer.remove(0, 1);
            continue;
        }

        // 读取大端序字段
        const quint16 msgType = static_cast<quint16>(
            (static_cast<quint16>(static_cast<quint8>(m_buffer.at(2))) << 8)
            | static_cast<quint16>(static_cast<quint8>(m_buffer.at(3))));
        const quint32 bodyLen =
              (static_cast<quint32>(static_cast<quint8>(m_buffer.at(4))) << 24)
            | (static_cast<quint32>(static_cast<quint8>(m_buffer.at(5))) << 16)
            | (static_cast<quint32>(static_cast<quint8>(m_buffer.at(6))) << 8)
            | static_cast<quint32>(static_cast<quint8>(m_buffer.at(7)));

        // 负载长度异常：视为伪帧头，滑动丢弃，继续寻找下一个可能的 Magic
        if (bodyLen > kPacketMaxBodySize) {
            m_buffer.remove(0, 1);
            continue;
        }

        const qint64 totalSize =
            static_cast<qint64>(kPacketHeaderSize) + static_cast<qint64>(bodyLen);
        if (m_buffer.size() < totalSize)
            return false; // 半包：Body 未到齐，缓存等待更多数据

        outMsgType = msgType;
        outBody = m_buffer.mid(kPacketHeaderSize, static_cast<int>(bodyLen));
        m_buffer.remove(0, static_cast<int>(totalSize));
        return true;
    }
    return false;
}

void PacketAssembler::clear()
{
    m_buffer.clear();
}

void PacketAssembler::dropInvalidPrefix()
{
    while (!m_buffer.isEmpty()) {
        const bool mayBePrefix =
            static_cast<quint8>(m_buffer.at(0)) == kPacketMagic0
            && (m_buffer.size() < 2
                || static_cast<quint8>(m_buffer.at(1)) == kPacketMagic1);
        if (mayBePrefix)
            break;
        m_buffer.remove(0, 1);
    }
}

} // namespace cp
