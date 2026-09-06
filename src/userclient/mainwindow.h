#ifndef USERCLIENT_MAINWINDOW_H
#define USERCLIENT_MAINWINDOW_H

#include <QMainWindow>

class QStackedWidget;
class QButtonGroup;
class LoginPage;
class StationPage;
class StationDetailPage;
class ProfilePage;
class MapPage;
struct Station;

/** 充电用户端主窗口（负责人：葛伊诺）。
 *  根栈 [登录 / 主界面 / 地图]；主界面 = 三标签页（首页/充电/我的）+ 底部导航。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void showMain(const QString &phone);
    void onStationSelected(const Station &station);
    void openMap(const Station &station);
    void showTab(int index);
    void backToMain();
    void onLogout();

private:
    void setupMainPage();

    QStackedWidget    *m_root       = nullptr;
    QStackedWidget    *m_pageStack  = nullptr;
    QButtonGroup      *m_navGroup   = nullptr;
    StationPage       *m_stationPage = nullptr;
    StationDetailPage *m_detailPage = nullptr;
    ProfilePage       *m_profilePage = nullptr;
    MapPage           *m_mapPage    = nullptr;
};

#endif // USERCLIENT_MAINWINDOW_H
