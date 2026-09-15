#include "mainwindow_p.h"
using namespace pcserver_admin;
using namespace pcserver_ui;
void MainWindow::buildUi()
{
    auto *rootLayout = d->ui->verticalLayout;
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);
    clearLayout(rootLayout);
    d->ui->placeholderLabel = nullptr;

    auto *header = new QFrame(d->ui->centralwidget);
    header->setFrameShape(QFrame::StyledPanel);
    header->setObjectName(QStringLiteral("appHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 10, 14, 10);
    headerLayout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("PC 服务器端"), header);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);

    d->adminLabel = new QLabel(QStringLiteral("当前管理员：%1").arg(d->adminName), header);
    auto *logoutButton = new QPushButton(QStringLiteral("退出登录"), header);
    logoutButton->setProperty("role", QStringLiteral("danger"));

    headerLayout->addWidget(title);
    headerLayout->addStretch(1);
    headerLayout->addWidget(d->adminLabel);
    headerLayout->addWidget(logoutButton);

    connect(logoutButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("退出登录"), QStringLiteral("确定退出登录并关闭程序吗？"))
            == QMessageBox::Yes) {
            close();
        }
    });

    d->tabs = new QTabWidget(d->ui->centralwidget);
    d->tabs->setObjectName(QStringLiteral("mainTabs"));
    rootLayout->addWidget(header);
    rootLayout->addWidget(d->tabs, 1);

    auto *salesPage = new QWidget(d->tabs);
    auto *salesLayout = new QVBoxLayout(salesPage);
    salesLayout->setContentsMargins(0, 0, 0, 0);
    salesLayout->setSpacing(10);

    auto *salesToolbar = new QHBoxLayout;
    auto *rangeLabel = new QLabel(QStringLiteral("统计周期"), salesPage);
    d->salesRangeCombo = new QComboBox(salesPage);
    d->salesRangeCombo->addItem(QStringLiteral("近7天"), 7);
    d->salesRangeCombo->addItem(QStringLiteral("近30天"), 30);
    auto *salesRefreshButton = new QPushButton(QStringLiteral("刷新"), salesPage);
    salesToolbar->addWidget(rangeLabel);
    salesToolbar->addWidget(d->salesRangeCombo);
    salesToolbar->addStretch(1);
    salesToolbar->addWidget(salesRefreshButton);

    auto *salesCards = new QGridLayout;
    salesCards->setHorizontalSpacing(10);
    salesCards->setVerticalSpacing(10);
    salesCards->addWidget(createMetricCard(QStringLiteral("今日营收"), &d->salesTodayValue), 0, 0);
    salesCards->addWidget(createMetricCard(QStringLiteral("本月营收"), &d->salesMonthValue), 0, 1);
    salesCards->addWidget(createMetricCard(QStringLiteral("总营收"), &d->salesTotalValue), 0, 2);

    d->salesChartView = new QChartView(new QChart, salesPage);
    d->salesChartView->setMinimumHeight(280);
    d->salesChartView->setRenderHint(QPainter::Antialiasing, true);

    d->ordersTable = new QTableWidget(salesPage);
    configureOrdersTable(d->ordersTable);
    d->ordersTable->setMinimumHeight(240);

    salesLayout->addLayout(salesToolbar);
    salesLayout->addLayout(salesCards);
    salesLayout->addWidget(d->salesChartView);
    salesLayout->addWidget(d->ordersTable, 1);
    d->tabs->addTab(salesPage, QStringLiteral("销售业绩"));

    auto *forecastPage = new QWidget(d->tabs);
    auto *forecastLayout = new QVBoxLayout(forecastPage);
    forecastLayout->setContentsMargins(0, 0, 0, 0);
    forecastLayout->setSpacing(10);

    auto *forecastToolbar = new QHBoxLayout;
    auto *stationLabel = new QLabel(QStringLiteral("电站"), forecastPage);
    d->forecastStationCombo = new QComboBox(forecastPage);
    d->forecastStationCombo->setObjectName(QStringLiteral("forecastStationCombo"));
    d->forecastStationCombo->setMinimumWidth(190);
    auto *windowLabel = new QLabel(QStringLiteral("历史窗口"), forecastPage);
    d->forecastWindowCombo = new QComboBox(forecastPage);
    d->forecastWindowCombo->addItem(QStringLiteral("近 12 小时"), 12);
    d->forecastWindowCombo->addItem(QStringLiteral("近 24 小时"), 24);
    auto *horizonLabel = new QLabel(QStringLiteral("预测范围"), forecastPage);
    d->forecastHorizonCombo = new QComboBox(forecastPage);
    d->forecastHorizonCombo->addItem(QStringLiteral("未来 1 小时"), 1);
    d->forecastHorizonCombo->addItem(QStringLiteral("未来 3 小时"), 3);
    d->forecastHorizonCombo->addItem(QStringLiteral("未来 6 小时"), 6);
    d->forecastHorizonCombo->addItem(QStringLiteral("未来 24 小时"), 24);
    d->forecastHorizonCombo->setCurrentIndex(2);
    auto *modelLabel = new QLabel(QStringLiteral("预测模型"), forecastPage);
    d->forecastModelCombo = new QComboBox(forecastPage);
    d->forecastModelCombo->addItem(QStringLiteral("最小二乘回归(OLS)"),
                                   static_cast<int>(cp::ForecastModel::OLS));
    d->forecastModelCombo->addItem(QStringLiteral("加权移动平均(WMA)"),
                                   static_cast<int>(cp::ForecastModel::WMA));
    d->forecastRefreshButton = new QPushButton(QStringLiteral("刷新"), forecastPage);

    forecastToolbar->addWidget(stationLabel);
    forecastToolbar->addWidget(d->forecastStationCombo);
    forecastToolbar->addSpacing(8);
    forecastToolbar->addWidget(windowLabel);
    forecastToolbar->addWidget(d->forecastWindowCombo);
    forecastToolbar->addSpacing(8);
    forecastToolbar->addWidget(horizonLabel);
    forecastToolbar->addWidget(d->forecastHorizonCombo);
    forecastToolbar->addSpacing(8);
    forecastToolbar->addWidget(modelLabel);
    forecastToolbar->addWidget(d->forecastModelCombo);
    forecastToolbar->addStretch(1);
    forecastToolbar->addWidget(d->forecastRefreshButton);

    d->forecastChartView = new QChartView(new QChart, forecastPage);
    d->forecastChartView->setObjectName(QStringLiteral("forecastChartView"));
    d->forecastChartView->setMinimumHeight(360);
    d->forecastChartView->setRenderHint(QPainter::Antialiasing, true);

    d->forecastStatusLabel = new QLabel(forecastPage);
    d->forecastStatusLabel->setObjectName(QStringLiteral("forecastStatusLabel"));
    d->forecastStatusLabel->setWordWrap(true);

    forecastLayout->addLayout(forecastToolbar);
    forecastLayout->addWidget(d->forecastChartView, 1);
    forecastLayout->addWidget(d->forecastStatusLabel);

    connect(d->forecastRefreshButton, &QPushButton::clicked,
            this, &MainWindow::refreshLoadForecast);
    connect(d->forecastStationCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { refreshLoadForecast(); });
    connect(d->forecastWindowCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { refreshLoadForecast(); });
    connect(d->forecastHorizonCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { refreshLoadForecast(); });
    connect(d->forecastModelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this]() { refreshLoadForecast(); });

    d->tabs->addTab(forecastPage, QStringLiteral("负荷预测"));

    auto *statusPage = new QWidget(d->tabs);
    auto *statusLayout = new QVBoxLayout(statusPage);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);

    auto *statusCards = new QGridLayout;
    statusCards->setHorizontalSpacing(10);
    statusCards->setVerticalSpacing(10);
    statusCards->addWidget(createMetricCard(QStringLiteral("电桩总数"), &d->statusTotalValue), 0, 0);
    statusCards->addWidget(createMetricCard(QStringLiteral("闲置"),
                                            &d->statusIdleValue,
                                            QStringLiteral("ok")),
                           0, 1);
    statusCards->addWidget(createMetricCard(QStringLiteral("充电中"),
                                            &d->statusChargingValue,
                                            QStringLiteral("info")),
                           0, 2);
    statusCards->addWidget(createMetricCard(QStringLiteral("故障"),
                                            &d->statusFaultValue,
                                            QStringLiteral("danger")),
                           0, 3);

    auto *statusToolbar = new QHBoxLayout;
    d->statusFilterCombo = new QComboBox(statusPage);
    d->statusFilterCombo->addItem(QStringLiteral("全部"), -1);
    d->statusFilterCombo->addItem(QStringLiteral("闲置"), 0);
    d->statusFilterCombo->addItem(QStringLiteral("充电中"), 1);
    d->statusFilterCombo->addItem(QStringLiteral("故障"), 2);
    auto *statusRefreshButton = new QPushButton(QStringLiteral("刷新"), statusPage);
    statusToolbar->addWidget(new QLabel(QStringLiteral("筛选状态"), statusPage));
    statusToolbar->addWidget(d->statusFilterCombo);
    statusToolbar->addStretch(1);
    statusToolbar->addWidget(statusRefreshButton);

    d->statusTable = new QTableWidget(statusPage);
    configurePileTable(d->statusTable);

    auto *statusButtons = new QHBoxLayout;
    d->statusSetIdleButton = new QPushButton(QStringLiteral("设为闲置"), statusPage);
    d->statusSetChargingButton = new QPushButton(QStringLiteral("设为充电中"), statusPage);
    d->statusSetFaultButton = new QPushButton(QStringLiteral("设为故障"), statusPage);
    d->statusSetIdleButton->setProperty("role", QStringLiteral("success"));
    d->statusSetChargingButton->setProperty("role", QStringLiteral("primary"));
    d->statusSetFaultButton->setProperty("role", QStringLiteral("danger"));
    statusButtons->addWidget(d->statusSetIdleButton);
    statusButtons->addWidget(d->statusSetChargingButton);
    statusButtons->addWidget(d->statusSetFaultButton);
    statusButtons->addStretch(1);

    statusLayout->addLayout(statusCards);
    statusLayout->addLayout(statusToolbar);
    statusLayout->addWidget(d->statusTable, 1);
    statusLayout->addLayout(statusButtons);
    d->tabs->addTab(statusPage, QStringLiteral("电桩状态"));

    auto *managePage = new QWidget(d->tabs);
    auto *manageLayout = new QHBoxLayout(managePage);
    manageLayout->setContentsMargins(0, 0, 0, 0);
    manageLayout->setSpacing(12);

    auto *manageFormPanel = new QFrame(managePage);
    manageFormPanel->setFrameShape(QFrame::StyledPanel);
    manageFormPanel->setObjectName(QStringLiteral("manageFormPanel"));
    auto *manageFormLayout = new QVBoxLayout(manageFormPanel);
    manageFormLayout->setContentsMargins(14, 14, 14, 14);
    manageFormLayout->setSpacing(10);

    auto *manageTitle = new QLabel(QStringLiteral("电桩维护"), manageFormPanel);
    QFont manageTitleFont = manageTitle->font();
    manageTitleFont.setBold(true);
    manageTitleFont.setPointSize(manageTitleFont.pointSize() + 2);
    manageTitle->setFont(manageTitleFont);

    auto *form = new QFormLayout;
    d->manageStationCombo = new QComboBox(manageFormPanel);
    d->manageCodeEdit = new QLineEdit(manageFormPanel);
    d->manageCodeEdit->setPlaceholderText(QStringLiteral("如 A-01"));
    d->manageTypeCombo = new QComboBox(manageFormPanel);
    d->manageTypeCombo->addItem(QStringLiteral("快充"));
    d->manageTypeCombo->addItem(QStringLiteral("慢充"));
    d->managePowerSpin = new QDoubleSpinBox(manageFormPanel);
    d->managePowerSpin->setRange(1.0, 360.0);
    d->managePowerSpin->setDecimals(1);
    d->managePowerSpin->setSuffix(QStringLiteral(" kW"));
    d->managePowerSpin->setValue(60.0);
    d->manageStateCombo = new QComboBox(manageFormPanel);
    d->manageStateCombo->addItem(QStringLiteral("闲置"), 0);
    d->manageStateCombo->addItem(QStringLiteral("充电中"), 1);
    d->manageStateCombo->addItem(QStringLiteral("故障"), 2);

    form->addRow(QStringLiteral("所属电站"), d->manageStationCombo);
    form->addRow(QStringLiteral("电桩编码"), d->manageCodeEdit);
    form->addRow(QStringLiteral("类型"), d->manageTypeCombo);
    form->addRow(QStringLiteral("功率"), d->managePowerSpin);
    form->addRow(QStringLiteral("状态"), d->manageStateCombo);

    auto *manageButtons = new QGridLayout;
    d->manageAddButton = new QPushButton(QStringLiteral("新增"), manageFormPanel);
    d->manageUpdateButton = new QPushButton(QStringLiteral("更新"), manageFormPanel);
    d->manageDeleteButton = new QPushButton(QStringLiteral("删除"), manageFormPanel);
    d->manageRestartButton = new QPushButton(QStringLiteral("远程重启"), manageFormPanel);
    d->manageClearButton = new QPushButton(QStringLiteral("清空"), manageFormPanel);
    auto *manageRefreshButton = new QPushButton(QStringLiteral("刷新"), manageFormPanel);
    d->manageAddButton->setProperty("role", QStringLiteral("primary"));
    d->manageDeleteButton->setProperty("role", QStringLiteral("danger"));
    d->manageRestartButton->setProperty("role", QStringLiteral("warning"));
    manageButtons->addWidget(d->manageAddButton, 0, 0);
    manageButtons->addWidget(d->manageUpdateButton, 0, 1);
    manageButtons->addWidget(d->manageDeleteButton, 1, 0);
    manageButtons->addWidget(d->manageRestartButton, 1, 1);
    manageButtons->addWidget(d->manageClearButton, 2, 0);
    manageButtons->addWidget(manageRefreshButton, 2, 1);

    manageFormLayout->addWidget(manageTitle);
    manageFormLayout->addLayout(form);
    manageFormLayout->addLayout(manageButtons);

    d->manageTable = new QTableWidget(managePage);
    configurePileTable(d->manageTable);
    setupStationPage();
    setupUserPage();

    manageLayout->addWidget(manageFormPanel, 0);
    manageLayout->addWidget(d->manageTable, 1);
    d->tabs->addTab(managePage, QStringLiteral("充电桩管理"));

    // NO.20 / NO.21 / NO.22 / NO.23：运营控制台四个页签（张芮萌负责需求的前端落地）
    d->tabs->addTab(new pcserver::PricingPolicyPanel(d->store, d->tabs),
                    QStringLiteral("价格策略"));
    d->tabs->addTab(new pcserver::SelfHealPanel(d->store, d->tabs),
                    QStringLiteral("自愈告警"));
    d->tabs->addTab(new pcserver::RunLogPanel(d->tabs),
                    QStringLiteral("运行日志"));
    d->tabs->addTab(new pcserver::SelfCheckPanel(d->store, d->tabs),
                    QStringLiteral("交付自检"));

    connect(d->salesRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshSales();
    });
    connect(salesRefreshButton, &QPushButton::clicked, this, [this]() {
        refreshSales();
    });
    connect(statusRefreshButton, &QPushButton::clicked, this, [this]() {
        refreshPileStatus();
    });
    connect(d->statusFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshPileStatus();
    });
    connect(d->statusTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = d->statusTable->currentRow();
        d->selectedStatusPileId = row >= 0 ? rowId(d->statusTable, row) : -1;
        updateActionButtons();
    });
    connect(d->statusSetIdleButton, &QPushButton::clicked, this, [this]() {
        changeSelectedPileState(0);
    });
    connect(d->statusSetChargingButton, &QPushButton::clicked, this, [this]() {
        changeSelectedPileState(1);
    });
    connect(d->statusSetFaultButton, &QPushButton::clicked, this, [this]() {
        changeSelectedPileState(2);
    });

    connect(d->manageTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        loadManageFormFromSelection();
    });
    connect(d->manageAddButton, &QPushButton::clicked, this, [this]() {
        submitManagePile(false);
    });
    connect(d->manageUpdateButton, &QPushButton::clicked, this, [this]() {
        submitManagePile(true);
    });
    connect(d->manageDeleteButton, &QPushButton::clicked, this, [this]() {
        const int pileId = d->selectedManagePileId;
        if (pileId <= 0) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), QStringLiteral("请先选择一个电桩。"));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("删除电桩"), QStringLiteral("确定删除当前选中的电桩吗？"))
            != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (!DatabaseManager::instance().deletePile(pileId, &error)) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), error);
            return;
        }
        d->selectedManagePileId = -1;
        statusBar()->showMessage(QStringLiteral("电桩已删除"), 5000);
        refreshAll();
    });
    connect(d->manageRestartButton, &QPushButton::clicked, this, [this]() {
        sendRemoteRestart();
    });
    connect(d->manageClearButton, &QPushButton::clicked, this, [this]() {
        clearManageForm();
    });
    connect(manageRefreshButton, &QPushButton::clicked, this, [this]() {
        refreshPileManagement();
    });

    updateActionButtons();
}

