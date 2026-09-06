#include "mainwindow.h"

#include "loginpage.h"
#include "stationpage.h"
#include "stationdetailpage.h"
#include "profilepage.h"
#include "mappage.h"

#include <QStackedWidget>
#include <QButtonGroup>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("充电用户端"));
    resize(420, 800);

    auto *central = new QWidget(this);
    auto *cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    m_root = new QStackedWidget(central);
    cl->addWidget(m_root);
    setCentralWidget(central);

    // 0 登录页
    auto *login = new LoginPage(this);
    connect(login, &LoginPage::loginSucceeded, this, &MainWindow::showMain);
    m_root->addWidget(login);

    // 1 主界面
    setupMainPage();

    // 2 地图页
    m_mapPage = new MapPage(this);
    connect(m_mapPage, &MapPage::backRequested, this, &MainWindow::backToMain);
    m_root->addWidget(m_mapPage);

    connect(m_stationPage, &StationPage::stationSelected, this, &MainWindow::onStationSelected);
    connect(m_detailPage, &StationDetailPage::navigateRequested, this, &MainWindow::openMap);
    connect(m_profilePage, &ProfilePage::logoutRequested, this, &MainWindow::onLogout);

    showTab(0);
    m_navGroup->button(0)->setChecked(true);
}

void MainWindow::setupMainPage()
{
    auto *mainPage = new QWidget(this);
    auto *mp = new QVBoxLayout(mainPage);
    mp->setContentsMargins(0, 0, 0, 0);
    mp->setSpacing(0);

    m_pageStack = new QStackedWidget(mainPage);
    m_stationPage = new StationPage(mainPage);
    m_detailPage = new StationDetailPage(mainPage);
    m_profilePage = new ProfilePage(mainPage);
    m_pageStack->addWidget(m_stationPage); // 0 首页
    m_pageStack->addWidget(m_detailPage);  // 1 充电
    m_pageStack->addWidget(m_profilePage); // 2 我的
    mp->addWidget(m_pageStack, 1);

    // 底部导航
    auto *navBar = new QWidget(mainPage);
    navBar->setObjectName(QStringLiteral("navBar"));
    auto *nl = new QHBoxLayout(navBar);
    nl->setContentsMargins(0, 6, 0, 6);
    nl->setSpacing(0);

    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    const QString labels[] = { QStringLiteral("首页"), QStringLiteral("充电"), QStringLiteral("我的") };
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QPushButton(labels[i], navBar);
        btn->setCheckable(true);
        btn->setObjectName(QStringLiteral("navTab"));
        nl->addWidget(btn);
        m_navGroup->addButton(btn, i);
        connect(btn, &QPushButton::clicked, this, [this, i] { showTab(i); });
    }
    mp->addWidget(navBar);

    m_root->addWidget(mainPage); // 1
}

void MainWindow::showMain(const QString &phone)
{
    m_profilePage->setPhone(phone);
    m_root->setCurrentIndex(1);
    showTab(0);
    m_navGroup->button(0)->setChecked(true);
}

void MainWindow::onStationSelected(const Station &station)
{
    m_detailPage->setStation(station);
    showTab(1);
    m_navGroup->button(1)->setChecked(true);
}

void MainWindow::showTab(int index)
{
    m_pageStack->setCurrentIndex(index);
}

void MainWindow::openMap(const Station &station)
{
    m_mapPage->setRoute(station);
    m_root->setCurrentIndex(2);
}

void MainWindow::backToMain()
{
    m_root->setCurrentIndex(1);
}

void MainWindow::onLogout()
{
    m_root->setCurrentIndex(0);
}
