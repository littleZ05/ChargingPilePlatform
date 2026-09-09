#ifndef PCSERVER_DASHBOARD_API_H
#define PCSERVER_DASHBOARD_API_H

#include <QByteArray>
#include <QHash>
#include <QObject>

class QTcpServer;
class QTcpSocket;

namespace pcserver {

class StationStore;

/**
 * NO.16 Web 大屏数据注入：极简 HTTP/JSON 服务（仅监听本机回环）。
 *
 * - GET /api/dashboard/overview → 大屏全量指标（真实聚合优先 + 仿真兜底）；
 * - GET /api/dashboard/health   → 存活探测；
 * - 其余路径 / 非 GET → 4xx JSON；
 * - 所有响应均带 CORS 头（Access-Control-Allow-Origin: *），
 *   供以 file:// 方式打开的 index.html 直接 fetch。
 */
class DashboardApiServer : public QObject
{
    Q_OBJECT
public:
    explicit DashboardApiServer(StationStore *store, QObject *parent = nullptr);
    ~DashboardApiServer() override;

    /** port<=0 时取 defaultPort()；仅监听 QHostAddress::LocalHost */
    bool start(int port = 0, QString *error = nullptr);
    void stop();

    bool isListening() const;
    int  port() const;   // 实际监听端口（0 = 未监听）

    /** 默认端口 8890，可用环境变量 PCSERVER_DASH_PORT 覆盖 */
    static int defaultPort();

private:
    void handleNewConnection();
    void handleReadyRead();
    void handleClientDisconnected();
    void serveRequest(QTcpSocket *client, const QByteArray &requestHead);
    QByteArray buildResponse(int statusCode, const QByteArray &body,
                             const QByteArray &contentType) const;

    StationStore *m_store = nullptr;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;  // 持有：逐连接请求缓冲
    int m_port = 0;
};

} // namespace pcserver

#endif // PCSERVER_DASHBOARD_API_H