void MainWindow::setupStationPage()
{
    auto *page = new QWidget(d->tabs);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(10);

    auto *splitter = new QSplitter(Qt::Vertical, page);
    splitter->setChildrenCollapsible(false);

    auto *stationGroup = new QGroupBox(QStringLiteral("充电站列表"), splitter);
    auto *stationLayout = new QVBoxLayout(stationGroup);
    auto *toolbar = new QHBoxLayout;
    d->stationRefreshButton = new QPushButton(QStringLiteral("刷新"), stationGroup);
    d->stationAddButton = new QPushButton(QStringLiteral("新增电站"), stationGroup);
    d->stationRefreshButton->setObjectName(QStringLiteral("refreshStationsButton"));
    d->stationAddButton->setObjectName(QStringLiteral("addStationButton"));
    d->stationAddButton->setProperty("role", QStringLiteral("primary"));
    toolbar->addWidget(d->stationRefreshButton);
    toolbar->addWidget(d->stationAddButton);
    toolbar->addStretch(1);
    stationLayout->addLayout(toolbar);

    d->stationTable = new QTableWidget(stationGroup);
    d->stationTable->setObjectName(QStringLiteral("stationTable"));
    d->stationTable->setColumnCount(7);
    d->stationTable->setHorizontalHeaderLabels({
        QStringLiteral("站ID"),
        QStringLiteral("站名"),
        QStringLiteral("地址"),
        QStringLiteral("经度"),
        QStringLiteral("纬度"),
        QStringLiteral("总电桩数"),
        QStringLiteral("当前在线率")
    });
    d->stationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->stationTable->setSelectionMode(QAbstractItemView::SingleSelection);
    d->stationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->stationTable->setAlternatingRowColors(true);
    d->stationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    d->stationTable->horizontalHeader()->setStretchLastSection(true);
    d->stationTable->verticalHeader()->setVisible(false);
    stationLayout->addWidget(d->stationTable);

    auto *pileGroup = new QGroupBox(QStringLiteral("站内电桩实时状态明细"), splitter);
    auto *pileLayout = new QVBoxLayout(pileGroup);
    d->stationPileTable = new QTableWidget(pileGroup);
    d->stationPileTable->setObjectName(QStringLiteral("pileTable"));
    d->stationPileTable->setColumnCount(7);
    d->stationPileTable->setHorizontalHeaderLabels({
        QStringLiteral("电桩ID"),
        QStringLiteral("电桩编号"),
        QStringLiteral("类型"),
        QStringLiteral("功率(kW)"),
        QStringLiteral("实时状态"),
        QStringLiteral("累计充电次数"),
        QStringLiteral("累计时长(s)")
    });
    d->stationPileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->stationPileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    d->stationPileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->stationPileTable->setAlternatingRowColors(true);
    d->stationPileTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    d->stationPileTable->horizontalHeader()->setStretchLastSection(true);
    d->stationPileTable->verticalHeader()->setVisible(false);
    pileLayout->addWidget(d->stationPileTable);

    d->stationPileHintLabel = new QLabel(
        QStringLiteral("请在上方列表选择一座充电站查看站内电桩状态"), pileGroup);
    d->stationPileHintLabel->setObjectName(QStringLiteral("pileHintLabel"));
    d->stationPileHintLabel->setAlignment(Qt::AlignCenter);
    pileLayout->addWidget(d->stationPileHintLabel);

    splitter->addWidget(stationGroup);
    splitter->addWidget(pileGroup);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    pageLayout->addWidget(splitter);
    d->tabs->addTab(page, QStringLiteral("充电站管理"));

    connect(d->stationRefreshButton, &QPushButton::clicked, this, &MainWindow::refreshStations);
    connect(d->stationTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        refreshPileDetail();
    });
    connect(d->stationAddButton, &QPushButton::clicked, this, &MainWindow::onAddStationClicked);

    if (d->store) {
        d->stationTimer = new QTimer(this);
        d->stationTimer->setInterval(3000);
        connect(d->stationTimer, &QTimer::timeout, this, &MainWindow::simulateRealtimeOnce);
        // 真实数据优先：默认不再随机翻转电桩状态（会污染大屏 KPI 与订单统计）。
        // 仅当显式设置 PCSERVER_SIMULATE=1 时才启用演示用状态迁移。
        if (qEnvironmentVariableIntValue("PCSERVER_SIMULATE")) {
            d->stationTimer->start();
            qInfo().noquote() << QStringLiteral(
                "[演示] 已启用随机状态迁移（PCSERVER_SIMULATE=1）");
        }
    }
}

