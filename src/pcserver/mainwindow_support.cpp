#include "mainwindow_support.h"
namespace pcserver_ui {

using namespace pcserver_admin;

QString pileStateText(int state)
{
    switch (state) {
    case 0:
        return QStringLiteral("闲置");
    case 1:
        return QStringLiteral("充电中");
    case 2:
        return QStringLiteral("故障");
    default:
        return QStringLiteral("未知");
    }
}

QString orderStateText(int state)
{
    switch (state) {
    case 0:
        return QStringLiteral("进行中");
    case 1:
        return QStringLiteral("已完成");
    case 2:
        return QStringLiteral("已取消");
    default:
        return QStringLiteral("未知");
    }
}

QString userStatusText(int status)
{
    return status == 1 ? QStringLiteral("冻结") : QStringLiteral("正常");
}

/** 将 LIKE 用户输入中的通配符转义为字面量（参数化之外的第二道防通配符注入） */


/** NO.18 状态语义色：闲置=成功绿 / 充电中=主蓝 / 故障=危险红（与全局主题色板一致） */
QColor pileStateColor(int state)
{
    switch (state) {
    case 0:
        return QColor(QStringLiteral("#52C41A"));
    case 1:
        return QColor(QStringLiteral("#1890FF"));
    case 2:
        return QColor(QStringLiteral("#FF4D4F"));
    default:
        return QColor(QStringLiteral("#8FA0B2"));
    }
}

/** 订单状态语义色：进行中=主蓝 / 已完成=成功绿 / 已取消=中性灰 */
QColor orderStateColor(int state)
{
    switch (state) {
    case 0:
        return QColor(QStringLiteral("#1890FF"));
    case 1:
        return QColor(QStringLiteral("#52C41A"));
    case 2:
        return QColor(QStringLiteral("#8FA0B2"));
    default:
        return QColor(QStringLiteral("#8FA0B2"));
    }
}

/** 在线率语义色：健康=绿 / 波动=蓝 / 偏低=告警 / 全离线=红 */
QColor onlineRateColor(double rate)
{
    if (rate >= 90.0) {
        return QColor(QStringLiteral("#52C41A"));
    }
    if (rate >= 50.0) {
        return QColor(QStringLiteral("#1890FF"));
    }
    if (rate > 0.0) {
        return QColor(QStringLiteral("#FAAD14"));
    }
    return QColor(QStringLiteral("#FF4D4F"));
}

void tintStatusItem(QTableWidgetItem *item, const QColor &color)
{
    if (item) {
        item->setForeground(QBrush(color));
    }
}

QString durationText(int seconds)
{
    return QStringLiteral("%1 h").arg(seconds / 3600.0, 0, 'f', 1);
}

QString moneyText(double value)
{
    return cp::money(value);
}

QString socketServerTimeText()
{
    return QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QJsonObject socketResponseEnvelope(int code, const QString &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("code"), code);
    object.insert(QStringLiteral("message"), message);
    object.insert(QStringLiteral("server_time"), socketServerTimeText());
    return object;
}

bool parseSocketJsonObject(const QByteArray &body, QJsonObject *out)
{
    if (!out)
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *out = document.object();
    return true;
}

QByteArray socketJsonCompact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void clearLayout(QLayout *layout)
{
    if (!layout) {
        return;
    }

    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            delete widget;
        }
        if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

QFrame *createMetricCard(const QString &title, QLabel **valueLabel,
                         const QString &metricTone)
{
    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    card->setObjectName(QStringLiteral("metricCard"));
    card->setMinimumHeight(76);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("metricTitle"));

    auto *value = new QLabel(QStringLiteral("--"), card);
    value->setObjectName(QStringLiteral("metricValue"));
    value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (!metricTone.isEmpty()) {
        value->setProperty("metricTone", metricTone);
    }

    layout->addWidget(titleLabel);
    layout->addWidget(value);

    if (valueLabel) {
        *valueLabel = value;
    }
    return card;
}

QTableWidgetItem *makeItem(const QString &text, const QVariant &userData)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    if (userData.isValid()) {
        item->setData(Qt::UserRole, userData);
    }
    return item;
}

int rowId(const QTableWidget *table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) {
        return -1;
    }
    const auto *item = table->item(row, 0);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

int findRowById(const QTableWidget *table, int id)
{
    if (!table) {
        return -1;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        if (rowId(table, row) == id) {
            return row;
        }
    }
    return -1;
}

