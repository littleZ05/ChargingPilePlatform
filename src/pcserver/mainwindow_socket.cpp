#include "mainwindow_p.h"
using namespace pcserver_admin;
using namespace pcserver_ui;
void MainWindow::startSocketServer()
{
    d->netServer = new cp::NetServer(this);
    connect(d->netServer, &cp::NetServer::packetReceived,
            this, &MainWindow::handleSocketPacket);

    connect(d->netServer, &cp::NetServer::clientDisconnected, this,
            [this](QTcpSocket *client) { m_sessionPhones.remove(client); });
    const int configuredPort = qEnvironmentVariableIntValue("PCSERVER_PORT");
    const auto port = static_cast<quint16>(configuredPort > 0 ? configuredPort : cp::kServerPort);
    d->socketStarted = d->netServer->startServer(port);
    if (d->socketStarted) {
        qInfo().noquote()
            << QStringLiteral("[net] Socket 服务已监听端口 %1").arg(port);
    } else {
        qWarning().noquote()
            << QStringLiteral("[net] Socket 服务监听端口 %1 失败").arg(port);
    }
}

void MainWindow::startDashboardServer()
{
    if (!d->store || !d->store->isOpen())
        return;

    d->dashboardServer = new pcserver::DashboardApiServer(d->store, this);
    QString error;
    if (!d->dashboardServer->start(0, &error)) {
        qWarning().noquote()
            << QStringLiteral("[dashboard] 大屏数据服务启动失败：%1").arg(error);
        return;
    }
    d->dashboardStarted = true;
    qInfo().noquote()
        << QStringLiteral(
               "[dashboard] 大屏数据服务已监听 http://127.0.0.1:%1/api/dashboard/overview")
               .arg(d->dashboardServer->port());
}

void MainWindow::sendSocketReply(QTcpSocket *client, quint16 msgType,
                                 const QJsonObject &payload)
{
    if (!client || !d->netServer) {
        qWarning() << "[net] 回包失败：服务端或客户端句柄不可用";
        return;
    }

    // NO.20 全链路错误处理：协议层所有非 0 业务码统一留痕，
    // 在「运行日志」页可直接查看（错误码 + 中文原因 + 消息类型）。
    const int code = payload.value(QStringLiteral("code")).toInt(0);
    if (code != 0) {
        pcserver::RunLog::instance().append(
            QStringLiteral("错误"),
            QStringLiteral("[协议] MsgType=%1 code=%2 %3")
                .arg(msgType)
                .arg(code)
                .arg(payload.value(QStringLiteral("message")).toString()));
    }

    if (!d->netServer->sendPacket(client, msgType, socketJsonCompact(payload))) {
        qWarning() << "[net] 回包发送失败 msgType=" << msgType;
    }
}

void MainWindow::handleSocketPacket(QTcpSocket *client, quint16 msgType,
                                    const QByteArray &body)
{
    switch (msgType) {
    case static_cast<quint16>(cp::MsgType::kHeartbeat):
        handleHeartbeatPacket(client, body);
        break;
    case static_cast<quint16>(cp::MsgType::kStationQuery):
        handleStationQueryPacket(client, body);
        break;
    case cp::MsgType::kLoginRequest:
        handleUserLoginPacket(client, body);
        break;
    case cp::MsgType::kOrderReport:
    case cp::MsgType::kStopCharge:
    case cp::MsgType::kStartCharge:
    case cp::MsgType::kRechargeRequest:
    case cp::MsgType::kProfileQuery:
    case cp::MsgType::kProfileUpdate: {
        QJsonObject request;
        if (!parseSocketJsonObject(body, &request)) {
            sendSocketReply(client, msgType, socketResponseEnvelope(400, QStringLiteral("请求必须是JSON对象")));
            break;
        }
        pcserver::ChargeService service(d->store);
        sendSocketReply(client, msgType, service.execute(msgType, m_sessionPhones.value(client), request));
        refreshAll();
        break;
    }
    default:
        sendSocketReply(client, msgType, socketResponseEnvelope(400, QStringLiteral("不支持的消息类型")));
        break;
    }
}

