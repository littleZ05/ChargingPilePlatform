#include "mainwindow.h"

#include "loginpage.h"
#include "stationpage.h"
#include "stationdetailpage.h"
#include "profilepage.h"
#include "mappage.h"
#include "servergateway.h"
#include "common.h"

#include <QStackedWidget>
#include <QButtonGroup>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QStatusBar>
#include <QDateTime>

namespace {
constexpr int kDefaultPort = cp::kServerPort; // 9999
}

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

    // ---- 联调控制条（integration 分支）：连接/心跳/拉取电站/订单上报 ----
    m_gateway = new ServerGateway(this);
    auto *bar = new QWidget(statusBar());
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 2, 0, 2);
    bl->setSpacing(6);
    auto *hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), bar);
    hostEdit->setFixedWidth(110);
    auto *portSpin = new QSpinBox(bar);
    portSpin->setRange(1024, 65535);
    portSpin->setValue(kDefaultPort);
    portSpin->setFixedWidth(80);
    auto *btnConn = new QPushButton(QStringLiteral("连接"), bar);
    auto *btnHeart = new QPushButton(QStringLiteral("心跳"), bar);
    auto *btnFetch = new QPushButton(QStringLiteral("拉取电站"), bar);
    auto *btnOrder = new QPushButton(QStringLiteral("上报订单(演示)"), bar);
    auto *connLabel = new QLabel(QStringLiteral("未连接"), bar);
    bl->addWidget(new QLabel(QStringLiteral("服务器"), bar));
    bl->addWidget(hostEdit);
    bl->addWidget(portSpin);
    bl->addWidget(btnConn);
    bl->addWidget(btnHeart);
    bl->addWidget(btnFetch);
    bl->addWidget(btnOrder);
    bl->addWidget(connLabel);
    statusBar()->addPermanentWidget(bar);

    connect(btnConn, &QPushButton::clicked, this, [this, hostEdit, portSpin, btnConn]() {
        if (m_gateway->isConnected()) {
            m_gateway->disconnectFrom();
            btnConn->setText(QStringLiteral("连接"));
        } else {
            m_gateway->connectTo(hostEdit->text().trimmed(),
                                 static_cast<quint16>(portSpin->value()));
        }
    });
    connect(btnHeart, &QPushButton::clicked, m_gateway, &ServerGateway::sendHeartbeat);
    connect(btnFetch, &QPushButton::clicked, m_gateway, &ServerGateway::requestStations);
    connect(btnOrder, &QPushButton::clicked, this, [this]() {
        const QString orderNo = QStringLiteral("UC%1")
                                    .arg(QDateTime::currentMSecsSinceEpoch());
        // 演示上报固定桩 S02-P01（demo_seed 保证存在）；真实计费金额由充电流程填入
        m_gateway->reportOrder(orderNo, QStringLiteral("S02-P01"), 12.5, 15.00);
    });
    connect(m_gateway, &ServerGateway::stateChanged, this,
            [btnConn, connLabel](bool ok) {
                btnConn->setText(ok ? QStringLiteral("断开") : QStringLiteral("连接"));
                connLabel->setText(ok ? QStringLiteral("已连接") : QStringLiteral("未连接"));
            });
    connect(m_gateway, &ServerGateway::stationsReceived, this,
            [connLabel](int total, int onSale) {
                connLabel->setText(QStringLiteral("%1 站 / 特惠%2").arg(total).arg(onSale));
            });
    connect(m_gateway, &ServerGateway::logMessage, this,
            [this](const QString &t) { statusBar()->showMessage(t, 4000); });
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
