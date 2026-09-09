#include "dashboard_api.h"

#include "stationstore.h"

#include <QDate>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

constexpr int kDefaultDashboardPort = 8890;

QString nowText()
{
    return QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QJsonObject okEnvelope(const QJsonObject &data)
{
    QJsonObject body;
    body.insert(QStringLiteral("code"), 0);
    body.insert(QStringLiteral("message"), QStringLiteral("ok"));
    body.insert(QStringLiteral("ts"), nowText());
    body.insert(QStringLiteral("data"), data);
    return body;
}

QJsonObject errorEnvelope(int code, const QString &message)
{
    QJsonObject body;
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("message"), message);
    body.insert(QStringLiteral("ts"), nowText());
    return body;
}

/** 大屏快照 → /api/dashboard/overview 的 data 段 */
QJsonObject snapshotToData(const pcserver::DashboardSnapshot &snap)
{
    QJsonObject kpi;
    kpi.insert(QStringLiteral("total_piles"), snap.totalPiles);
    kpi.insert(QStringLiteral("idle_piles"), snap.idlePiles);
    kpi.insert(QStringLiteral("charging_piles"), snap.chargingPiles);
    kpi.insert(QStringLiteral("fault_piles"), snap.faultPiles);
    kpi.insert(QStringLiteral("online_rate"), snap.onlineRate);
    kpi.insert(QStringLiteral("total_capacity_kw"), snap.totalCapacityKw);
    kpi.insert(QStringLiteral("current_load_kw"), snap.currentLoadKw);
    kpi.insert(QStringLiteral("today_revenue_yuan"), snap.todayRevenueYuan);
    kpi.insert(QStringLiteral("today_orders"), snap.todayOrders);
    kpi.insert(QStringLiteral("today_energy_kwh"), snap.todayEnergyKwh);
    kpi.insert(QStringLiteral("forecast_peak_kw"), snap.forecastPeakKw);
    kpi.insert(QStringLiteral("forecast_horizon"), snap.forecastHorizon);
    kpi.insert(QStringLiteral("forecast_trend"), snap.forecastTrendText);

    QJsonObject data;
    data.insert(QStringLiteral("kpi"), kpi);

    // 桩状态分布（真实值由聚合结果回填）
    QJsonArray states;
    const int counts[3] = { snap.idlePiles, snap.chargingPiles, snap.faultPiles };
    const QString names[3] = {
        QStringLiteral("闲置空闲 (0)"),
        QStringLiteral("充电使用中 (1)"),
        QStringLiteral("异常/待自愈 (2)")
    };
    for (int i = 0; i < 3; ++i) {
        QJsonObject item;
        item.insert(QStringLiteral("state"), i);
        item.insert(QStringLiteral("name"), names[i]);
        item.insert(QStringLiteral("value"), counts[i]);
        states.append(item);
    }
    data.insert(QStringLiteral("pile_states"), states);

    QJsonArray load;
    for (double v : snap.load24hKw)
        load.append(v);
    data.insert(QStringLiteral("load24h_kw"), load);

    QJsonObject forecast;
    forecast.insert(QStringLiteral("ok"), snap.forecastOk);
    forecast.insert(QStringLiteral("error"), snap.forecastError);
    forecast.insert(QStringLiteral("horizon"), snap.forecastHorizon);
    QJsonArray forecastKw;
    for (double v : snap.forecastKw)
        forecastKw.append(v);
    forecast.insert(QStringLiteral("kw"), forecastKw);
    forecast.insert(QStringLiteral("model"), snap.forecastModelName);
    forecast.insert(QStringLiteral("trend"), snap.forecastTrendText);
    forecast.insert(QStringLiteral("peak_kw"), snap.forecastPeakKw);
    forecast.insert(QStringLiteral("peak_hour"), snap.forecastPeakHour);
    forecast.insert(QStringLiteral("samples_used"), snap.forecastSamplesUsed);
    data.insert(QStringLiteral("forecast"), forecast);

    QJsonArray revenue;
    for (double v : snap.revenue7d)
        revenue.append(v);
    QJsonArray orders;
    for (int v : snap.order7d)
        orders.append(v);
    data.insert(QStringLiteral("revenue7d"), revenue);
    data.insert(QStringLiteral("order7d"), orders);

    QJsonArray days;
    const QDate today = QDate::currentDate();
    for (int i = 6; i >= 0; --i) {
        const QDate d = today.addDays(-i);
        days.append(QStringLiteral("%1-%2")
                        .arg(d.month(), 2, 10, QLatin1Char('0'))
                        .arg(d.day(), 2, 10, QLatin1Char('0')));
    }
    data.insert(QStringLiteral("days"), days);

    QJsonObject meta;
    meta.insert(QStringLiteral("load_demo_fallback"), snap.loadUsedDemoFallback);
    meta.insert(QStringLiteral("forecast_ok"), snap.forecastOk);
    data.insert(QStringLiteral("meta"), meta);
    return data;
}

} // namespace