void configurePileTable(QTableWidget *table)
{
    table->setColumnCount(8);
    table->setHorizontalHeaderLabels({
        QStringLiteral("ID"),
        QStringLiteral("电站"),
        QStringLiteral("编码"),
        QStringLiteral("类型"),
        QStringLiteral("功率(kW)"),
        QStringLiteral("状态"),
        QStringLiteral("充电次数"),
        QStringLiteral("累计时长")
    });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setColumnHidden(0, true);
}

void configureOrdersTable(QTableWidget *table)
{
    table->setColumnCount(10);
    table->setHorizontalHeaderLabels({
        QStringLiteral("编号"),
        QStringLiteral("用户"),
        QStringLiteral("电站"),
        QStringLiteral("电桩"),
        QStringLiteral("开始"),
        QStringLiteral("结束"),
        QStringLiteral("电量(kWh)"),
        QStringLiteral("单价"),
        QStringLiteral("金额"),
        QStringLiteral("状态")
    });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void configureUserTable(QTableWidget *table)
{
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({
        QStringLiteral("用户ID"),
        QStringLiteral("手机号"),
        QStringLiteral("昵称"),
        QStringLiteral("钱包余额(元)"),
        QStringLiteral("注册时间"),
        QStringLiteral("状态")
    });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void fillUserTable(QTableWidget *table, const QVector<UserRow> &rows)
{
    QSignalBlocker blocker(table);
    table->clearContents();
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const UserRow &user = rows.at(row);
        table->setItem(row, 0, makeItem(QString::number(user.id), user.id));
        table->setItem(row, 1, makeItem(user.phone));
        table->setItem(row, 2, makeItem(user.nickname));
        table->setItem(row, 3, makeItem(moneyText(user.balance)));
        table->setItem(row, 4, makeItem(user.gmtCreate));
        table->setItem(row, 5, makeItem(userStatusText(user.status), user.status));
        tintStatusItem(table->item(row, 5),
                       user.status == 1
                           ? QColor(QStringLiteral("#FAAD14"))   // 冻结=告警
                           : QColor(QStringLiteral("#52C41A"))); // 正常=成功
    }
}

void fillPileTable(QTableWidget *table, const QVector<PileRow> &rows)
{
    QSignalBlocker blocker(table);
    table->clearContents();
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto &item = rows.at(row);
        table->setItem(row, 0, makeItem(QString::number(item.id), item.id));
        table->setItem(row, 1, makeItem(item.stationName, item.stationId));
        table->setItem(row, 2, makeItem(item.code));
        table->setItem(row, 3, makeItem(item.type));
        table->setItem(row, 4, makeItem(QString::number(item.powerKw, 'f', 1)));
        table->setItem(row, 5, makeItem(pileStateText(item.state), item.state));
        table->setItem(row, 6, makeItem(QString::number(item.chargeCount)));
        table->setItem(row, 7, makeItem(durationText(item.chargeSeconds)));
        tintStatusItem(table->item(row, 5), pileStateColor(item.state));
    }
}

void fillOrdersTable(QTableWidget *table, const QVector<OrderRow> &rows)
{
    QSignalBlocker blocker(table);
    table->clearContents();
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto &item = rows.at(row);
        table->setItem(row, 0, makeItem(QString::number(item.id), item.id));
        table->setItem(row, 1, makeItem(item.userName));
        table->setItem(row, 2, makeItem(item.stationName));
        table->setItem(row, 3, makeItem(item.pileCode));
        table->setItem(row, 4, makeItem(item.startTime));
        table->setItem(row, 5, makeItem(item.endTime.isEmpty() ? QStringLiteral("进行中") : item.endTime));
        table->setItem(row, 6, makeItem(QString::number(item.kwh, 'f', 1)));
        table->setItem(row, 7, makeItem(moneyText(item.price)));
        table->setItem(row, 8, makeItem(moneyText(item.amount)));
        table->setItem(row, 9, makeItem(orderStateText(item.state)));
        tintStatusItem(table->item(row, 9), orderStateColor(item.state));
    }
}

void fillStationCombo(QComboBox *combo, const QVector<StationRow> &stations)
{
    QSignalBlocker blocker(combo);
    const int currentId = combo->currentData().toInt();
    combo->clear();
    for (const auto &station : stations) {
        combo->addItem(QStringLiteral("%1").arg(station.name), station.id);
    }
    if (currentId > 0) {
        const int index = combo->findData(currentId);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    } else if (combo->count() > 0) {
        combo->setCurrentIndex(0);
    }
}

