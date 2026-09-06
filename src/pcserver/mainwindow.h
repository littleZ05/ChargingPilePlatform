#ifndef PCSERVER_MAINWINDOW_H
#define PCSERVER_MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include "stationstore.h"

namespace Ui { class MainWindow; }

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

private:
    void buildUi();
    void refreshAll();
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