void MainWindow::setupUserPage()
{
    auto *page = new QWidget(d->tabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(QStringLiteral("手机号搜索"), page));
    d->userSearchEdit = new QLineEdit(page);
    d->userSearchEdit->setObjectName(QStringLiteral("userSearchEdit"));
    d->userSearchEdit->setClearButtonEnabled(true);
    d->userSearchEdit->setPlaceholderText(QStringLiteral("支持手机号模糊搜索，例如 138000"));
    d->userSearchEdit->setMaximumWidth(280);
    d->userSearchButton = new QPushButton(QStringLiteral("搜索"), page);
    d->userSearchButton->setObjectName(QStringLiteral("userSearchButton"));
    d->userRefreshButton = new QPushButton(QStringLiteral("刷新"), page);
    d->userRefreshButton->setObjectName(QStringLiteral("userRefreshButton"));
    d->userSearchButton->setProperty("role", QStringLiteral("primary"));
    toolbar->addWidget(d->userSearchEdit);
    toolbar->addWidget(d->userSearchButton);
    toolbar->addWidget(d->userRefreshButton);
    toolbar->addStretch(1);
    layout->addLayout(toolbar);

    d->userTable = new QTableWidget(page);
    d->userTable->setObjectName(QStringLiteral("userTable"));
    configureUserTable(d->userTable);
    layout->addWidget(d->userTable, 1);

    auto *actionRow = new QHBoxLayout;
    d->userFreezeButton = new QPushButton(QStringLiteral("冻结账号"), page);
    d->userFreezeButton->setObjectName(QStringLiteral("userFreezeButton"));
    d->userUnfreezeButton = new QPushButton(QStringLiteral("解冻账号"), page);
    d->userUnfreezeButton->setObjectName(QStringLiteral("userUnfreezeButton"));
    d->userFreezeButton->setProperty("role", QStringLiteral("warning"));
    d->userUnfreezeButton->setProperty("role", QStringLiteral("success"));
    actionRow->addWidget(d->userFreezeButton);
    actionRow->addWidget(d->userUnfreezeButton);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    d->tabs->addTab(page, QStringLiteral("用户管理"));

    connect(d->userSearchButton, &QPushButton::clicked, this, &MainWindow::refreshUsers);
    connect(d->userSearchEdit, &QLineEdit::returnPressed, this, &MainWindow::refreshUsers);
    connect(d->userRefreshButton, &QPushButton::clicked, this, &MainWindow::refreshUsers);
    connect(d->userTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = d->userTable->currentRow();
        if (row >= 0) {
            d->selectedUserId = rowId(d->userTable, row);
            const auto *statusItem = d->userTable->item(row, 5);
            d->selectedUserStatus =
                statusItem ? statusItem->data(Qt::UserRole).toInt() : -1;
        } else {
            d->selectedUserId = -1;
            d->selectedUserStatus = -1;
        }
        updateActionButtons();
    });
    connect(d->userFreezeButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("冻结账号"),
                                  QStringLiteral("确定冻结该用户账号吗？冻结后禁止继续充电。"))
            == QMessageBox::Yes) {
            changeSelectedUserStatus(1);
        }
    });
    connect(d->userUnfreezeButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("解冻账号"),
                                  QStringLiteral("确定解冻该用户账号吗？"))
            == QMessageBox::Yes) {
            changeSelectedUserStatus(0);
        }
    });
}
