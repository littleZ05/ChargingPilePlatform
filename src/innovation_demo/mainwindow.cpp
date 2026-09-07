#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "pricing_engine.h"

#include <QMessageBox>
#include <QStatusBar>
#include <QDir>
#include <QTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("创新点模拟触发演示（NO.22/23）"));

    QString err;
    const QString db = QDir::current().filePath(QStringLiteral("innovation_demo.db"));
    if (!m_store.open(db, &err)) {
        QMessageBox::critical(this, QStringLiteral("数据库初始化失败"), err);
        return;
    }
    m_ready = true;

    connect(ui->btnApply, &QPushButton::clicked, this, &MainWindow::applyPricing);
    connect(ui->btnLow,   &QPushButton::clicked, this, &MainWindow::reportPower);
    connect(ui->btnNormal,&QPushButton::clicked, this, &MainWindow::reportNormalPower);
    connect(ui->stationCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onStationChanged);
    connect(ui->pileCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPileChanged);

    reloadStations();
    reloadPiles();
    statusBar()->showMessage(QStringLiteral("演示库：%1").arg(db), 8000);
    log(QStringLiteral("演示就绪：手动输入“预测空闲率”触发动态计费；"
                       "连续 3 次低功率触发自愈告警（答辩前由 ML 预测接口替换手动输入）。"));
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::reloadStations()
{
    ui->stationCombo->clear();
    for (const DemoStation &s : m_store.stations())
        ui->stationCombo->addItem(QStringLiteral("%1（基础价 %2 元/度）")
                                      .arg(s.name).arg(QString::number(s.basePrice, 'f', 2)),
                                  s.id);
    if (ui->stationCombo->count() > 0)
        onStationChanged(0);
}

void MainWindow::reloadPiles()
{
    ui->pileCombo->clear();
    for (const DemoPile &p : m_store.piles())
        ui->pileCombo->addItem(QStringLiteral("%1（功率 %2 kW）")
                                   .arg(p.code).arg(QString::number(p.powerKw, 'f', 0)),
                               p.id);
    if (ui->pileCombo->count() > 0)
        onPileChanged(0);
}

void MainWindow::onStationChanged(int)
{
    if (!m_ready || ui->stationCombo->currentIndex() < 0)
        return;
    ui->strategyLabel->setText(
        m_store.strategyText(ui->stationCombo->currentData().toInt()));
}

void MainWindow::onPileChanged(int)
{
    if (!m_ready || ui->pileCombo->currentIndex() < 0)
        return;
    const int pid = ui->pileCombo->currentData().toInt();
    ui->thresholdLabel->setText(
        QStringLiteral("低异常阈值：%1 kW（历史均值 80%）")
            .arg(QString::number(m_store.lowThreshold(pid), 'f', 0)));
}

void MainWindow::applyPricing()
{
    const int sid = ui->stationCombo->currentData().toInt();
    const double rate = ui->idleSpin->value();
    QString out;
    m_store.applyStrategy(sid, rate, &out);
    log(out);
    ui->strategyLabel->setText(m_store.strategyText(sid));
}

void MainWindow::reportPower()
{
    const int pid = ui->pileCombo->currentData().toInt();
    const double low = m_store.lowThreshold(pid);
    log(m_store.checkAndHeal(pid, low * 0.5));   // 模拟低于阈值 50%
}

void MainWindow::reportNormalPower()
{
    const int pid = ui->pileCombo->currentData().toInt();
    m_store.resetStreak(pid);
    log(QStringLiteral("模拟功率恢复正常，自愈计数清零，状态 Normal"));
}

void MainWindow::log(const QString &s)
{
    ui->logEdit->appendPlainText(QStringLiteral("[%1] %2")
                                     .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), s));
    statusBar()->showMessage(s, 5000);
}
