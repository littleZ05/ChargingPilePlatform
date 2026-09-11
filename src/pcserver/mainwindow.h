#ifndef PCSERVER_MAINWINDOW_H
#define PCSERVER_MAINWINDOW_H

#include <QByteArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>

#include "stationstore.h"

namespace Ui { class MainWindow; }
class QTcpSocket;
namespace pcserver { class DashboardApiServer; }

namespace pcserver {

/**
 * P0 统一数据源：把管理后台（登录/业绩/桩状态/桩管理/用户管理）的库路径
 * 对齐到 StationStore 使用的同一个 SQLite 文件，消除“双库分裂”。
 * 必须在 showAdminLogin / MainWindow 构造之前调用。
 */
void setAdminDatabasePath(const QString &path);

bool showAdminLogin(QWidget *parent, QString *userName);

} // namespace pcserver

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(pcserver::StationStore *store, QWidget *parent = nullptr);
    explicit MainWindow(pcserver::StationStore *store, const QString &adminName,
                        QWidget *parent = nullptr);
    explicit MainWindow(const QString &adminName, QWidget *parent = nullptr);
    ~MainWindow() override;

    void refreshStations();
    void refreshPileDetail();
    void refreshUsers();

private slots:
    void simulateRealtimeOnce();
    void handleSocketPacket(QTcpSocket *client, quint16 msgType,
                            const QByteArray &body);
    void changeSelectedUserStatus(int status);

private:
    void buildUi();
    void refreshAll();
    void startSocketServer();
    void startDashboardServer();
    void sendSocketReply(QTcpSocket *client, quint16 msgType,
                         const QJsonObject &payload);
    void handleHeartbeatPacket(QTcpSocket *client, const QByteArray &body);
    void handleStationQueryPacket(QTcpSocket *client, const QByteArray &body);
    void handleOrderReportPacket(QTcpSocket *client, const QByteArray &body);
    void handleStartChargePacket(QTcpSocket *client, const QByteArray &body);
    void handleRechargePacket(QTcpSocket *client, const QByteArray &body);
    void handleUserLoginPacket(QTcpSocket *client, const QByteArray &body);
    void refreshSales();
    void refreshPileStatus();
    void refreshPileManagement();
    void refreshLoadForecast();
    void clearManageForm();
    void loadManageFormFromSelection();
    void submitManagePile(bool updateExisting);
    void changeSelectedPileState(int state);
    void sendRemoteRestart();
    void updateActionButtons();
    void setupStationPage();
    void setupUserPage();
    void fillStationTable(const QVector<pcserver::StationInfo> &stations);
    int selectedStationId() const;
    void selectStationById(int stationId);
    void onAddStationClicked();

    struct Private;
    Private *d;
};

#endif // PCSERVER_MAINWINDOW_H
