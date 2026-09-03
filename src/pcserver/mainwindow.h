#ifndef PCSERVER_MAINWINDOW_H
#define PCSERVER_MAINWINDOW_H

#include <QMainWindow>

#include "stationstore.h"

namespace Ui { class MainWindow; }

class QLabel;
class QPushButton;
class QTableWidget;

/**
 * PC 服务器端主窗口。
 * NO.12 充电站管理：顶部电站列表（ID/站名/地址/经纬度/总桩数/在线率），
 * 下方为选中电站的电桩状态明细区（点击行/实时刷新接入）。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(pcserver::StationStore *store, QWidget *parent = nullptr);
    ~MainWindow() override;

    /** 从数据库重新加载充电站列表（测试与按钮共用入口） */
    void refreshStations();
    /** 重新加载当前选中电站的站内电桩实时状态明细 */
    void refreshPileDetail();

private:
    void setupStationPage();
    void connectSignals();
    void fillStationTable(const QVector<pcserver::StationInfo> &stations);
    int selectedStationId() const;

    Ui::MainWindow *ui;
    pcserver::StationStore *m_store = nullptr;
    QTableWidget *m_stationTable = nullptr;
    QTableWidget *m_pileTable = nullptr;
    QLabel       *m_pileHintLabel = nullptr;
    QPushButton  *m_refreshButton = nullptr;
    QPushButton  *m_addStationButton = nullptr;
    int           m_currentStationId = -1;
};

#endif // PCSERVER_MAINWINDOW_H
