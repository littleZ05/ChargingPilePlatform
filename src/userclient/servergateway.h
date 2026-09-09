#ifndef UC_SERVERGATEWAY_H
#define UC_SERVERGATEWAY_H

/**
 * 用户端 ↔ 服务器 联调网关（基于 cp::NetClient，遵循 socket-protocol.md）
 * 提供：连接/心跳/电站查询/订单结算上报；业务 JSON 编解码与回复解析。
 * 维护：组长 integration 分支（供葛伊诺 NO.4-7 页面接入）。
 */
#include <QObject>
#include <QJsonArray>
#include "net_client.h"

class ServerGateway : public QObject
{
    Q_OBJECT
public:
    explicit ServerGateway(QObject *parent = nullptr);

    void connectTo(const QString &host, quint16 port);
    void disconnectFrom();
    bool isConnected() const;
    void sendHeartbeat();
    void requestStations(int limit = 100);
    void reportOrder(const QString &orderNo, const QString &pileCode,
                     double kwh, double amount);

signals:
    void logMessage(const QString &text);
    void stateChanged(bool connected);
    void stationsReceived(int total, int onSale);
    void orderReported(int code, const QString &message, bool received);

private slots:
    void onConnected();
    void onDisconnected();
    void onPacket(quint16 msgType, const QByteArray &body);
    void onError(const QString &errorString);

private:
    cp::NetClient m_client;
};

#endif