void MainWindow::handleHeartbeatPacket(QTcpSocket *client, const QByteArray &body)
{
    QJsonObject request;
    QJsonObject response;
    if (!parseSocketJsonObject(body, &request)) {
        response = socketResponseEnvelope(
            400, QStringLiteral("心跳负载必须是 JSON 对象"));
    } else {
        response = socketResponseEnvelope(0, QStringLiteral("pong"));
        const QJsonValue clientId = request.value(QStringLiteral("client_id"));
        if (clientId.isString()) {
            response.insert(QStringLiteral("client_id"), clientId.toString());
        }
        const QJsonValue ts = request.value(QStringLiteral("ts"));
        if (ts.isDouble()) {
            response.insert(QStringLiteral("ts"), ts.toDouble());
        }
    }
    sendSocketReply(client, static_cast<quint16>(cp::MsgType::kHeartbeat),
                    response);
}


void MainWindow::handleUserLoginPacket(QTcpSocket *client,
                                       const QByteArray &body)
{
    const auto msgType = static_cast<quint16>(cp::MsgType::kLoginRequest);
    QJsonObject request;
    if (!parseSocketJsonObject(body, &request)) {
        sendSocketReply(client, msgType,
                        socketResponseEnvelope(
                            400, QStringLiteral("登录负载必须是 JSON 对象")));
        return;
    }
    const QString phone =
        request.value(QStringLiteral("phone")).toString().trimmed();
    if (phone.size() != 11 || !std::all_of(phone.begin(), phone.end(),
                                           [](QChar c) { return c.isDigit(); })) {
        sendSocketReply(client, msgType,
                        socketResponseEnvelope(
                            400, QStringLiteral("手机号应为11位数字")));
        return;
    }
    if (!d->store || !d->store->isOpen()) {
        sendSocketReply(client, msgType,
                        socketResponseEnvelope(
                            503, QStringLiteral("用户服务未就绪")));
        return;
    }
    int userId = 0, status = 0;
    double balance = 0.0;
    QString nickname;
    bool created = false;
    QString err;
    if (!d->store->userLoginByPhone(phone, &userId, &nickname, &balance,
                                    &status, &created, &err)) {
        sendSocketReply(client, msgType,
                        socketResponseEnvelope(
                            500, err.isEmpty() ? QStringLiteral("登录失败") : err));
        return;
    }
    QJsonObject response = socketResponseEnvelope(
        0, created ? QStringLiteral("新用户已自动注册并登录")
                   : QStringLiteral("登录成功"));
    response.insert(QStringLiteral("user_id"), userId);
    response.insert(QStringLiteral("phone"), phone);
    response.insert(QStringLiteral("nickname"), nickname);
    response.insert(QStringLiteral("balance"), balance);
    response.insert(QStringLiteral("status"), status);
    response.insert(QStringLiteral("created"), created);
    m_sessionPhones.insert(client, phone);
    sendSocketReply(client, msgType, response);
    qInfo().noquote()
        << QStringLiteral("[net][登录] phone=%1 user=%2 created=%3")
               .arg(phone).arg(nickname).arg(created);
}

