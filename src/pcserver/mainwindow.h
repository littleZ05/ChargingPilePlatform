#ifndef PCSERVER_MAINWINDOW_H
#define PCSERVER_MAINWINDOW_H

#include <QByteArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>

#include "stationstore.h"

namespace Ui { class MainWindow; }
class QTcpSocket;

namespace pcserver {

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

private slots:
    void simulateRealtimeOnce();
    void handleSocketPacket(QTcpSocket *client, quint16 msgType,
                            const QByteArray &body);

private:
    void buildUi();
    void refreshAll();
    void startSocketServer();
    void sendSocketReply(QTcpSocket *client, quint16 msgType,
                         const QJsonObject &payload);
    void handleHeartbeatPacket(QTcpSocket *client, const QByteArray &body);
    void handleStationQueryPacket(QTcpSocket *client, const QByteArray &body);
    void handleOrderReportPacket(QTcpSocket *client, const QByteArray &body);
    void refreshSales();
    void refreshPileStatus();
    void refreshPileManagement();
    void clearManageForm();
    void loadManageFormFromSelection();
    void submitManagePile(bool updateExisting);
    void changeSelectedPileState(int state);
    void sendRemoteRestart();
    void updateActionButtons();
    void setupStationPage();
    void fillStationTable(const QVector<pcserver::StationInfo> &stations);
    int selectedStationId() const;
    void selectStationById(int stationId);
    void onAddStationClicked();

    struct Private;
    Private *d;
};

#endif // PCSERVER_MAINWINDOW_H
