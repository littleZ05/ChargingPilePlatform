#include "mainwindow.h"
#include "admin_repository.h"
#include "mainwindow_support.h"
#include "mainwindow_p.h"
#include "ui_mainwindow.h"

#include <algorithm>

#include <QtCharts/QCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QFont>
#include <QMargins>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSplitter>
#include <QTcpSocket>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "addstationdialog.h"
#include "common.h"
#include "dashboard_api.h"
#include "loadforecast.h"
#include "net_server.h"
#include "opsconsole.h"
#include "charge_service.h"
#include "forecast_async.h"
#include "pricingservice.h"
#include <QFutureWatcher>
#include "stationstore.h"
#include "uitheme.h"

using namespace pcserver_admin;
using namespace pcserver_ui;


namespace pcserver {

void setAdminDatabasePath(const QString &path)
{
    adminDbPathOverride() = path;
}

bool showAdminLogin(QWidget *parent, QString *userName)
{
    return buildLoginDialog(parent, userName);
}

} // namespace pcserver



MainWindow::MainWindow(pcserver::StationStore *store, QWidget *parent)
    : MainWindow(store, QStringLiteral("管理员"), parent)
{
}

MainWindow::MainWindow(const QString &adminName, QWidget *parent)
    : MainWindow(nullptr, adminName, parent)
{
}

MainWindow::MainWindow(pcserver::StationStore *store, const QString &adminName, QWidget *parent)
    : QMainWindow(parent)
    , d(new Private)
{
    // NO.18：主窗口入口亦统一注入内置主题（独立运行/单元测试场景同样生效，
    // 资源缺失时内部安全兜底，不影响初始化）
    pcserver::applyUiTheme(qApp);

    d->store = store;
    if(store) {
        QString error;
        if(!DatabaseManager::instance().attach(*store,&error))
            qWarning() << "[admin repository]" << error;
    }
    d->adminName = adminName;
    d->ui = new Ui::MainWindow;
    d->ui->setupUi(this);
    buildUi();
    startSocketServer();
    startDashboardServer();
    refreshAll();
    statusBar()->showMessage(
        QStringLiteral("登录成功：%1%2")
            .arg(adminName,
                 QStringLiteral("；Socket 端口 %1%2")
                     .arg(cp::kServerPort)
                     .arg(d->socketStarted ? QStringLiteral(" 已监听")
                                           : QStringLiteral(" 启动失败")))
            + (d->dashboardStarted
                   ? QStringLiteral("；大屏数据 http://127.0.0.1:%1")
                         .arg(d->dashboardServer->port())
                   : QStringLiteral("；大屏数据服务未启动")),
        6000);
}

MainWindow::~MainWindow()
{
    if (d->netServer) {
        d->netServer->stopServer();
        delete d->netServer; // 已从本窗口子对象链移除，避免析构重复释放
        d->netServer = nullptr;
    }
    if (d->dashboardServer) {
        d->dashboardServer->stop();
        delete d->dashboardServer;
        d->dashboardServer = nullptr;
    }
    if(d->store)
        DatabaseManager::instance().detach(d->store->connectionName());
    delete d->ui;
    delete d;
}



void MainWindow::refreshAll()
{
    QString error;
    if (!DatabaseManager::instance().initialize(&error)) {
        statusBar()->showMessage(QStringLiteral("数据库初始化失败：%1").arg(error), 8000);
    }
    refreshSales();
    refreshPileStatus();
    refreshPileManagement();
    refreshUsers();
    refreshStations();
    refreshLoadForecast();
    updateActionButtons();
    d->adminLabel->setText(QStringLiteral("当前管理员：%1").arg(d->adminName));
}