void MainWindow::handleStationQueryPacket(QTcpSocket *client,
                                          const QByteArray &body)
{
    const auto msgType = static_cast<quint16>(cp::MsgType::kStationQuery);
    if (!d->store || !d->store->isOpen()) {
        sendSocketReply(client, msgType,
                        socketResponseEnvelope(
                            503, QStringLiteral("电站数据服务未就绪")));
        return;
    }

    QJsonObject request;
    if (!parseSocketJsonObject(body, &request)) {
        sendSocketReply(client, msgType,
                        socketResponseEnvelope(
                            400, QStringLiteral("电站查询负载必须是 JSON 对象")));
        return;
    }

    const QJsonValue stationValue = request.value(QStringLiteral("station_id"));
    const int stationId = stationValue.isDouble() ? stationValue.toInt() : 0;
    int limit = request.value(QStringLiteral("limit")).toInt(100);
    limit = qBound(1, limit, 200);

    QVector<pcserver::StationInfo> stations = d->store->listStations();
    if (stationId > 0) {
        QVector<pcserver::StationInfo> filtered;
        for (const pcserver::StationInfo &station : stations) {
            if (station.id == stationId) {
                filtered.append(station);
                break;
            }
        }
        if (filtered.isEmpty()) {
            sendSocketReply(
                client, msgType,
                socketResponseEnvelope(
                    404, QStringLiteral("电站不存在：id=%1").arg(stationId)));
            return;
        }
        stations = filtered;
    }

    QJsonArray stationArray;
    int returned = 0;
    for (const pcserver::StationInfo &station : stations) {
        if (returned >= limit)
            break;
        QJsonObject item;
        item.insert(QStringLiteral("id"), station.id);
        item.insert(QStringLiteral("name"), station.name);
        item.insert(QStringLiteral("address"), station.address);
        item.insert(QStringLiteral("longitude"), station.longitude);
        item.insert(QStringLiteral("latitude"), station.latitude);
        item.insert(QStringLiteral("total_piles"), station.totalPiles);
        item.insert(QStringLiteral("idle_piles"), station.idlePiles);
        item.insert(QStringLiteral("online_piles"), station.onlinePiles);
        item.insert(QStringLiteral("online_rate"), station.onlineRate);
        item.insert(QStringLiteral("base_price"), station.basePrice);
        item.insert(QStringLiteral("price"), station.currentPrice);
        item.insert(QStringLiteral("discount"), station.discount);
        item.insert(QStringLiteral("is_discount"), station.onSale);
        const double predictedIdle = pcserver::predictIdleRatePercent(*d->store, station.id);
        item.insert(QStringLiteral("forecast_available"), predictedIdle >= 0);
        item.insert(QStringLiteral("predicted_idle_rate"), predictedIdle);
        item.insert(QStringLiteral("predicted_idle_piles"), predictedIdle >= 0
                    ? qBound(0, qRound(station.totalPiles * predictedIdle / 100.0), station.totalPiles) : -1);
        item.insert(QStringLiteral("forecast_horizon_hours"), 1);
        item.insert(QStringLiteral("forecast_basis"), QStringLiteral("小时功率估算，非排队承诺"));
        stationArray.append(item);
        ++returned;
    }

    QJsonObject response =
        socketResponseEnvelope(0, QStringLiteral("ok"));
    response.insert(QStringLiteral("total"), returned);
    response.insert(QStringLiteral("stations"), stationArray);

    // 站内电桩明细：仅当请求指定了单个电站（station_id > 0）时返回数据库中的真实桩列表，
    // 供用户端「电站详情」直接渲染 code/type/power_kw/state（取代原先的本地合成桩）。
    if (stationId > 0) {
        QJsonArray pileArray;
        const QVector<pcserver::PileInfo> piles = d->store->listPiles(stationId);
        for (const pcserver::PileInfo &pile : piles) {
            QJsonObject item;
            item.insert(QStringLiteral("id"), pile.id);
            item.insert(QStringLiteral("station_id"), pile.stationId);
            item.insert(QStringLiteral("code"), pile.code);
            item.insert(QStringLiteral("type"), pile.type);
            item.insert(QStringLiteral("power_kw"), pile.powerKw);
            item.insert(QStringLiteral("state"), static_cast<int>(pile.state));
            item.insert(QStringLiteral("state_text"), cp::pileStateText(pile.state));
            item.insert(QStringLiteral("charge_count"), pile.chargeCount);
            item.insert(QStringLiteral("charge_seconds"),
                        static_cast<double>(pile.chargeSeconds));
            pileArray.append(item);
        }
        response.insert(QStringLiteral("piles"), pileArray);
        response.insert(QStringLiteral("piles_total"), pileArray.size());
    }

    sendSocketReply(client, msgType, response);
}