/** NO.18：QChart 走原生 API 配深色底/浅色文字，避免 QSS 触碰 QChartView 宿主 */
void setChartTheme(QChart *chart)
{
    if (!chart) {
        return;
    }
    chart->setBackgroundBrush(QBrush(QColor(QStringLiteral("#111A26"))));
    chart->setBackgroundRoundness(8.0);
    chart->setTitleBrush(QBrush(QColor(QStringLiteral("#E6EDF3"))));
    if (chart->legend()) {
        chart->legend()->setLabelBrush(QBrush(QColor(QStringLiteral("#C7D2DE"))));
    }
}

void setChartAxisTheme(QAbstractAxis *axis)
{
    if (!axis) {
        return;
    }
    axis->setLabelsColor(QColor(QStringLiteral("#9FB0C2")));
    axis->setTitleBrush(QBrush(QColor(QStringLiteral("#B6C2CF"))));
    axis->setGridLineColor(QColor(QStringLiteral("#263244")));
}

void fillRevenueChart(QChartView *view, const QVector<RevenuePoint> &points, int days)
{
    auto *chart = new QChart;
    chart->setTitle(QStringLiteral("近%1天营收趋势").arg(days));
    chart->legend()->hide();
    setChartTheme(chart);

    auto *series = new QLineSeries(chart);
    series->setName(QStringLiteral("营收"));
    series->setPointsVisible(true);
    QPen pen(QColor(QStringLiteral("#1890FF")));
    pen.setWidthF(2.5);
    series->setPen(pen);

    double maxValue = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        series->append(i, points.at(i).amount);
        maxValue = std::max(maxValue, points.at(i).amount);
    }

    auto *axisX = new QCategoryAxis(chart);
    for (int i = 0; i < points.size(); ++i) {
        axisX->append(points.at(i).label, i);
    }
    axisX->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    axisX->setRange(0, std::max(0, static_cast<int>(points.size()) - 1));
    setChartAxisTheme(axisX);

    auto *axisY = new QValueAxis(chart);
    axisY->setTitleText(QStringLiteral("元"));
    axisY->setLabelFormat(QStringLiteral("%.0f"));
    axisY->setRange(0.0, std::max(100.0, maxValue * 1.25));
    axisY->setTickCount(6);
    setChartAxisTheme(axisY);

    chart->addSeries(series);
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    chart->setMargins(QMargins(8, 4, 8, 4));

    view->setChart(chart);
    view->setRenderHint(QPainter::Antialiasing, true);
}

QDateTime currentHourAnchor()
{
    const QDateTime now = QDateTime::currentDateTime();
    return QDateTime(now.date(), QTime(now.time().hour(), 0));
}

