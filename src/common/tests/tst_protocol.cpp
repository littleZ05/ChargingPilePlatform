#include <QtTest/QtTest>

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "../protocol.h"

using namespace cp;

class TstProtocol : public QObject
{
    Q_OBJECT

private slots:
    void roundTripRequestEnvelope();
    void roundTripErrorResponse();
    void rejectMalformedJson();
    void rejectWrongShape();
    void validateSeqAndVersion();
    void validateRoleUidTokenAndKind();
    void validateCodeAndData();
    void rejectOversizeMessage();
    void knownMessageTypeWhitelist();
    void roleAccessMatrix_data();
    void roleAccessMatrix();
    void centralValidators();
};

void TstProtocol::roundTripRequestEnvelope()
{
    QJsonObject data;
    data.insert(QStringLiteral("lon"), 123.49584);
    data.insert(QStringLiteral("lat"), 41.71517);
    data.insert(QStringLiteral("radius_km"), 5);

    const Envelope request = makeRequest(7, Role::User, QStringLiteral("13800138000"), data, 1234567);
    QByteArray json;
    QString error;
    QVERIFY2(serializeEnvelope(request, &json, &error), qPrintable(error));

    Envelope parsed;
    QVERIFY2(deserializeEnvelope(json, &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.version, kProtocolVersion);
    QCOMPARE(parsed.kind, MessageKind::Request);
    QCOMPARE(parsed.seq, quint32(7));
    QCOMPARE(parsed.timestampMs, qint64(1234567));
    QCOMPARE(parsed.role, Role::User);
    QCOMPARE(parsed.uid, QStringLiteral("13800138000"));
    QCOMPARE(parsed.code, 0);
    QCOMPARE(parsed.data.value(QStringLiteral("lon")).toDouble(), 123.49584);
    QCOMPARE(parsed.data.value(QStringLiteral("radius_km")).toInt(), 5);
}

void TstProtocol::roundTripErrorResponse()
{
    Envelope request = makeRequest(9, Role::Operator, QStringLiteral("admin"), {});
    const Envelope response = makeResponse(
        request, ErrCode::kForbidden,
        QJsonObject{ { QStringLiteral("detail"), QStringLiteral("需要更高权限") } });

    QByteArray json;
    QString error;
    QVERIFY2(serializeEnvelope(response, &json, &error), qPrintable(error));

    Envelope parsed;
    QVERIFY2(deserializeEnvelope(json, &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.kind, MessageKind::Response);
    QCOMPARE(parsed.seq, quint32(9));
    QCOMPARE(parsed.role, Role::Operator);
    QCOMPARE(parsed.code, ErrCode::kForbidden);
    QVERIFY(parsed.message.contains(QStringLiteral("无权限")));
}

void TstProtocol::rejectMalformedJson()
{
    QString error;
    Envelope out;
    QVERIFY(!deserializeEnvelope(QByteArrayLiteral("{bad json"), &out, &error));
    QVERIFY(!error.isEmpty());
}

void TstProtocol::rejectWrongShape()
{
    QString error;
    Envelope out;
    QVERIFY(!deserializeEnvelope(QByteArrayLiteral("[1,2,3]"), &out, &error));
    QVERIFY(error.contains(QStringLiteral("JSON 对象")));

    // data 必须是对象
    const QByteArray arrayData = QByteArrayLiteral(
        "{\"ver\":1,\"kind\":\"req\",\"seq\":1,\"ts\":1,"
        "\"role\":\"user\",\"uid\":\"u1\",\"code\":0,\"msg\":\"\",\"data\":[1,2]}");
    QVERIFY(!deserializeEnvelope(arrayData, &out, &error));
    QVERIFY(error.contains(QStringLiteral("data 必须是对象")));
}

void TstProtocol::validateSeqAndVersion()
{
    QString error;
    Envelope out;
    const QString head = QStringLiteral(
        "{\"ver\":1,\"kind\":\"req\",\"seq\":%1,\"ts\":1,"
        "\"role\":\"user\",\"uid\":\"u1\",\"code\":0,\"msg\":\"\",\"data\":{}}");

    // seq=0 必须拒绝
    QVERIFY(!deserializeEnvelope(head.arg(0).toUtf8(), &out, &error));
    QVERIFY(error.contains(QStringLiteral("seq")));

    // 版本 2 必须拒绝
    QByteArray badVer = head.arg(1).toUtf8();
    badVer.replace("{\"ver\":1", "{\"ver\":2");
    QVERIFY(!deserializeEnvelope(badVer, &out, &error));
    QVERIFY(error.contains(QStringLiteral("ver")));

    // 上限 seq 应合法
    QVERIFY2(deserializeEnvelope(head.arg(4294967295LL).toUtf8(), &out, &error),
             qPrintable(error));
    QCOMPARE(out.seq, quint32(0xFFFFFFFFu));
}

void TstProtocol::validateRoleUidTokenAndKind()
{
    QString error;
    Envelope out;
    const auto make = [](const QString &key, const QString &value) {
        return QStringLiteral(
                   "{\"ver\":1,\"kind\":\"req\",\"seq\":1,\"ts\":1,"
                   "\"role\":\"user\",\"uid\":\"u1\",\"token\":\"\","
                   "\"code\":0,\"msg\":\"\",\"data\":{},\"%1\":%2}")
            .arg(key, value)
            .toUtf8();
    };

    // 非法 kind
    QByteArray badKind = make(QStringLiteral("kind"), QStringLiteral("\"xxx\""));
    QVERIFY(!deserializeEnvelope(badKind, &out, &error));
    QVERIFY(error.contains(QStringLiteral("kind")));

    // 非法 role
    QByteArray badRole = make(QStringLiteral("role"), QStringLiteral("\"root\""));
    QVERIFY(!deserializeEnvelope(badRole, &out, &error));
    QVERIFY(error.contains(QStringLiteral("role")));

    // uid 超长
    QByteArray longUid = make(QStringLiteral("uid"),
                              QStringLiteral("\"%1\"").arg(QString(kMaxUidLength + 1, QLatin1Char('x'))));
    QVERIFY(!deserializeEnvelope(longUid, &out, &error));
    QVERIFY(error.contains(QStringLiteral("uid")));

    // token 超长
    QByteArray longToken = make(QStringLiteral("token"),
                                QStringLiteral("\"%1\"").arg(QString(kMaxTokenLength + 1, QLatin1Char('t'))));
    QVERIFY(!deserializeEnvelope(longToken, &out, &error));
    QVERIFY(error.contains(QStringLiteral("token")));
}

void TstProtocol::validateCodeAndData()
{
    QString error;
    Envelope out;
    QByteArray body = QByteArrayLiteral(
        "{\"ver\":1,\"kind\":\"resp\",\"seq\":2,\"ts\":1,"
        "\"role\":\"user\",\"uid\":\"u1\",\"code\":123,\"msg\":\"\",\"data\":{}}");
    QVERIFY(!deserializeEnvelope(body, &out, &error));
    QVERIFY(error.contains(QStringLiteral("code")));
}

void TstProtocol::rejectOversizeMessage()
{
    QString error;
    Envelope out;

    QJsonObject huge;
    huge.insert(QStringLiteral("x"), QString(kProtocolMaxJsonBytes + 1024, QLatin1Char('a')));
    Envelope e = makeRequest(1, Role::User, QStringLiteral("u1"), huge);
    QByteArray json;
    QVERIFY(!serializeEnvelope(e, &json, &error));
    QVERIFY(error.contains(QStringLiteral("上限")));

    QByteArray oversized = QByteArray(kProtocolMaxJsonBytes + 1, 'a');
    QVERIFY(!deserializeEnvelope(oversized, &out, &error));
    QVERIFY(error.contains(QStringLiteral("上限")));
}

void TstProtocol::knownMessageTypeWhitelist()
{
    QVERIFY(isKnownMessageType(MsgType::kHeartbeat));
    QVERIFY(isKnownMessageType(MsgType::kStationQuery));
    QVERIFY(isKnownMessageType(MsgType::kSalesQueryRequest));
    QVERIFY(isKnownMessageType(MsgType::kStationManageResponse));
    QVERIFY(!isKnownMessageType(999));
}

void TstProtocol::roleAccessMatrix_data()
{
    QTest::addColumn<Role>("role");
    QTest::addColumn<int>("msgType");
    QTest::addColumn<bool>("allowed");

    QTest::newRow("guest 心跳") << Role::Guest << MsgType::kHeartbeat << true;
    QTest::newRow("guest 查站") << Role::Guest << MsgType::kStationQuery << false;
    QTest::newRow("user 查站") << Role::User << MsgType::kStationQuery << true;
    QTest::newRow("user 订单上报") << Role::User << MsgType::kOrderReport << true;
    QTest::newRow("user 销售查询") << Role::User << MsgType::kSalesQueryRequest << false;
    QTest::newRow("operator 销售查询") << Role::Operator << MsgType::kSalesQueryRequest << true;
    QTest::newRow("operator 桩管理") << Role::Operator << MsgType::kPileManageRequest << true;
    QTest::newRow("admin 站管理") << Role::Admin << MsgType::kStationManageRequest << true;
    QTest::newRow("system 桩状态上报") << Role::System << MsgType::kPileStateReport << true;
    QTest::newRow("unknown 心跳") << Role::Unknown << MsgType::kHeartbeat << false;
}

void TstProtocol::roleAccessMatrix()
{
    QFETCH(Role, role);
    QFETCH(int, msgType);
    QFETCH(bool, allowed);

    QCOMPARE(hasMessageAccess(role, msgType), allowed);
}

void TstProtocol::centralValidators()
{
    QVERIFY(Validate::phone11(QStringLiteral("13800138000")));
    QVERIFY(!Validate::phone11(QStringLiteral("1380013800a")));
    QVERIFY(!Validate::phone11(QStringLiteral("1380013800")));
    QVERIFY(Validate::textLength(QStringLiteral("abc"), 1, 8));
    QVERIFY(!Validate::textLength(QStringLiteral("abcdefghij"), 1, 8));
    QVERIFY(Validate::inRange(123.4, -180.0, 180.0));
    QVERIFY(!Validate::inRange(181.0, -180.0, 180.0));
    QVERIFY(Validate::nonNegative(0.0));
    QVERIFY(!Validate::nonNegative(-0.01));
    QVERIFY(Validate::positiveId(1));
    QVERIFY(Validate::pileState(PileState::Charging));
    QVERIFY(Validate::orderState(OrderState::Finished));
    QVERIFY(errorText(ErrCode::kForbidden).contains(QStringLiteral("无权限")));
}

QTEST_MAIN(TstProtocol)

#include "tst_protocol.moc"