void MainWindow::refreshSales()
{
    QString error;
    const int days = d->salesRangeCombo->currentData().toInt();
    const SalesSummary summary = DatabaseManager::instance().salesSummary(&error);
    d->salesTodayValue->setText(QStringLiteral("%1 元").arg(moneyText(summary.today)));
    d->salesMonthValue->setText(QStringLiteral("%1 元").arg(moneyText(summary.month)));
    d->salesTotalValue->setText(QStringLiteral("%1 元").arg(moneyText(summary.total)));

    QVector<RevenuePoint> points = DatabaseManager::instance().revenueSeries(days, &error);
    if (points.isEmpty()) {
        points.push_back({ QStringLiteral("今天"), 0.0 });
    }
    fillRevenueChart(d->salesChartView, points, days);
    fillOrdersTable(d->ordersTable, DatabaseManager::instance().recentOrders(12, &error));
}

void MainWindow::refreshLoadForecast()
{
    if (!d->forecastChartView || !d->forecastStationCombo || !d->forecastStatusLabel)
        return;
    if (!d->store || !d->store->isOpen()) {
        d->forecastStatusLabel->setText(QStringLiteral("负荷预测不可用：数据库未就绪"));
        return;
    }

    QString error;
    const QVector<pcserver::StationInfo> stations = d->store->listStations();
    if (stations.isEmpty()) {
        d->forecastStationCombo->clear();
        d->forecastStatusLabel->setText(QStringLiteral("暂无充电站数据，请先添加电站"));
        return;
    }

    const int keepStationId = d->forecastStationCombo->currentData().toInt();
    {
        QSignalBlocker blocker(d->forecastStationCombo);
        d->forecastStationCombo->clear();
        for (const auto &station : stations)
            d->forecastStationCombo->addItem(station.name, station.id);
        const int index = d->forecastStationCombo->findData(keepStationId);
        d->forecastStationCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    const int stationId = d->forecastStationCombo->currentData().toInt();
    QString stationName = d->forecastStationCombo->currentText();
    for (const auto &station : stations) {
        if (station.id == stationId) {
            stationName = station.name;
            break;
        }
    }

    const int hours = qMax(12, d->forecastWindowCombo->currentData().toInt());
    const int horizon = qBound(1, d->forecastHorizonCombo->currentData().toInt(), 24);
    const auto model = static_cast<cp::ForecastModel>(
        d->forecastModelCombo->currentData().toInt());

    bool usedDemoFallback = false;
    int imputedHours = 0;
    const QVector<double> history =
        d->store->hourlyLoadSamples(stationId, hours, &usedDemoFallback, &error, &imputedHours);
    if (history.size() != hours) {
        d->forecastStatusLabel->setText(
            QStringLiteral("历史负荷采样失败：%1").arg(error.isEmpty() ? QStringLiteral("未知错误") : error));
        return;
    }

    const double capacityKw = d->store->ratedCapacityKw(stationId, &error);
    cp::LoadForecastInput input;
    input.historyKw = history;
    input.horizonHours = horizon;
    input.model = model;
    input.capacityKw = capacityKw;
    const quint64 generation = ++m_forecastGeneration;
    const double currentKw = d->store->currentLoadKw(stationId, &error);
    auto *watcher = new QFutureWatcher<cp::ForecastCalculation>(this);
    d->forecastStatusLabel->setText(QStringLiteral("后台计算中…"));
    connect(watcher, &QFutureWatcher<cp::ForecastCalculation>::finished, this,
            [this, watcher, generation, history, stationName, usedDemoFallback,
             currentKw, capacityKw, imputedHours] {
        const auto calculated = watcher->result();
        watcher->deleteLater();
        if (generation != m_forecastGeneration)
            return; // A newer station/model selection owns the screen.
        const auto &result = calculated.result;
        if (!result.ok) {
            d->forecastStatusLabel->setText(QStringLiteral("预测失败：%1").arg(result.error));
            return;
        }
        fillLoadForecastChart(d->forecastChartView, stationName, currentHourAnchor(), history, result);
        const QString source = usedDemoFallback ? QStringLiteral("演示采样") : QStringLiteral("功率记录聚合（%1小时插补）").arg(imputedHours);
        const bool warning = capacityKw > 0 && result.peakForecastKw / capacityKw >= 0.8;
        d->forecastStatusLabel->setText(QStringLiteral(
            "当前 %1 kW ｜预测模型：%2 ｜ %3 ｜峰值预测 %4 kW（未来第%5小时）｜数据源：%6｜%7")
            .arg(currentKw,0,'f',1).arg(result.modelName).arg(cp::loadTrendText(result.trend))
            .arg(result.peakForecastKw,0,'f',1).arg(result.peakHourOffset).arg(source)
            .arg(warning ? QStringLiteral("负荷预警：预测峰值达容量80%") : QStringLiteral("预测负荷正常")));
        qInfo() << "[forecast] calculation worker thread" << calculated.threadId;
    });
    watcher->setFuture(cp::forecastAsync(input));
}

void MainWindow::refreshPileStatus()
{
    QString error;
    const QVector<PileRow> allRows = DatabaseManager::instance().pileRows(-1, &error);
    int total = 0;
    int idle = 0;
    int charging = 0;
    int fault = 0;
    for (const auto &row : allRows) {
        ++total;
        if (row.state == 0) {
            ++idle;
        } else if (row.state == 1) {
            ++charging;
        } else if (row.state == 2) {
            ++fault;
        }
    }

    d->statusTotalValue->setText(QString::number(total));
    d->statusIdleValue->setText(QString::number(idle));
    d->statusChargingValue->setText(QString::number(charging));
    d->statusFaultValue->setText(QString::number(fault));

    const int filter = d->statusFilterCombo->currentData().toInt();
    const QVector<PileRow> filteredRows = DatabaseManager::instance().pileRows(filter, &error);
    fillPileTable(d->statusTable, filteredRows);

    if (d->statusTable->rowCount() > 0) {
        int row = d->selectedStatusPileId > 0 ? findRowById(d->statusTable, d->selectedStatusPileId) : -1;
        if (row < 0) {
            row = 0;
        }
        d->statusTable->selectRow(row);
        d->selectedStatusPileId = rowId(d->statusTable, row);
    } else {
        d->selectedStatusPileId = -1;
    }
    updateActionButtons();
}

void MainWindow::refreshPileManagement()
{
    QString error;
    fillStationCombo(d->manageStationCombo, DatabaseManager::instance().stations(&error));

    const QVector<PileRow> rows = DatabaseManager::instance().pileRows(-1, &error);
    fillPileTable(d->manageTable, rows);

    if (d->manageTable->rowCount() > 0) {
        int row = d->selectedManagePileId > 0 ? findRowById(d->manageTable, d->selectedManagePileId) : -1;
        if (row < 0) {
            row = 0;
        }
        d->manageTable->selectRow(row);
        d->selectedManagePileId = rowId(d->manageTable, row);
        loadManageFormFromSelection();
    } else {
        clearManageForm();
    }
    updateActionButtons();
}

void MainWindow::clearManageForm()
{
    d->selectedManagePileId = -1;
    if (d->manageStationCombo->count() > 0) {
        d->manageStationCombo->setCurrentIndex(0);
    }
    d->manageCodeEdit->clear();
    if (d->manageTypeCombo->count() > 0) {
        d->manageTypeCombo->setCurrentIndex(0);
    }
    d->managePowerSpin->setValue(60.0);
    if (d->manageStateCombo->count() > 0) {
        d->manageStateCombo->setCurrentIndex(0);
    }
    if (d->manageTable) {
        QSignalBlocker blocker(d->manageTable);
        d->manageTable->clearSelection();
    }
    updateActionButtons();
}

void MainWindow::loadManageFormFromSelection()
{
    const int row = d->manageTable->currentRow();
    if (row < 0) {
        clearManageForm();
        return;
    }

    d->selectedManagePileId = rowId(d->manageTable, row);
    const int stationId = d->manageTable->item(row, 1)->data(Qt::UserRole).toInt();
    const QString code = d->manageTable->item(row, 2)->text();
    const QString type = d->manageTable->item(row, 3)->text();
    const double powerKw = d->manageTable->item(row, 4)->text().toDouble();
    const int state = d->manageTable->item(row, 5)->data(Qt::UserRole).toInt();

    const int stationIndex = d->manageStationCombo->findData(stationId);
    if (stationIndex >= 0) {
        d->manageStationCombo->setCurrentIndex(stationIndex);
    }
    d->manageCodeEdit->setText(code);
    const int typeIndex = d->manageTypeCombo->findText(type);
    if (typeIndex >= 0) {
        d->manageTypeCombo->setCurrentIndex(typeIndex);
    }
    d->managePowerSpin->setValue(powerKw);
    const int stateIndex = d->manageStateCombo->findData(state);
    if (stateIndex >= 0) {
        d->manageStateCombo->setCurrentIndex(stateIndex);
    }
    updateActionButtons();
}

void MainWindow::submitManagePile(bool updateExisting)
{
    const int stationId = d->manageStationCombo->currentData().toInt();
    const QString code = d->manageCodeEdit->text().trimmed();
    const QString type = d->manageTypeCombo->currentText();
    const double powerKw = d->managePowerSpin->value();
    const int state = d->manageStateCombo->currentData().toInt();

    if (stationId <= 0) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("请选择所属电站。"));
        return;
    }
    if (code.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("请输入电桩编码。"));
        return;
    }

    QString error;
    int newId = -1;
    bool ok = false;
    if (updateExisting) {
        if (d->selectedManagePileId <= 0) {
            QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("请先选择要更新的电桩。"));
            return;
        }
        ok = DatabaseManager::instance().updatePile(d->selectedManagePileId, stationId, code, type, powerKw, state, &error);
        newId = d->selectedManagePileId;
    } else {
        ok = DatabaseManager::instance().addPile(stationId, code, type, powerKw, state, &newId, &error);
    }

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }

    d->selectedManagePileId = newId;
    statusBar()->showMessage(updateExisting ? QStringLiteral("电桩已更新") : QStringLiteral("电桩已新增"), 5000);
    refreshAll();
    if (d->selectedManagePileId > 0) {
        const int row = findRowById(d->manageTable, d->selectedManagePileId);
        if (row >= 0) {
            d->manageTable->selectRow(row);
            loadManageFormFromSelection();
        }
    }
}

