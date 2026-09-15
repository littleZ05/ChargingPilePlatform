#include "pcserver_session.h"
#include "common.h"
#include "net_client.h"
#include <QJsonDocument>
#include <QSettings>
#include <QTimer>
#include <QUuid>

namespace userclient {
QString PcServerSession::commandKey(int type) const
{
    return QStringLiteral("business/%1/%2/%3/%4").arg(m_host).arg(m_port).arg(m_phone).arg(type);
}

bool PcServerSession::command(int type, QJsonObject request)
{
    if (!isConnected() || !m_authenticated || m_inflight.contains(type))
        return false;
    const bool writing = type != cp::MsgType::kProfileQuery;
    if (writing && m_commands.contains(type)) {
        QJsonObject previous = m_commands.value(type);
        previous.remove("request_id");
        if (previous != request)
            return false; // Resolve an uncertain write before starting another.
        request = m_commands.value(type);
    } else {
        request.insert("request_id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    const QByteArray bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (writing) {
        QSettings settings(QStringLiteral("ChargingPilePlatform"),QStringLiteral("UserClient"));
        settings.setValue(commandKey(type), bytes);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return false;
        m_commands.insert(type, request);
    }
    const QString id = request.value("request_id").toString();
    m_inflight.insert(type, id);
    if (!m_netClient->sendPacket(type, bytes)) {
        m_inflight.remove(type);
        return false;
    }
    QTimer::singleShot(10000, this, [this, type, id] {
        if (m_inflight.value(type) != id)
            return;
        m_inflight.remove(type);
        const QJsonObject error{{"code", 408}, {"request_id", id},
            {"message", QStringLiteral("服务器尚未确认，请重连或重试；系统会核实结果，避免重复扣款")}};
        emit businessResult(type, error);
    });
    return true;
}

void PcServerSession::restoreCommands()
{
    m_commands.clear();
    const int types[] = {cp::MsgType::kStartCharge, cp::MsgType::kOrderReport,
                         cp::MsgType::kStopCharge, cp::MsgType::kRechargeRequest,
                         cp::MsgType::kProfileUpdate};
    QSettings settings(QStringLiteral("ChargingPilePlatform"),QStringLiteral("UserClient"));
    for (int type : types) {
        QJsonObject r = QJsonDocument::fromJson(settings.value(commandKey(type)).toByteArray()).object();
        if (r.isEmpty())
            continue;
        m_commands.insert(type, r);
        r.remove("request_id");
        command(type, r);
    }
}

bool PcServerSession::receiveBusiness(int type, const QByteArray &body)
{
    if (type != cp::MsgType::kStartCharge && type != cp::MsgType::kOrderReport
        && type != cp::MsgType::kStopCharge && type != cp::MsgType::kRechargeRequest
        && type != cp::MsgType::kProfileQuery && type != cp::MsgType::kProfileUpdate)
        return false;
    const QJsonObject r = QJsonDocument::fromJson(body).object();
    const QString id = r.value("request_id").toString();
    const QString expected = m_inflight.value(type, m_commands.value(type).value("request_id").toString());
    if (id.isEmpty() || id != expected)
        return true; // Late/unrelated responses cannot mutate the visible order.
    m_inflight.remove(type);
    const int code = r.value("code").toInt(-1);
    if (code >= 0 && code < 500) {
        m_commands.remove(type);
        QSettings(QStringLiteral("ChargingPilePlatform"),QStringLiteral("UserClient")).remove(commandKey(type));
    }
    const QString message = r.value("message").toString();
    emit businessResult(type, r);
    if (type == cp::MsgType::kStartCharge && code != 0)
        emit startChargeResult(code, message, r.value("pile_code").toString(),
                               r.value("order_id").toInt(), r.value("unit_price").toDouble());
    if (type == cp::MsgType::kRechargeRequest)
        emit rechargeResult(code, message, r.value("amount").toDouble(), r.value("balance").toDouble());
    if (type != cp::MsgType::kProfileQuery)
        m_profileRefreshPending = true;
    if (m_profileRefreshPending && isConnected()
        && !m_inflight.contains(cp::MsgType::kProfileQuery)) {
        m_profileRefreshPending = false;
        command(cp::MsgType::kProfileQuery);
    }
    return true;
}

void PcServerSession::logout()
{
    m_phone.clear();
    m_authenticated = false;
    m_profileRefreshPending = false;
    m_commands.clear();
    m_inflight.clear();
    m_loginPending = false;
    m_netClient->disconnectFromServer();
}
}
