#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QDebug>

namespace {

constexpr int kStationColumnId       = 0;
constexpr int kStationColumnName     = 1;
constexpr int kStationColumnAddress  = 2;
constexpr int kStationColumnLng      = 3;
constexpr int kStationColumnLat      = 4;
constexpr int kStationColumnTotal    = 5;
constexpr int kStationColumnRate     = 6;

} // namespace

MainWindow::MainWindow(pcserver::StationStore *store, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    m_store = store;
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("PC 服务器端 - 东软电动汽车充电桩应用管理平台"));
    resize(1180, 760);

    setupStationPage();
    connectSignals();
    refreshStations();
    statusBar()->showMessage(QStringLiteral("NO.12 充电站管理：演示数据为 SQLite 本地模拟"), 5000);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupStationPage()
{
    // 清空 ui 文件中的占位内容，统一改为站/桩双表格后台布局
    while (QLayoutItem *item = ui->verticalLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    auto *splitter = new QSplitter(Qt::Vertical, ui->centralwidget);
    splitter->setChildrenCollapsible(false);

    // 上方：充电站列表（含刷新/新增操作入口）
    auto *stationGroup = new QGroupBox(QStringLiteral("充电站列表"), splitter);
    auto *stationLayout = new QVBoxLayout(stationGroup);
    auto *stationToolbar = new QHBoxLayout;
    m_refreshButton = new QPushButton(QStringLiteral("刷新"), stationGroup);
    m_addStationButton = new QPushButton(QStringLiteral("新增电站…"), stationGroup);
    m_refreshButton->setObjectName(QStringLiteral("refreshStationsButton"));
    m_addStationButton->setObjectName(QStringLiteral("addStationButton"));
    stationToolbar->addWidget(m_refreshButton);
    stationToolbar->addWidget(m_addStationButton);
    stationToolbar->addStretch();
    stationLayout->addLayout(stationToolbar);

    m_stationTable = new QTableWidget(stationGroup);
    m_stationTable->setObjectName(QStringLiteral("stationTable"));
    m_stationTable->setColumnCount(7);
    m_stationTable->setHorizontalHeaderLabels({
        QStringLiteral("站ID"), QStringLiteral("站名"), QStringLiteral("地址"),
        QStringLiteral("经度"), QStringLiteral("纬度"),
        QStringLiteral("总电桩数"), QStringLiteral("当前在线率")
    });
    m_stationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stationTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stationTable->setAlternatingRowColors(true);
    m_stationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_stationTable->horizontalHeader()->setStretchLastSection(true);
    m_stationTable->verticalHeader()->setVisible(false);
    stationLayout->addWidget(m_stationTable);

    // 下方：站内电桩实时状态明细
    auto *pileGroup = new QGroupBox(QStringLiteral("站内电桩实时状态明细"), splitter);
    auto *pileLayout = new QVBoxLayout(pileGroup);
    m_pileTable = new QTableWidget(pileGroup);
    m_pileTable->setObjectName(QStringLiteral("pileTable"));
    m_pileTable->setColumnCount(7);
    m_pileTable->setHorizontalHeaderLabels({
        QStringLiteral("电桩ID"), QStringLiteral("电桩编号"), QStringLiteral("类型"),
        QStringLiteral("功率(kW)"), QStringLiteral("实时状态"),
        QStringLiteral("累计充电次数"), QStringLiteral("累计时长(s)")
    });
    m_pileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pileTable->setAlternatingRowColors(true);
    m_pileTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_pileTable->horizontalHeader()->setStretchLastSection(true);
    m_pileTable->verticalHeader()->setVisible(false);
    pileLayout->addWidget(m_pileTable);

    m_pileHintLabel = new QLabel(QStringLiteral("请在上方列表选择一座充电站查看站内电桩状态"),
                                 pileGroup);
    m_pileHintLabel->setObjectName(QStringLiteral("pileHintLabel"));
    m_pileHintLabel->setAlignment(Qt::AlignCenter);
    pileLayout->addWidget(m_pileHintLabel);

    splitter->addWidget(stationGroup);
    splitter->addWidget(pileGroup);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    ui->verticalLayout->addWidget(splitter);
}

void MainWindow::connectSignals()
{
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshStations);
    connect(m_addStationButton, &QPushButton::clicked, this, [this]() {
        // 新增电站对话框在“新增电站”原子功能中接入
        statusBar()->showMessage(QStringLiteral("新增电站功能开发中"), 3000);
    });
}

void MainWindow::refreshStations()
{
    if (!m_store || !m_store->isOpen())
        return;
    fillStationTable(m_store->listStations());
}

void MainWindow::fillStationTable(const QVector<pcserver::StationInfo> &stations)
{
    m_stationTable->setRowCount(0);
    m_stationTable->setRowCount(stations.size());

    for (int row = 0; row < stations.size(); ++row) {
        const pcserver::StationInfo &s = stations.at(row);
        auto *idItem = new QTableWidgetItem(QString::number(s.id));
        idItem->setData(Qt::UserRole, s.id);
        idItem->setTextAlignment(Qt::AlignCenter);
        m_stationTable->setItem(row, kStationColumnId, idItem);

        auto *nameItem = new QTableWidgetItem(s.name);
        nameItem->setData(Qt::UserRole, s.id);
        m_stationTable->setItem(row, kStationColumnName, nameItem);

        m_stationTable->setItem(row, kStationColumnAddress, new QTableWidgetItem(s.address));
        m_stationTable->setItem(row, kStationColumnLng,
                                new QTableWidgetItem(QString::number(s.longitude, 'f', 6)));
        m_stationTable->setItem(row, kStationColumnLat,
                                new QTableWidgetItem(QString::number(s.latitude, 'f', 6)));
        m_stationTable->setItem(row, kStationColumnTotal,
                                new QTableWidgetItem(QString::number(s.totalPiles)));
        m_stationTable->setItem(row, kStationColumnRate,
                                new QTableWidgetItem(
                                    QStringLiteral("%1%").arg(QString::number(s.onlineRate, 'f', 1))));
    }

    statusBar()->showMessage(QStringLiteral("共 %1 座充电站").arg(stations.size()), 3000);
}

int MainWindow::selectedStationId() const
{
    const int row = m_stationTable->currentRow();
    if (row < 0)
        return -1;
    const QTableWidgetItem *nameItem = m_stationTable->item(row, kStationColumnName);
    if (!nameItem)
        return -1;
    return nameItem->data(Qt::UserRole).toInt();
}
