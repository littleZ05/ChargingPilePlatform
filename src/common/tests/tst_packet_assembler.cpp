#include <QtTest/QtTest>

#include "common.h"
#include "packet_assembler.h"

/** 需求 NO.19 通信底层协议组包/解包模块 PacketAssembler 单元测试 */
class TestPacketAssembler : public QObject
{
    Q_OBJECT
private slots:
    void packHeaderFields();
    void normalRoundTrip();
    void emptyBody();
    void stickyPackets();
    void partialPacketTwoFeeds();
    void dirtyDataResync();
    void oversizedBodyDiscarded();
    void clearResetsBuffer();
};

void TestPacketAssembler::packHeaderFields()
{
    // 帧 = Magic(2) + MsgType(2, BE) + BodyLen(4, BE) + Body
    const QByteArray body = QByteArrayLiteral("{\"k\":1}");
    const QByteArray frame = cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kStationQuery), body);

    QCOMPARE(frame.size(), cp::kPacketHeaderSize + body.size());
    QCOMPARE(static_cast<quint8>(frame.at(0)), cp::kPacketMagic0); // 'C'
    QCOMPARE(static_cast<quint8>(frame.at(1)), cp::kPacketMagic1); // 'P'

    // MsgType=1 大端序：00 01
    QCOMPARE(static_cast<quint8>(frame.at(2)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(3)), quint8(0x01));

    // BodyLen=7（"{\"k\":1}" 为 7 字节）大端序：00 00 00 07
    QCOMPARE(body.size(), 7);
    QCOMPARE(static_cast<quint8>(frame.at(4)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(5)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(6)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(7)), quint8(0x07));
    QCOMPARE(frame.mid(cp::kPacketHeaderSize), body);
}

void TestPacketAssembler::normalRoundTrip()
{
    cp::PacketAssembler asm_;
    const QByteArray body =
        QByteArrayLiteral("{\"type\":\"heartbeat\",\"ts\":1750000000}");

    asm_.feed(cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kHeartbeat), body));

    quint16 msgType = 0xFFFF;
    QByteArray outBody;
    QVERIFY(asm_.nextPacket(msgType, outBody));
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kHeartbeat));
    QCOMPARE(outBody, body);

    // 缓冲区已清空，不应再取出第二个包
    msgType = 0xFFFF;
    outBody.clear();
    QVERIFY(!asm_.nextPacket(msgType, outBody));
}

void TestPacketAssembler::emptyBody()
{
    cp::PacketAssembler asm_;
    asm_.feed(cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kOrderReport), QByteArray()));

    quint16 msgType = 0xFFFF;
    QByteArray outBody = QByteArrayLiteral("dirty");
    QVERIFY(asm_.nextPacket(msgType, outBody));
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kOrderReport));
    QVERIFY(outBody.isEmpty());
}

void TestPacketAssembler::stickyPackets()
{
    cp::PacketAssembler asm_;
    const QByteArray body1 = QByteArrayLiteral("{\"seq\":1}");
    const QByteArray body2 = QByteArrayLiteral("{\"seq\":2,\"stationId\":10086}");

    // 两个完整包连在一起一次 feed（粘包）
    const QByteArray stream =
        cp::PacketAssembler::pack(static_cast<quint16>(cp::MsgType::kHeartbeat), body1)
        + cp::PacketAssembler::pack(
              static_cast<quint16>(cp::MsgType::kStationQuery), body2);
    asm_.feed(stream);

    quint16 msgType = 0xFFFF;
    QByteArray outBody;
    QVERIFY(asm_.nextPacket(msgType, outBody));
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kHeartbeat));
    QCOMPARE(outBody, body1);

    QVERIFY(asm_.nextPacket(msgType, outBody));
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kStationQuery));
    QCOMPARE(outBody, body2);

    QVERIFY(!asm_.nextPacket(msgType, outBody));
}