namespace pcserver {

DashboardApiServer::DashboardApiServer(StationStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

DashboardApiServer::~DashboardApiServer()
{
    stop();
}

int DashboardApiServer::defaultPort()
{
    bool ok = false;
    const int envPort = qEnvironmentVariableIntValue("PCSERVER_DASH_PORT", &ok);
    if (ok && envPort > 0 && envPort <= 65535)
        return envPort;
    return kDefaultDashboardPort;
}

bool DashboardApiServer::start(int port, QString *error)
{
    stop();
    if (!m_store || !m_store->isOpen()) {
        if (error) *error = QStringLiteral("数据库未就绪，大屏数据服务未启动");
        return false;
    }
    if (port <= 0)
        port = defaultPort();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &DashboardApiServer::handleNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        if (error) *error = m_server->errorString();
        stop();
        return false;
    }
    m_port = m_server->serverPort();
    return true;
}

void DashboardApiServer::stop()
{
    if (m_server) {
        const QList<QTcpSocket *> clients = m_buffers.keys();
        for (QTcpSocket *client : clients)
            client->disconnectFromHost();
        m_buffers.clear();
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    m_port = 0;
}

bool DashboardApiServer::isListening() const
{
    return m_server && m_server->isListening();
}

int DashboardApiServer::port() const
{
    return m_port;
}

void DashboardApiServer::handleNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        if (!client)
            continue;
        connect(client, &QTcpSocket::readyRead,
                this, &DashboardApiServer::handleReadyRead);
        connect(client, &QTcpSocket::disconnected,
                this, &DashboardApiServer::handleClientDisconnected);
        m_buffers.insert(client, QByteArray());
    }
}

void DashboardApiServer::handleReadyRead()
{
    auto *client = qobject_cast<QTcpSocket *>(sender());
    if (!client || !m_buffers.contains(client))
        return;

    QByteArray &buffer = m_buffers[client];
    buffer += client->readAll();
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;  // 请求头尚未收齐（GET 无请求体，够用）

    const QByteArray requestHead = buffer.left(headerEnd);
    m_buffers.remove(client);
    serveRequest(client, requestHead);
}

void DashboardApiServer::handleClientDisconnected()
{
    auto *client = qobject_cast<QTcpSocket *>(sender());
    if (!client)
        return;
    m_buffers.remove(client);
    client->deleteLater();
}

QByteArray DashboardApiServer::buildResponse(int statusCode,
                                             const QByteArray &body,
                                             const QByteArray &contentType) const
{
    const char *reason = statusCode == 200 ? "OK"
                         : statusCode == 204 ? "No Content"
                         : statusCode == 400 ? "Bad Request"
                         : statusCode == 404 ? "Not Found"
                         : statusCode == 405 ? "Method Not Allowed"
                                             : "Internal Server Error";

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + ' ' + reason
                + "\r\n";
    if (!contentType.isEmpty()) {
        response += "Content-Type: " + contentType + "\r\n";
    }
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

void DashboardApiServer::serveRequest(QTcpSocket *client,
                                      const QByteArray &requestHead)
{
    if (!client)
        return;

    const QList<QByteArray> lines = requestHead.split('\n');
    if (lines.isEmpty()) {
        client->write(buildResponse(
            400, QJsonDocument(errorEnvelope(400, QStringLiteral("请求头为空")))
                     .toJson(QJsonDocument::Compact),
            "application/json; charset=utf-8"));
        client->flush();
        client->disconnectFromHost();
        return;
    }

    const QByteArray firstLine = lines.first().trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');
    const QByteArray method = parts.value(0);
    const QByteArray rawPath = parts.value(1);

    if (method == "OPTIONS") {
        client->write(buildResponse(204, QByteArray(), QByteArray()));
        client->flush();
        client->disconnectFromHost();
        return;
    }
    if (method != "GET") {
        QJsonObject body = errorEnvelope(405, QStringLiteral("仅支持 GET"));
        client->write(buildResponse(
            405, QJsonDocument(body).toJson(QJsonDocument::Compact),
            "application/json; charset=utf-8"));
        client->flush();
        client->disconnectFromHost();
        return;
    }

    const int queryPos = rawPath.indexOf('?');
    const QByteArray path = queryPos >= 0 ? rawPath.left(queryPos) : rawPath;

    QJsonObject body;
    int statusCode = 200;
    if (path == "/api/dashboard/health") {
        QJsonObject data;
        data.insert(QStringLiteral("service"), QStringLiteral("dashboard-api"));
        data.insert(QStringLiteral("store_open"), m_store && m_store->isOpen());
        data.insert(QStringLiteral("port"), m_port);
        body = okEnvelope(data);
    } else if (path == "/api/dashboard/overview") {
        QString error;
        DashboardSnapshot snap;
        if (m_store && m_store->dashboardSnapshot(&snap, 6, &error)) {
            body = okEnvelope(snapshotToData(snap));
        } else {
            statusCode = 500;
            body = errorEnvelope(500, error.isEmpty()
                                      ? QStringLiteral("大屏数据聚合失败")
                                      : error);
        }
    } else {
        statusCode = 404;
        body = errorEnvelope(404, QStringLiteral("资源不存在：%1")
                                      .arg(QString::fromUtf8(path)));
    }

    client->write(buildResponse(
        statusCode, QJsonDocument(body).toJson(QJsonDocument::Compact),
        "application/json; charset=utf-8"));
    client->flush();
    client->disconnectFromHost();
}

} // namespace pcserver
