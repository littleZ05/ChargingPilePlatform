#ifndef PCSERVER_MAINWINDOW_P_H
#define PCSERVER_MAINWINDOW_P_H
#include "mainwindow_support.h"
struct MainWindow::Private
{
    Ui::MainWindow *ui = nullptr;
    QString adminName;
    pcserver::StationStore *store = nullptr;
    cp::NetServer *netServer = nullptr;   // 持有：Socket 服务端（构造时创建，析构时回收）
    bool socketStarted = false;
    pcserver::DashboardApiServer *dashboardServer = nullptr; // 持有：大屏 HTTP 数据服务
    bool dashboardStarted = false;

    QTabWidget *tabs = nullptr;
    QLabel *adminLabel = nullptr;

    QComboBox *salesRangeCombo = nullptr;
    QLabel *salesTodayValue = nullptr;
    QLabel *salesMonthValue = nullptr;
    QLabel *salesTotalValue = nullptr;
    QChartView *salesChartView = nullptr;
    QTableWidget *ordersTable = nullptr;

    QComboBox *forecastStationCombo = nullptr;
    QComboBox *forecastWindowCombo = nullptr;
    QComboBox *forecastHorizonCombo = nullptr;
    QComboBox *forecastModelCombo = nullptr;
    QChartView *forecastChartView = nullptr;
    QLabel *forecastStatusLabel = nullptr;
    QPushButton *forecastRefreshButton = nullptr;

    QLabel *statusTotalValue = nullptr;
    QLabel *statusIdleValue = nullptr;
    QLabel *statusChargingValue = nullptr;
    QLabel *statusFaultValue = nullptr;
    QComboBox *statusFilterCombo = nullptr;
    QTableWidget *statusTable = nullptr;
    QPushButton *statusSetIdleButton = nullptr;
    QPushButton *statusSetChargingButton = nullptr;
    QPushButton *statusSetFaultButton = nullptr;

    QComboBox *manageStationCombo = nullptr;
    QLineEdit *manageCodeEdit = nullptr;
    QComboBox *manageTypeCombo = nullptr;
    QDoubleSpinBox *managePowerSpin = nullptr;
    QComboBox *manageStateCombo = nullptr;
    QTableWidget *manageTable = nullptr;
    QPushButton *manageAddButton = nullptr;
    QPushButton *manageUpdateButton = nullptr;
    QPushButton *manageDeleteButton = nullptr;
    QPushButton *manageRestartButton = nullptr;
    QPushButton *manageClearButton = nullptr;

    QLineEdit *userSearchEdit = nullptr;
    QPushButton *userSearchButton = nullptr;
    QPushButton *userRefreshButton = nullptr;
    QPushButton *userFreezeButton = nullptr;
    QPushButton *userUnfreezeButton = nullptr;
    QTableWidget *userTable = nullptr;
    int selectedUserId = -1;
    int selectedUserStatus = -1;

    QTableWidget *stationTable = nullptr;
    QTableWidget *stationPileTable = nullptr;
    QLabel *stationPileHintLabel = nullptr;
    QPushButton *stationRefreshButton = nullptr;
    QPushButton *stationAddButton = nullptr;
    QTimer *stationTimer = nullptr;
    int currentStationId = -1;

    int selectedStatusPileId = -1;
    int selectedManagePileId = -1;
};
#endif
