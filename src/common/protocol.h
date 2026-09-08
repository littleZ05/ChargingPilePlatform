#ifndef CP_PROTOCOL_H
#define CP_PROTOCOL_H

/**
 * protocol.h：应用层安全通信数据结构（NO.15，维护人：陈庚泉）
 *
 * 与 PacketAssembler（吴羽桐，NO.19）的分工：
 * - PacketAssembler 负责底层二进制帧 [Magic|MsgType|BodyLen|Body]；
 * - 本文件定义 Body 内版本化 JSON Envelope（消息体契约）：
 *   {
 *     "ver": 1,
 *     "kind": "req|resp|event",
 *     "seq": 1,
 *     "ts": 1720000000000,
 *     "role": "user|operator|admin|system",
 *     "uid": "13800138000",
 *     "token": "",
 *     "code": 0,
 *     "msg": "",
 *     "data": {}
 *   }
 * - 本文件还提供统一输入校验与消息级访问控制，保证参数与权限在入口统一收敛。
 */
#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>

#include "common.h"

namespace cp {

inline constexpr int kProtocolVersion = 1;

/** Body 最大长度（与 PacketAssembler 的上限分开，应用层更严格） */
inline constexpr int kProtocolMaxJsonBytes = 64 * 1024;

/** Envelope 各文本字段长度上限（防止恶意超大字段） */
inline constexpr int kMaxRoleLength   = 16;
inline constexpr int kMaxUidLength    = 64;
inline constexpr int kMaxTokenLength  = 128;
inline constexpr int kMaxMessageLen   = 512;
inline constexpr int kMaxDataKeys     = 128;

namespace ErrCode {
inline constexpr int kOk            = 0;
inline constexpr int kBadRequest    = 400;
inline constexpr int kUnauthorized  = 401;
inline constexpr int kForbidden     = 403;
inline constexpr int kNotFound      = 404;
inline constexpr int kConflict      = 409;
inline constexpr int kValidation    = 422;
inline constexpr int kTooMany       = 429;
inline constexpr int kServerError   = 500;
inline constexpr int kProtocolError = 1001;
inline constexpr int kDbError       = 2001;
}

inline bool isValidErrorCode(int code)
{
    switch (code) {
    case ErrCode::kOk:
    case ErrCode::kBadRequest:
    case ErrCode::kUnauthorized:
    case ErrCode::kForbidden:
    case ErrCode::kNotFound:
    case ErrCode::kConflict:
    case ErrCode::kValidation:
    case ErrCode::kTooMany:
    case ErrCode::kServerError:
    case ErrCode::kProtocolError:
    case ErrCode::kDbError:
        return true;
    default:
        return false;
    }
}

inline QString errorText(int code)
{
    switch (code) {
    case ErrCode::kOk:            return QStringLiteral("成功");
    case ErrCode::kBadRequest:    return QStringLiteral("请求格式错误");
    case ErrCode::kUnauthorized:  return QStringLiteral("未认证");
    case ErrCode::kForbidden:     return QStringLiteral("无权限");
    case ErrCode::kNotFound:      return QStringLiteral("资源不存在");
    case ErrCode::kConflict:      return QStringLiteral("状态冲突");
    case ErrCode::kValidation:    return QStringLiteral("参数校验失败");
    case ErrCode::kTooMany:       return QStringLiteral("请求过于频繁");
    case ErrCode::kServerError:   return QStringLiteral("服务器内部错误");
    case ErrCode::kProtocolError: return QStringLiteral("协议解析失败");
    case ErrCode::kDbError:       return QStringLiteral("数据库错误");
    default:                      return QStringLiteral("未知错误");
    }
}

/** 消息用途：请求 / 响应 / 主动事件 */
enum class MessageKind {
    Request = 0,
    Response = 1,
    Event = 2
};

inline QString kindToText(MessageKind kind)
{
    switch (kind) {
    case MessageKind::Request:  return QStringLiteral("req");
    case MessageKind::Response: return QStringLiteral("resp");
    case MessageKind::Event:    return QStringLiteral("event");
    }
    return QString();
}

inline bool kindFromText(const QString &text, MessageKind *kind)
{
    if (text == QStringLiteral("req")) {
        *kind = MessageKind::Request;
        return true;
    }
    if (text == QStringLiteral("resp")) {
        *kind = MessageKind::Response;
        return true;
    }
    if (text == QStringLiteral("event")) {
        *kind = MessageKind::Event;
        return true;
    }
    return false;
}

/** 权限角色：Unknown 表示未通过校验；System 供电桩/后台自动服务使用 */
enum class Role {
    Unknown = 0,
    Guest = 1,
    User = 2,
    Operator = 3,
    Admin = 4,
    System = 5
};

inline QString roleToText(Role role)
{
    switch (role) {
    case Role::Guest:    return QStringLiteral("guest");
    case Role::User:     return QStringLiteral("user");
    case Role::Operator: return QStringLiteral("operator");
    case Role::Admin:    return QStringLiteral("admin");
    case Role::System:   return QStringLiteral("system");
    case Role::Unknown:  return QString();
    }
    return QString();
}

inline Role roleFromText(const QString &text)
{
    if (text == QStringLiteral("guest"))    return Role::Guest;
    if (text == QStringLiteral("user"))     return Role::User;
    if (text == QStringLiteral("operator")) return Role::Operator;
    if (text == QStringLiteral("admin"))    return Role::Admin;
    if (text == QStringLiteral("system"))   return Role::System;
    return Role::Unknown;
}

/** 集中参数校验（NO.15；与 DB CHECK 保持一致，先于落库拦截） */
namespace Validate {

inline bool digitOnly(const QString &s)
{
    for (const QChar c : s) {
        if (!c.isDigit())
            return false;
    }
    return true;
}

inline bool phone11(const QString &s)
{
    return s.size() == 11 && digitOnly(s);
}

inline bool textLength(const QString &s, int min, int max)
{
    return s.size() >= min && s.size() <= max;
}

inline bool inRange(double v, double min, double max)
{
    return v >= min && v <= max;
}

inline bool nonNegative(double v)
{
    return v >= 0.0;
}

inline bool positiveId(qint64 id)
{
    return id > 0;
}

inline bool pileState(PileState s)
{
    return s == PileState::Idle || s == PileState::Charging || s == PileState::Fault;
}

inline bool orderState(OrderState s)
{
    return s == OrderState::Charging || s == OrderState::Finished
           || s == OrderState::Canceled;
}

} // namespace Validate

/** 应用层消息 Envelope */
struct Envelope
{
    int         version = kProtocolVersion;
    MessageKind kind = MessageKind::Request;
    quint32     seq = 0;
    qint64      timestampMs = 0;
    Role        role = Role::Unknown;
    QString     uid;
    QString     token;
    int         code = 0;
    QString     message;
    QJsonObject data;
};

inline bool validateEnvelope(const Envelope &e, QString *error = nullptr)
{
    const QString fail = [&]() -> QString {
        if (e.version != kProtocolVersion)
            return QStringLiteral("不支持的协议版本");
        if (kindToText(e.kind).isEmpty())
            return QStringLiteral("kind 非法");
        if (e.seq == 0)
            return QStringLiteral("seq 必须大于 0");
        if (e.timestampMs < 0)
            return QStringLiteral("时间戳非法");
        if (e.role == Role::Unknown || roleToText(e.role).size() > kMaxRoleLength)
            return QStringLiteral("role 非法");
        if (e.uid.size() > kMaxUidLength)
            return QStringLiteral("uid 超长");
        if (e.token.size() > kMaxTokenLength)
            return QStringLiteral("token 超长");
        if (!isValidErrorCode(e.code))
            return QStringLiteral("错误码非法");
        if (e.message.size() > kMaxMessageLen)
            return QStringLiteral("msg 超长");
        if (e.data.size() > kMaxDataKeys)
            return QStringLiteral("data 字段过多");
        return QString();
    }();
    if (!fail.isEmpty()) {
        if (error) *error = fail;
        return false;
    }
    return true;
}

inline bool serializeEnvelope(const Envelope &e, QByteArray *out, QString *error = nullptr)
{
    QString err;
    if (!validateEnvelope(e, &err)) {
        if (error) *error = err;
        return false;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("ver"), e.version);
    obj.insert(QStringLiteral("kind"), kindToText(e.kind));
    obj.insert(QStringLiteral("seq"), static_cast<qint64>(e.seq));
    obj.insert(QStringLiteral("ts"), e.timestampMs);
    obj.insert(QStringLiteral("role"), roleToText(e.role));
    obj.insert(QStringLiteral("uid"), e.uid);
    obj.insert(QStringLiteral("token"), e.token);
    obj.insert(QStringLiteral("code"), e.code);
    obj.insert(QStringLiteral("msg"), e.message);
    obj.insert(QStringLiteral("data"), e.data);

    const QByteArray json =
        QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (json.size() > kProtocolMaxJsonBytes) {
        if (error) *error = QStringLiteral("消息体超过 %1 字节上限").arg(kProtocolMaxJsonBytes);
        return false;
    }
    *out = json;
    return true;
}

inline bool deserializeEnvelope(const QByteArray &in, Envelope *out, QString *error = nullptr)
{
    if (in.size() > kProtocolMaxJsonBytes) {
        if (error) *error = QStringLiteral("消息体超过 %1 字节上限").arg(kProtocolMaxJsonBytes);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(in, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("消息体必须是 JSON 对象");
        return false;
    }

    const QJsonObject obj = doc.object();
    Envelope e;
    const auto integerField = [](const QJsonValue &v, qint64 min, qint64 max,
                                 qint64 *result) -> bool {
        if (!v.isDouble())
            return false;
        const double d = v.toDouble();
        if (d < static_cast<double>(min) || d > static_cast<double>(max)
            || d != static_cast<qint64>(d))
            return false;
        *result = static_cast<qint64>(d);
        return true;
    };

    qint64 ver = 0;
    if (!integerField(obj.value(QStringLiteral("ver")), kProtocolVersion, kProtocolVersion, &ver)) {
        if (error) *error = QStringLiteral("ver 必须为 1");
        return false;
    }
    e.version = static_cast<int>(ver);

    const QString kindText = obj.value(QStringLiteral("kind")).toString();
    if (!kindFromText(kindText, &e.kind)) {
        if (error) *error = QStringLiteral("kind 非法");
        return false;
    }

    qint64 seq = 0;
    if (!integerField(obj.value(QStringLiteral("seq")), 1, 0xFFFFFFFFLL, &seq)) {
        if (error) *error = QStringLiteral("seq 必须在 1 ~ 4294967295 之间");
        return false;
    }
    e.seq = static_cast<quint32>(seq);

    qint64 ts = -1;
    if (!integerField(obj.value(QStringLiteral("ts")), 0, Q_INT64_C(4102444800000), &ts)) {
        if (error) *error = QStringLiteral("ts 非法");
        return false;
    }
    e.timestampMs = ts;

    e.role = roleFromText(obj.value(QStringLiteral("role")).toString());
    if (e.role == Role::Unknown) {
        if (error) *error = QStringLiteral("role 非法");
        return false;
    }
    e.uid = obj.value(QStringLiteral("uid")).toString();
    e.token = obj.value(QStringLiteral("token")).toString();

    qint64 code = -1;
    if (!integerField(obj.value(QStringLiteral("code")), 0, 9999, &code)
        || !isValidErrorCode(static_cast<int>(code))) {
        if (error) *error = QStringLiteral("code 非法");
        return false;
    }
    e.code = static_cast<int>(code);
    e.message = obj.value(QStringLiteral("msg")).toString();

    const QJsonValue dataVal = obj.value(QStringLiteral("data"));
    if (!dataVal.isObject()) {
        if (error) *error = QStringLiteral("data 必须是对象");
        return false;
    }
    e.data = dataVal.toObject();

    QString err;
    if (!validateEnvelope(e, &err)) {
        if (error) *error = err;
        return false;
    }
    *out = e;
    return true;
}

inline Envelope makeRequest(quint32 seq, Role role, const QString &uid,
                            const QJsonObject &data,
                            qint64 timestampMs = QDateTime::currentMSecsSinceEpoch())
{
    Envelope e;
    e.version = kProtocolVersion;
    e.kind = MessageKind::Request;
    e.seq = seq;
    e.timestampMs = timestampMs;
    e.role = role;
    e.uid = uid;
    e.data = data;
    return e;
}

inline Envelope makeResponse(const Envelope &request, int code,
                             const QJsonObject &data = {},
                             const QString &message = {},
                             qint64 timestampMs = QDateTime::currentMSecsSinceEpoch())
{
    Envelope e;
    e.version = kProtocolVersion;
    e.kind = MessageKind::Response;
    e.seq = request.seq;
    e.timestampMs = timestampMs;
    e.role = request.role;
    e.uid = request.uid;
    e.token = request.token;
    e.code = code;
    e.message = message.isEmpty() && code != ErrCode::kOk
                    ? errorText(code)
                    : message;
    e.data = data;
    return e;
}

/** 消息类型白名单：未知类型拒绝接入（防止协议被滥用） */
inline bool isKnownMessageType(int type)
{
    switch (type) {
    case MsgType::kHeartbeat:
    case MsgType::kStationQuery:
    case MsgType::kOrderReport:
    case MsgType::kPileStateReport:
    case MsgType::kStartCharge:
    case MsgType::kStopCharge:
    case MsgType::kProfileQuery:
    case MsgType::kProfileUpdate:
    case MsgType::kLoginRequest:
    case MsgType::kRegisterRequest:
    case MsgType::kPileManageRequest:
    case MsgType::kSalesQueryRequest:
    case MsgType::kUserManageRequest:
    case MsgType::kStationManageRequest:
    case MsgType::kAdminQueryRequest:
    case MsgType::kHeartbeatResponse:
    case MsgType::kStationQueryResponse:
    case MsgType::kOrderReportResponse:
    case MsgType::kPileStateReportResponse:
    case MsgType::kStartChargeResponse:
    case MsgType::kStopChargeResponse:
    case MsgType::kProfileQueryResponse:
    case MsgType::kProfileUpdateResponse:
    case MsgType::kLoginResponse:
    case MsgType::kRegisterResponse:
    case MsgType::kPileManageResponse:
    case MsgType::kSalesQueryResponse:
    case MsgType::kUserManageResponse:
    case MsgType::kStationManageResponse:
    case MsgType::kAdminQueryResponse:
    case MsgType::kErrorResponse:
        return true;
    default:
        return false;
    }
}

/** 消息所需最低权限（访问控制矩阵） */
enum class AccessLevel {
    Public = 1,   // 心跳/登录/注册
    User = 2,     // 普通用户业务
    Operator = 3  // 服务端管理/销售/用户/电站/桩管理
};

inline AccessLevel messageAccessLevel(int type)
{
    switch (type) {
    case MsgType::kHeartbeat:
    case MsgType::kHeartbeatResponse:
    case MsgType::kLoginRequest:
    case MsgType::kLoginResponse:
    case MsgType::kRegisterRequest:
    case MsgType::kRegisterResponse:
        return AccessLevel::Public;
    case MsgType::kStationQuery:
    case MsgType::kStationQueryResponse:
    case MsgType::kOrderReport:
    case MsgType::kOrderReportResponse:
    case MsgType::kStartCharge:
    case MsgType::kStartChargeResponse:
    case MsgType::kStopCharge:
    case MsgType::kStopChargeResponse:
    case MsgType::kProfileQuery:
    case MsgType::kProfileQueryResponse:
    case MsgType::kProfileUpdate:
    case MsgType::kProfileUpdateResponse:
        return AccessLevel::User;
    default:
        return AccessLevel::Operator; // 管理类消息与电桩状态上报
    }
}

inline bool hasMessageAccess(Role role, int type)
{
    if (role == Role::Unknown || !isKnownMessageType(type))
        return false;
    const AccessLevel level = messageAccessLevel(type);
    switch (level) {
    case AccessLevel::Public:
        return true;
    case AccessLevel::User:
        return role == Role::User || role == Role::Operator
               || role == Role::Admin || role == Role::System;
    case AccessLevel::Operator:
        return role == Role::Operator || role == Role::Admin
               || role == Role::System;
    }
    return false;
}

} // namespace cp

#endif // CP_PROTOCOL_H