void MainWindow::changeSelectedPileState(int state)
{
    if (d->selectedStatusPileId <= 0) {
        QMessageBox::warning(this, QStringLiteral("操作失败"), QStringLiteral("请先选择一个电桩。"));
        return;
    }

    QString error;
    if (!DatabaseManager::instance().setPileState(d->selectedStatusPileId, state, &error)) {
        QMessageBox::warning(this, QStringLiteral("操作失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("电桩状态已更新为 %1").arg(pileStateText(state)), 5000);
    refreshAll();
}

void MainWindow::sendRemoteRestart()
{
    if (d->selectedManagePileId <= 0) {
        QMessageBox::warning(this, QStringLiteral("操作失败"), QStringLiteral("请先选择一个电桩。"));
        return;
    }

    QString error;
    QString message;
    if (!DatabaseManager::instance().remoteRestartPile(d->selectedManagePileId, &message, &error)) {
        QMessageBox::warning(this, QStringLiteral("操作失败"), error);
        return;
    }
    statusBar()->showMessage(message, 5000);
    refreshAll();
}

void MainWindow::updateActionButtons()
{
    const bool hasStatusSelection = d->selectedStatusPileId > 0;
    const bool hasManageSelection = d->selectedManagePileId > 0;
    const bool hasUserSelection = d->selectedUserId > 0 && d->selectedUserStatus >= 0;
    d->statusSetIdleButton->setEnabled(hasStatusSelection);
    d->statusSetChargingButton->setEnabled(hasStatusSelection);
    d->statusSetFaultButton->setEnabled(hasStatusSelection);
    d->manageUpdateButton->setEnabled(hasManageSelection);
    d->manageDeleteButton->setEnabled(hasManageSelection);
    d->manageRestartButton->setEnabled(hasManageSelection);
    if (d->userFreezeButton) {
        d->userFreezeButton->setEnabled(hasUserSelection && d->selectedUserStatus == 0);
    }
    if (d->userUnfreezeButton) {
        d->userUnfreezeButton->setEnabled(hasUserSelection && d->selectedUserStatus == 1);
    }
}





void MainWindow::refreshUsers()
{
    if (!d->userTable) {
        return;
    }
    QString error;
    const QString keyword =
        d->userSearchEdit ? d->userSearchEdit->text().trimmed() : QString();
    const QVector<UserRow> rows = DatabaseManager::instance().users(keyword, &error);
    if (!error.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("读取用户列表失败：%1").arg(error), 5000);
    }

    const int keepId = d->selectedUserId;
    fillUserTable(d->userTable, rows);
    if (keepId > 0) {
        const int row = findRowById(d->userTable, keepId);
        if (row >= 0) {
            d->userTable->selectRow(row);
        } else {
            d->selectedUserId = -1;
            d->selectedUserStatus = -1;
        }
    }
    statusBar()->showMessage(QStringLiteral("共 %1 位用户").arg(rows.size()), 3000);
    updateActionButtons();
}

void MainWindow::changeSelectedUserStatus(int status)
{
    if (d->selectedUserId <= 0) {
        QMessageBox::warning(this, QStringLiteral("操作失败"),
                             QStringLiteral("请先选择一位用户。"));
        return;
    }
    if (status == d->selectedUserStatus) {
        QMessageBox::warning(this, QStringLiteral("操作失败"),
                             status == 1 ? QStringLiteral("该用户已是冻结状态")
                                         : QStringLiteral("该用户已是正常状态"));
        return;
    }

    QString error;
    if (!DatabaseManager::instance().setUserStatus(d->selectedUserId, status, &error)) {
        QMessageBox::warning(this, QStringLiteral("操作失败"), error);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("用户 %1 已%2")
            .arg(d->selectedUserId)
            .arg(status == 1 ? QStringLiteral("冻结") : QStringLiteral("解冻")),
        5000);
    refreshUsers();
}

void MainWindow::simulateRealtimeOnce()
{
    if (!d->store || !d->store->isOpen()) {
        return;
    }

    const auto stations = d->store->listStations();
    if (stations.isEmpty()) {
        return;
    }

    int stationId = d->currentStationId;
    bool stationExists = false;
    for (const auto &station : stations) {
        if (station.id == stationId) {
            stationExists = true;
            break;
        }
    }
    if (!stationExists) {
        stationId = stations.first().id;
    }

    const auto piles = d->store->listPiles(stationId);
    if (piles.isEmpty()) {
        return;
    }

    const auto &target = piles.at(QRandomGenerator::global()->bounded(piles.size()));
    QString error;
    if (!d->store->setPileState(
            target.id, pcserver::StationStore::nextSimulatedState(target.state), &error)) {
        statusBar()->showMessage(QStringLiteral("实时状态模拟失败：%1").arg(error), 5000);
        return;
    }
    refreshStations();
}

void MainWindow::refreshStations()
{
    if (!d->store || !d->store->isOpen()) {
        return;
    }

    const int keepSelectionId = selectedStationId();
    fillStationTable(d->store->listStations());
    if (keepSelectionId > 0) {
        selectStationById(keepSelectionId);
    }
    refreshPileDetail();
}

void MainWindow::fillStationTable(const QVector<pcserver::StationInfo> &stations)
{
    d->stationTable->setRowCount(stations.size());
    for (int row = 0; row < stations.size(); ++row) {
        const auto &station = stations.at(row);
        auto *idItem = new QTableWidgetItem(QString::number(station.id));
        idItem->setData(Qt::UserRole, station.id);
        idItem->setTextAlignment(Qt::AlignCenter);
        d->stationTable->setItem(row, 0, idItem);

        auto *nameItem = new QTableWidgetItem(station.name);
        nameItem->setData(Qt::UserRole, station.id);
        d->stationTable->setItem(row, 1, nameItem);
        d->stationTable->setItem(row, 2, new QTableWidgetItem(station.address));
        d->stationTable->setItem(row, 3, new QTableWidgetItem(QString::number(station.longitude, 'f', 6)));
        d->stationTable->setItem(row, 4, new QTableWidgetItem(QString::number(station.latitude, 'f', 6)));
        d->stationTable->setItem(row, 5, new QTableWidgetItem(QString::number(station.totalPiles)));
        auto *onlineRateItem =
            new QTableWidgetItem(QStringLiteral("%1%").arg(
                QString::number(station.onlineRate, 'f', 1)));
        onlineRateItem->setTextAlignment(Qt::AlignCenter);
        tintStatusItem(onlineRateItem, onlineRateColor(station.onlineRate));
        d->stationTable->setItem(row, 6, onlineRateItem);
    }
}

void MainWindow::refreshPileDetail()
{
    if (!d->stationPileTable) {
        return;
    }

    d->stationPileTable->setRowCount(0);
    d->currentStationId = selectedStationId();
    if (d->currentStationId <= 0 || !d->store || !d->store->isOpen()) {
        d->stationPileHintLabel->setText(
            QStringLiteral("请在上方列表选择一座充电站查看站内电桩状态"));
        return;
    }

    const auto piles = d->store->listPiles(d->currentStationId);
    d->stationPileTable->setRowCount(piles.size());
    d->stationPileHintLabel->setText(
        QStringLiteral("电站 ID=%1，共 %2 根电桩").arg(d->currentStationId).arg(piles.size()));

    for (int row = 0; row < piles.size(); ++row) {
        const auto &pile = piles.at(row);
        d->stationPileTable->setItem(row, 0, new QTableWidgetItem(QString::number(pile.id)));
        d->stationPileTable->setItem(row, 1, new QTableWidgetItem(pile.code));
        d->stationPileTable->setItem(row, 2, new QTableWidgetItem(pile.type));
        d->stationPileTable->setItem(row, 3, new QTableWidgetItem(QString::number(pile.powerKw, 'f', 1)));
        d->stationPileTable->setItem(
            row, 4, new QTableWidgetItem(pcserver::StationStore::pileStateText(pile.state)));
        tintStatusItem(d->stationPileTable->item(row, 4),
                       pileStateColor(static_cast<int>(pile.state)));
        d->stationPileTable->setItem(row, 5, new QTableWidgetItem(QString::number(pile.chargeCount)));
        d->stationPileTable->setItem(row, 6, new QTableWidgetItem(QString::number(pile.chargeSeconds)));
    }
}

void MainWindow::selectStationById(int stationId)
{
    if (!d->stationTable) {
        return;
    }
    for (int row = 0; row < d->stationTable->rowCount(); ++row) {
        const auto *item = d->stationTable->item(row, 0);
        if (item && item->data(Qt::UserRole).toInt() == stationId) {
            d->stationTable->selectRow(row);
            return;
        }
    }
}

int MainWindow::selectedStationId() const
{
    if (!d->stationTable) {
        return -1;
    }
    const int row = d->stationTable->currentRow();
    if (row < 0) {
        return -1;
    }
    const auto *item = d->stationTable->item(row, 1);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void MainWindow::onAddStationClicked()
{
    if (!d->store || !d->store->isOpen()) {
        return;
    }

    AddStationDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString error;
    const int stationId = d->store->addStation(
        dialog.stationName(), dialog.address(), dialog.longitude(), dialog.latitude(),
        dialog.pileCount(), &error);
    if (stationId <= 0) {
        QMessageBox::warning(this, QStringLiteral("新增电站失败"), error);
        return;
    }

    refreshStations();
    selectStationById(stationId);
    statusBar()->showMessage(
        QStringLiteral("已新增电站「%1」（ID=%2，%3 根电桩）")
            .arg(dialog.stationName())
            .arg(stationId)
            .arg(dialog.pileCount()),
        5000);
}