void TestPacketAssembler::partialPacketTwoFeeds()
{
    cp::PacketAssembler asm_;
    const QByteArray body = QByteArrayLiteral(
        "{\"orderId\":\"A10001\",\"kwh\":12.34,\"amount\":198.50}");
    const QByteArray full = cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kOrderReport), body);

    // 第一次 feed 只给前半截（半包：帧头都不完整）
    const int half = full.size() / 2;
    asm_.feed(full.left(half));

    quint16 msgType = 0xFFFF;
    QByteArray outBody;
    QVERIFY(!asm_.nextPacket(msgType, outBody)); // 半包：等待更多数据

    // 第二次 feed 补上剩余字节，应当能解出完整包
    asm_.feed(full.mid(half));
    QVERIFY(asm_.nextPacket(msgType, outBody));
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kOrderReport));
    QCOMPARE(outBody, body);
    QVERIFY(!asm_.nextPacket(msgType, outBody));
}

void TestPacketAssembler::dirtyDataResync()
{
    cp::PacketAssembler asm_;
    const QByteArray body = QByteArrayLiteral("{\"alive\":true}");
    const QByteArray frame = cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kHeartbeat), body);

    // 开头垃圾：含一个假 'C'(0x43) 但不是 'P' 跟随，以及两个真包之间穿插垃圾
    const QByteArray garbage1 = QByteArray::fromHex("a1ff43a2001100");
    const QByteArray garbage2 = QByteArray::fromHex("de9f");
    asm_.feed(garbage1 + frame + garbage2 + frame);

    quint16 msgType = 0xFFFF;
    QByteArray outBody;
    QVERIFY(asm_.nextPacket(msgType, outBody)); // 跳过 garbage1 自动对齐
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kHeartbeat));
    QCOMPARE(outBody, body);

    QVERIFY(asm_.nextPacket(msgType, outBody)); // 跳过 garbage2 再次对齐
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kHeartbeat));
    QCOMPARE(outBody, body);
    QVERIFY(!asm_.nextPacket(msgType, outBody));
}

void TestPacketAssembler::oversizedBodyDiscarded()
{
    cp::PacketAssembler asm_;
    const QByteArray body = QByteArrayLiteral("ok");
    const QByteArray frame = cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kStationQuery), body);

    // 伪造一个声称 BodyLen 超上限的伪帧头（Magic 合法，随后的垃圾会干扰对齐）
    QByteArray bogus;
    bogus.append(char(cp::kPacketMagic0));
    bogus.append(char(cp::kPacketMagic1));
    bogus.append(char(0x00)); // type 高位
    bogus.append(char(0x01)); // type 低位
    bogus.append(char(0xFF)); // bodyLen = 0xFFFFFFFF > 上限
    bogus.append(char(0xFF));
    bogus.append(char(0xFF));
    bogus.append(char(0xFF));

    asm_.feed(bogus + frame);

    quint16 msgType = 0xFFFF;
    QByteArray outBody;
    QVERIFY(asm_.nextPacket(msgType, outBody)); // 伪帧头被丢弃，滑到真实包
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kStationQuery));
    QCOMPARE(outBody, body);
    QVERIFY(!asm_.nextPacket(msgType, outBody));
}

void TestPacketAssembler::clearResetsBuffer()
{
    cp::PacketAssembler asm_;
    const QByteArray body = QByteArrayLiteral("{\"again\":1}");
    const QByteArray frame = cp::PacketAssembler::pack(
        static_cast<quint16>(cp::MsgType::kHeartbeat), body);

    // 先灌入残留半包，clear 后不应影响下一次连接的数据
    asm_.feed(frame.left(4));
    asm_.clear();

    asm_.feed(frame);
    quint16 msgType = 0xFFFF;
    QByteArray outBody;
    QVERIFY(asm_.nextPacket(msgType, outBody));
    QCOMPARE(msgType, static_cast<quint16>(cp::MsgType::kHeartbeat));
    QCOMPARE(outBody, body);
}

QTEST_APPLESS_MAIN(TestPacketAssembler)
#include "tst_packet_assembler.moc"
