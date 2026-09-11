#ifndef USERCLIENT_PCSERVER_SESSION_H
#define USERCLIENT_PCSERVER_SESSION_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace cp {
class NetClient;
}

class QTimer;

namespace userclient {

/** 服务器 kStationQuery 应答中的单座电站（docs/socket-protocol.md §3.2） */
struct ServerStation {
    int     id = 0;
    QString name;
    QString address;
    double  longitude = 0.0;
    double  latitude  = 0.0;
    int     totalPiles   = 0;
    int     idlePiles    = 0;
    int     onlinePiles  = 0;
    double  onlineRate   = 0.0;
    double  basePrice    = 0.0;
    double  price        = 0.0;
    double  discount     = 1.0;
    bool    onSale       = false;
};

/** 服务器返回的站内电桩（真实数据，取代原先本地合成的桩列表） */
struct ServerPile {
    int     id = 0;
    int     stationId = 0;
    QString code;
    QString type;
    double  powerKw = 0.0;
    int     state = 0;              // 0 闲置 / 1 充电中 / 2 故障
    int     chargeCount = 0;
    qint64  chargeSeconds = 0;
};

/**
 * PcServerSession：充电用户端到 PcServer 的单连接异步会话。
 *
 * 设计说明：
 * - 复用 cp::NetClient 二进制帧通道，默认连接 127.0.0.1:cp::kServerPort(9999)；
 * - 连接成功后周期发送 kHeartbeat，收到 code=0/message=pong 的 ACK 后复位失联计数；
 * - 断线（disconnected/socketError）或连续多个心跳周期无 pong 时进入自动重连，
 *   退避策略默认 1s → 2s → 4s → … 封顶 30s，同一时刻只保留一个重连定时器；
 * - 支持 kStationQuery 电站列表查询，应答统一反序列化为 ServerStation 后经信号发布；
 * - 全程 Qt 信号槽 + QTimer 异步事件驱动，无裸线程、无互斥锁。
 */
class PcServerSession : public QObject
{
    Q_OBJECT
public:
    explicit PcServerSession(QObject *parent = nullptr);
    ~PcServerSession() override;

    /** 启动连接与心跳/重连管理（幂等：已启动则忽略） */
    void start();
    /** 停止并断开：取消全部定时器，之后不再发起任何重连 */
    void stop();

    /** 默认 127.0.0.1:cp::kServerPort；应在 start() 前配置 */
    void setServerAddress(const QString &host, quint16 port);
    /** 心跳 Body 中的 client_id，默认 "userclient-01" */
    void setClientId(const QString &clientId);
    /** 心跳周期（毫秒），默认 5000；测试可调小 */
    void setHeartbeatIntervalMs(int intervalMs);
    /** 连续无 pong ACK 的心跳周期数达到该值时判定失联并重连，默认 3 */
    void setAckTimeoutHeartbeats(int count);
    /** 重连退避起始间隔（毫秒），默认 1000，逐次加倍、上限 30000 */
    void setReconnectBaseIntervalMs(int intervalMs);

    bool isConnected() const;

    /**
     * 发送 kStationQuery 查询电站列表（异步，结果经 stationListReceived 返回）。
     * stationId > 0 时只查单站；limit 自动收敛到 1~200。
     * @return true 表示请求帧已写入发送缓冲；未运行/未连接/上一请求未返回时为 false。
     */
    bool queryStations(int stationId = 0, int limit = 100);

    /**
     * 查询某个电站的站内电桩明细（真实数据）。
     * 协议上复用 kStationQuery，请求带 station_id > 0，应答中额外含 piles 数组。
     */
    bool queryStationPiles(int stationId);

    /**
     * 发起充电（kStartCharge）：服务器事务内建单（orders state=0）并把电桩置为「充电中」。
     * 这是结算的前置条件；返回 false 表示未发出（未连接/上一条未返回）。
     */
    bool startCharge(const QString &phone, const QString &pileCode);

    /** NO.6 余额充值（模拟支付成功，真实落库）：结果经 rechargeResult 返回 */
    bool recharge(const QString &phone, double amount);