/** NO.17 负荷预测曲线：历史实测（蓝实线）+ 未来预测（橙虚线）+ 当前时刻分隔线 */
void fillLoadForecastChart(QChartView *view,
                           const QString &stationName,
                           const QDateTime &anchorHour,
                           const QVector<double> &history,
                           const cp::LoadForecastResult &result)
{
    auto *chart = new QChart;
    chart->setTitle(QStringLiteral("%1 ｜ 近 %2 小时实测与未来 %3 小时预测（%4）")
                        .arg(stationName)
                        .arg(history.size())
                        .arg(result.forecastKw.size())
                        .arg(result.modelName));
    chart->setMargins(QMargins(8, 4, 8, 4));
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    setChartTheme(chart);

    auto *historySeries = new QLineSeries(chart);
    historySeries->setName(QStringLiteral("历史实测负荷"));
    QPen historyPen(QColor(QStringLiteral("#1890FF")));
    historyPen.setWidthF(2.5);
    historySeries->setPen(historyPen);

    auto *forecastSeries = new QLineSeries(chart);
    forecastSeries->setName(QStringLiteral("未来预测负荷"));
    QPen forecastPen(QColor(QStringLiteral("#FAAD14")));
    forecastPen.setWidthF(3.0);
    forecastPen.setStyle(Qt::DashLine);
    forecastSeries->setPen(forecastPen);
    forecastSeries->setPointsVisible(true);

    auto *nowSeries = new QLineSeries(chart);
    nowSeries->setName(QStringLiteral("当前时刻"));
    QPen nowPen(QColor(QStringLiteral("#64748B")));
    nowPen.setWidthF(1.5);
    nowPen.setStyle(Qt::DashLine);
    nowSeries->setPen(nowPen);

    double maxValue = 0.0;
    const int n = history.size();
    for (int i = 0; i < n; ++i) {
        historySeries->append(anchorHour.addSecs((i - n + 1) * 3600).toMSecsSinceEpoch(),
                              history.at(i));
        maxValue = std::max(maxValue, history.at(i));
    }
    for (int h = 0; h < result.forecastKw.size(); ++h) {
        forecastSeries->append(anchorHour.addSecs((h + 1) * 3600).toMSecsSinceEpoch(),
                               result.forecastKw.at(h));
        maxValue = std::max(maxValue, result.forecastKw.at(h));
    }
    const double yMax = std::max(100.0, maxValue * 1.2);
    const qint64 nowMs = anchorHour.toMSecsSinceEpoch();
    nowSeries->append(nowMs, 0.0);
    nowSeries->append(nowMs, yMax * 0.95);

    chart->addSeries(historySeries);
    chart->addSeries(forecastSeries);
    chart->addSeries(nowSeries);

    auto *axisX = new QDateTimeAxis(chart);
    axisX->setFormat(QStringLiteral("HH:00"));
    axisX->setTitleText(QStringLiteral("时间"));
    axisX->setRange(anchorHour.addSecs((1 - n) * 3600),
                    anchorHour.addSecs(result.forecastKw.size() * 3600));
    axisX->setTickCount(std::min(
        8, 4 + (n + static_cast<int>(result.forecastKw.size())) / 5));
    axisX->setGridLineVisible(true);
    setChartAxisTheme(axisX);

    auto *axisY = new QValueAxis(chart);
    axisY->setTitleText(QStringLiteral("负荷 (kW)"));
    axisY->setRange(0.0, yMax);
    axisY->setTickCount(6);
    axisY->setLabelFormat(QStringLiteral("%.0f"));
    setChartAxisTheme(axisY);

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    historySeries->attachAxis(axisX);
    historySeries->attachAxis(axisY);
    forecastSeries->attachAxis(axisX);
    forecastSeries->attachAxis(axisY);
    nowSeries->attachAxis(axisX);
    nowSeries->attachAxis(axisY);

    view->setChart(chart);
    view->setRenderHint(QPainter::Antialiasing, true);
}

/**
 * P0 统一数据源：由 main.cpp 注入与 StationStore 完全相同的库路径。
 * 注入后管理后台（登录/业绩/桩状态/桩管理/用户管理）与 Socket 服务、大屏 API、
 * 定价引擎、自愈服务读写同一个 SQLite 文件，五端数据链路才真正闭环。
 */




bool buildLoginDialog(QWidget *parent, QString *userName)
{
    QString error;
    if (!DatabaseManager::instance().initialize(&error)) {
        QMessageBox::critical(parent, QStringLiteral("数据库初始化失败"), error);
        return false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("管理员登录"));
    dialog.setModal(true);
    dialog.setMinimumWidth(360);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("请输入管理员账号"), &dialog);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *form = new QFormLayout;
    auto *usernameEdit = new QLineEdit(&dialog);
    auto *passwordEdit = new QLineEdit(&dialog);
    usernameEdit->setText(QStringLiteral("admin"));
    passwordEdit->setText(QStringLiteral("123456"));
    passwordEdit->setEchoMode(QLineEdit::Password);
    usernameEdit->setPlaceholderText(QStringLiteral("用户名"));
    passwordEdit->setPlaceholderText(QStringLiteral("密码"));
    form->addRow(QStringLiteral("用户名"), usernameEdit);
    form->addRow(QStringLiteral("密码"), passwordEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("登录"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("退出"));

    layout->addWidget(title);
    layout->addLayout(form);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString username = usernameEdit->text().trimmed();
        const QString password = passwordEdit->text();
        if (username.isEmpty() || password.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("登录失败"), QStringLiteral("请输入用户名和密码。"));
            return;
        }
        if (!DatabaseManager::instance().verifyAdmin(username, password, &error)) {
            QMessageBox::warning(&dialog, QStringLiteral("登录失败"), error.isEmpty() ? QStringLiteral("管理员账号或密码不正确。") : error);
            return;
        }
        if (userName) {
            *userName = username;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    usernameEdit->selectAll();
    usernameEdit->setFocus();
    return dialog.exec() == QDialog::Accepted;
}

} // namespace