    /**
     * 发送 kOrderReport 上报一次充电结算结果（异步，结果经 orderReportResult 返回）。
     * 对应 docs/socket-protocol.md §3.3，body 携带 order_no/pile_code/kwh/amount。
     * @return true 表示请求帧已写入发送缓冲；未运行/未连接/上一请求未返回时为 false。
     */
    bool reportOrder(const QString &orderNo, const QString &pileCode,
                     double kwh, double amount);

    /** 发送 kLoginRequest：手机号免密登录，未注册由服务端自动注册 */
    bool login(const QString &phone);

    /** 以下计数供界面状态展示与自动化测试使用 */
    int heartbeatSentCount() const    { return m_heartbeatSentCount; }
    int pongAckCount() const          { return m_pongAckCount; }
    int reconnectAttemptCount() const { return m_reconnectAttemptCount; }

signals:
    void connectedChanged(bool connected);
    /** 收到合法 pong ACK；serverTime 为服务器应答中的 server_time（可能为空） */
    void heartbeatPongReceived(const QString &serverTime);
    /** kStationQuery 应答：code 与协议错误码一致，非 0 时 stations 为空 */
    void stationListReceived(int code, const QString &message,
                             const QVector<ServerStation> &stations);
    /** kStationQuery（带 station_id）应答中的真实站内电桩列表 */
    void stationPilesReceived(int code, const QString &message, int stationId,
                              const QVector<ServerPile> &piles);
    /** kStartCharge 应答：code=0 表示建单成功，unitPrice 为服务器口径执行价（含折扣） */
    void startChargeResult(int code, const QString &message, const QString &pileCode,
                           int orderId, double unitPrice);
    /** kRechargeRequest 应答：code=0 时 balance 为充值后余额 */
    void rechargeResult(int code, const QString &message, double amount, double balance);
    /** kOrderReport 应答：code 非 0 或 received=false 表示未受理；pileFreed 表示是否释放了充电桩 */
    void orderReportResult(int code, const QString &message,
                           const QString &orderNo, const QString &pileCode,
                           bool received, bool pileFreed);
    /** kLoginRequest 应答：code=0 成功；created=true 表示新用户自动注册 */
    void loginResult(int code, const QString &message, const QString &phone,
                     const QString &nickname, double balance, bool created);

private slots:
    void onConnected();
    void onDisconnected();
    void onSocketError(const QString &errorString);
    void onPacketReceived(quint16 msgType, const QByteArray &body);
    void onHeartbeatTick();
    void onReconnectTick();

private:
    void connectNow();
    void scheduleReconnect();
    void sendHeartbeat();
    void handleHeartbeatPacket(const QByteArray &body);
    void handleStationQueryPacket(const QByteArray &body);
    void handleStartChargePacket(const QByteArray &body);
    void handleRechargePacket(const QByteArray &body);
    void handleOrderReportPacket(const QByteArray &body);
    void handleUserLoginPacket(const QByteArray &body);
    void notifyQueryFailed(const QString &reason);
    void notifyOrderFailed(const QString &reason);

    cp::NetClient *m_netClient = nullptr;         // 持有：子对象，生命周期由本类托管
    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 9999;
    QString m_clientId = QStringLiteral("userclient-01");

    QTimer *m_heartbeatTimer = nullptr;           // 持有：心跳发送定时器
    QTimer *m_reconnectTimer = nullptr;           // 持有：单次重连定时器

    int m_heartbeatIntervalMs = 5000;
    int m_ackTimeoutHeartbeats = 3;
    int m_reconnectBaseIntervalMs = 1000;
    int m_reconnectMaxIntervalMs = 30000;
    int m_reconnectDelayMs = 1000;

    bool m_running = false;
    bool m_connected = false;
    bool m_reconnectScheduled = false;
    bool m_awaitingPong = false;
    int  m_missedAcks = 0;

    bool m_stationQueryPending = false;
    bool m_stationPilesPending = false;
    bool m_startChargePending = false;
    bool m_rechargePending = false;
    bool m_orderReportPending = false;
    bool m_loginPending = false;
    int  m_heartbeatSentCount = 0;
    int  m_pongAckCount = 0;
    int  m_reconnectAttemptCount = 0;
};

} // namespace userclient

#endif // USERCLIENT_PCSERVER_SESSION_H
