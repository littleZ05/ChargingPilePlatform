#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtCharts/QCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QComboBox>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

#include "addstationdialog.h"
#include "common.h"
#include "stationstore.h"

namespace {

constexpr char kConnectionName[] = "pcserver_sqlite_connection";

struct StationRow {
    int id = 0;
    QString name;
    QString address;
    double longitude = 0.0;
    double latitude = 0.0;
    int totalPiles = 0;
    double onlineRate = 0.0;
    double basePrice = 0.0;
};

struct PileInfo {
    int id = 0;
    int stationId = 0;
    QString code;
    QString type;
    double powerKw = 0.0;
};

struct PileRow {
    int id = 0;
    int stationId = 0;
    QString stationName;
    QString code;
    QString type;
    double powerKw = 0.0;
    int state = 0;
    int chargeCount = 0;
    int chargeSeconds = 0;
};

struct OrderRow {
    int id = 0;
    QString userName;
    QString stationName;
    QString pileCode;
    QString startTime;
    QString endTime;
    double kwh = 0.0;
    double price = 0.0;
    double amount = 0.0;
    int state = 0;
};

struct RevenuePoint {
    QString label;
    double amount = 0.0;
};

struct SalesSummary {
    double today = 0.0;
    double month = 0.0;
    double total = 0.0;
};

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

QColor stateColor(int state)
{
    switch (state) {
    case 0:
        return QColor(QStringLiteral("#edf7ed"));
    case 1:
        return QColor(QStringLiteral("#eef4ff"));
    case 2:
        return QColor(QStringLiteral("#fff0f0"));
    default:
        return QColor(QStringLiteral("#f4f4f5"));
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

QFrame *createMetricCard(const QString &title, QLabel **valueLabel)
{
    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    card->setObjectName(QStringLiteral("metricCard"));
    card->setMinimumHeight(76);
    card->setStyleSheet(QStringLiteral(
        "#metricCard { background: #f8fafc; border: 1px solid #dbe3ea; border-radius: 6px; }"
        "#metricTitle { color: #667085; font-size: 12px; }"
        "#metricValue { color: #111827; font-size: 22px; font-weight: 600; }"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("metricTitle"));

    auto *value = new QLabel(QStringLiteral("--"), card);
    value->setObjectName(QStringLiteral("metricValue"));
    value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    layout->addWidget(titleLabel);
    layout->addWidget(value);

    if (valueLabel) {
        *valueLabel = value;
    }
    return card;
}

QTableWidgetItem *makeItem(const QString &text, const QVariant &userData = QVariant())
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    if (userData.isValid()) {
        item->setData(Qt::UserRole, userData);
    }
    return item;
}

void setRowTint(QTableWidget *table, int row, int state)
{
    const QBrush brush(stateColor(state));
    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto *item = table->item(row, col)) {
            item->setBackground(brush);
        }
    }
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
        setRowTint(table, row, item.state);
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
        setRowTint(table, row, item.state);
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

void fillRevenueChart(QChartView *view, const QVector<RevenuePoint> &points, int days)
{
    auto *chart = new QChart;
    chart->setTitle(QStringLiteral("近%1天营收趋势").arg(days));
    chart->legend()->hide();

    auto *series = new QLineSeries(chart);
    series->setName(QStringLiteral("营收"));
    series->setPointsVisible(true);
    QPen pen(QColor(QStringLiteral("#2563eb")));
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

    auto *axisY = new QValueAxis(chart);
    axisY->setTitleText(QStringLiteral("元"));
    axisY->setLabelFormat(QStringLiteral("%.0f"));
    axisY->setRange(0.0, std::max(100.0, maxValue * 1.25));
    axisY->setTickCount(6);

    chart->addSeries(series);
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    chart->setMargins(QMargins(8, 4, 8, 4));

    view->setChart(chart);
    view->setRenderHint(QPainter::Antialiasing, true);
}

class DatabaseManager
{
public:
    static DatabaseManager &instance()
    {
        static DatabaseManager manager;
        return manager;
    }

    bool initialize(QString *error = nullptr)
    {
        if (m_initialized && m_db.isOpen()) {
            return true;
        }
        if (!openDatabase(error)) {
            return false;
        }
        if (!ensureSchema(error)) {
            return false;
        }
        if (!seedDemoData(error)) {
            return false;
        }
        m_initialized = true;
        return true;
    }

    bool verifyAdmin(const QString &username, const QString &password, QString *error = nullptr) const
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT id FROM admins WHERE username = ? AND password = ?"));
        query.addBindValue(username.trimmed());
        query.addBindValue(password);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        return query.next();
    }

    SalesSummary salesSummary(QString *error = nullptr) const
    {
        SalesSummary summary;
        const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString month = QDate::currentDate().toString(QStringLiteral("yyyy-MM"));
        summary.today = scalarDouble(QStringLiteral(
            "SELECT COALESCE(SUM(amount), 0) FROM orders "
            "WHERE state = ? AND substr(end_time, 1, 10) = ?"),
            { static_cast<int>(cp::OrderState::Finished), today }, error);
        summary.month = scalarDouble(QStringLiteral(
            "SELECT COALESCE(SUM(amount), 0) FROM orders "
            "WHERE state = ? AND substr(end_time, 1, 7) = ?"),
            { static_cast<int>(cp::OrderState::Finished), month }, error);
        summary.total = scalarDouble(QStringLiteral(
            "SELECT COALESCE(SUM(amount), 0) FROM orders WHERE state = ?"),
            { static_cast<int>(cp::OrderState::Finished) }, error);
        return summary;
    }

    QVector<RevenuePoint> revenueSeries(int days, QString *error = nullptr) const
    {
        days = std::max(1, days);
        QVector<RevenuePoint> points;
        const QDate startDate = QDate::currentDate().addDays(-(days - 1));
        const QString start = startDate.toString(QStringLiteral("yyyy-MM-dd"));
        const QString end = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

        QHash<QString, double> revenueByDay;
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT substr(end_time, 1, 10) AS day, COALESCE(SUM(amount), 0) "
            "FROM orders WHERE state = ? AND substr(end_time, 1, 10) BETWEEN ? AND ? "
            "GROUP BY day"));
        query.addBindValue(static_cast<int>(cp::OrderState::Finished));
        query.addBindValue(start);
        query.addBindValue(end);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
        } else {
            while (query.next()) {
                revenueByDay.insert(query.value(0).toString(), query.value(1).toDouble());
            }
        }

        for (int i = 0; i < days; ++i) {
            const QDate date = startDate.addDays(i);
            points.push_back({ date.toString(QStringLiteral("MM-dd")), revenueByDay.value(date.toString(QStringLiteral("yyyy-MM-dd")), 0.0) });
        }
        return points;
    }

    QVector<OrderRow> recentOrders(int limit, QString *error = nullptr) const
    {
        QVector<OrderRow> rows;
        limit = std::max(1, limit);
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT o.id, COALESCE(u.nickname, ''), COALESCE(s.name, ''), COALESCE(p.code, ''), "
            "COALESCE(o.start_time, ''), COALESCE(o.end_time, ''), o.kwh, o.price, o.amount, o.state "
            "FROM orders o "
            "LEFT JOIN users u ON u.id = o.user_id "
            "LEFT JOIN piles p ON p.id = o.pile_id "
            "LEFT JOIN stations s ON s.id = o.station_id "
            "ORDER BY COALESCE(o.end_time, o.start_time) DESC, o.id DESC "
            "LIMIT %1").arg(limit));
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return rows;
        }
        while (query.next()) {
            OrderRow row;
            row.id = query.value(0).toInt();
            row.userName = query.value(1).toString();
            row.stationName = query.value(2).toString();
            row.pileCode = query.value(3).toString();
            row.startTime = query.value(4).toString();
            row.endTime = query.value(5).toString();
            row.kwh = query.value(6).toDouble();
            row.price = query.value(7).toDouble();
            row.amount = query.value(8).toDouble();
            row.state = query.value(9).toInt();
            rows.push_back(row);
        }
        return rows;
    }

    QVector<PileRow> pileRows(int stateFilter = -1, QString *error = nullptr) const
    {
        QVector<PileRow> rows;
        QString sql = QStringLiteral(
            "SELECT p.id, p.station_id, COALESCE(s.name, ''), p.code, p.type, p.power_kw, p.state, p.charge_count, p.charge_seconds "
            "FROM piles p LEFT JOIN stations s ON s.id = p.station_id ");
        if (stateFilter >= 0) {
            sql += QStringLiteral("WHERE p.state = %1 ").arg(stateFilter);
        }
        sql += QStringLiteral("ORDER BY p.id ASC");

        QSqlQuery query(m_db);
        if (!query.exec(sql)) {
            if (error) {
                *error = query.lastError().text();
            }
            return rows;
        }
        while (query.next()) {
            PileRow row;
            row.id = query.value(0).toInt();
            row.stationId = query.value(1).toInt();
            row.stationName = query.value(2).toString();
            row.code = query.value(3).toString();
            row.type = query.value(4).toString();
            row.powerKw = query.value(5).toDouble();
            row.state = query.value(6).toInt();
            row.chargeCount = query.value(7).toInt();
            row.chargeSeconds = query.value(8).toInt();
            rows.push_back(row);
        }
        return rows;
    }

    QVector<StationRow> stations(QString *error = nullptr) const
    {
        QVector<StationRow> rows;
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral(
            "SELECT id, name, address, longitude, latitude, total_piles, online_rate, base_price "
            "FROM stations ORDER BY id ASC"))) {
            if (error) {
                *error = query.lastError().text();
            }
            return rows;
        }
        while (query.next()) {
            StationRow row;
            row.id = query.value(0).toInt();
            row.name = query.value(1).toString();
            row.address = query.value(2).toString();
            row.longitude = query.value(3).toDouble();
            row.latitude = query.value(4).toDouble();
            row.totalPiles = query.value(5).toInt();
            row.onlineRate = query.value(6).toDouble();
            row.basePrice = query.value(7).toDouble();
            rows.push_back(row);
        }
        return rows;
    }

    bool addPile(int stationId, const QString &code, const QString &type, double powerKw, int state, int *newId = nullptr, QString *error = nullptr)
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "INSERT INTO piles(station_id, code, type, power_kw, state, charge_count, charge_seconds) "
            "VALUES(?, ?, ?, ?, ?, 0, 0)"));
        query.addBindValue(stationId);
        query.addBindValue(code.trimmed());
        query.addBindValue(type);
        query.addBindValue(powerKw);
        query.addBindValue(state);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        const int id = query.lastInsertId().toInt();
        if (newId) {
            *newId = id;
        }
        return recalculateStationStats(stationId, error);
    }

    bool updatePile(int pileId, int stationId, const QString &code, const QString &type, double powerKw, int state, QString *error = nullptr)
    {
        int oldStationId = -1;
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
        lookup.addBindValue(pileId);
        if (!lookup.exec() || !lookup.next()) {
            if (error) {
                *error = QStringLiteral("未找到要更新的电桩");
            }
            return false;
        }
        oldStationId = lookup.value(0).toInt();

        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "UPDATE piles SET station_id = ?, code = ?, type = ?, power_kw = ?, state = ? WHERE id = ?"));
        query.addBindValue(stationId);
        query.addBindValue(code.trimmed());
        query.addBindValue(type);
        query.addBindValue(powerKw);
        query.addBindValue(state);
        query.addBindValue(pileId);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }

        if (oldStationId == stationId) {
            return recalculateStationStats(stationId, error);
        }
        return recalculateStationStats(oldStationId, error) && recalculateStationStats(stationId, error);
    }

    bool deletePile(int pileId, QString *error = nullptr)
    {
        int stationId = -1;
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
        lookup.addBindValue(pileId);
        if (!lookup.exec() || !lookup.next()) {
            if (error) {
                *error = QStringLiteral("未找到要删除的电桩");
            }
            return false;
        }
        stationId = lookup.value(0).toInt();

        const QStringList statements = {
            QStringLiteral("DELETE FROM pile_power_logs WHERE pile_id = %1").arg(pileId),
            QStringLiteral("DELETE FROM pile_health_metrics WHERE pile_id = %1").arg(pileId),
            QStringLiteral("DELETE FROM orders WHERE pile_id = %1").arg(pileId),
            QStringLiteral("DELETE FROM piles WHERE id = %1").arg(pileId)
        };
        for (const QString &sql : statements) {
            QSqlQuery query(m_db);
            if (!query.exec(sql)) {
                if (error) {
                    *error = query.lastError().text();
                }
                return false;
            }
        }

        return recalculateStationStats(stationId, error);
    }

    bool setPileState(int pileId, int state, QString *error = nullptr)
    {
        int stationId = -1;
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT station_id FROM piles WHERE id = ?"));
        lookup.addBindValue(pileId);
        if (!lookup.exec() || !lookup.next()) {
            if (error) {
                *error = QStringLiteral("未找到要修改的电桩");
            }
            return false;
        }
        stationId = lookup.value(0).toInt();

        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("UPDATE piles SET state = ? WHERE id = ?"));
        query.addBindValue(state);
        query.addBindValue(pileId);
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }

        return recalculateStationStats(stationId, error);
    }

    bool remoteRestartPile(int pileId, QString *message = nullptr, QString *error = nullptr)
    {
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT code FROM piles WHERE id = ?"));
        lookup.addBindValue(pileId);
        if (!lookup.exec() || !lookup.next()) {
            if (error) {
                *error = QStringLiteral("未找到要重启的电桩");
            }
            return false;
        }
        const QString code = lookup.value(0).toString();
        if (!setPileState(pileId, 0, error)) {
            return false;
        }
        if (message) {
            *message = QStringLiteral("已向 %1 发送远程重启指令，电桩已切回闲置").arg(code);
        }
        return true;
    }

private:
    DatabaseManager() = default;

    QString dbPath() const
    {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (base.isEmpty()) {
            base = QDir::homePath() + QStringLiteral("/ChargingPilePlatform");
        }
        QDir dir(base);
        if (!dir.exists()) {
            QDir().mkpath(base);
        }
        return dir.filePath(QStringLiteral("chargingpile-platform.db"));
    }

    bool openDatabase(QString *error)
    {
        const QString path = dbPath();
        const QFileInfo dbInfo(path);
        QDir().mkpath(dbInfo.absolutePath());
        QFile dbFile(dbInfo.absoluteFilePath());
        if (!dbFile.exists()) {
            if (!dbFile.open(QIODevice::WriteOnly)) {
                if (error) {
                    *error = QStringLiteral("无法创建数据库文件 %1：%2")
                                 .arg(dbInfo.absoluteFilePath(), dbFile.errorString());
                }
                return false;
            }
            dbFile.close();
        }
        if (QSqlDatabase::contains(QString::fromLatin1(kConnectionName))) {
            m_db = QSqlDatabase::database(QString::fromLatin1(kConnectionName));
        } else {
            m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QString::fromLatin1(kConnectionName));
        }
        m_db.setDatabaseName(path);
        if (!m_db.open()) {
            if (error) {
                *error = m_db.lastError().text();
            }
            return false;
        }
        QSqlQuery pragma(m_db);
        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        return true;
    }

    bool runStatements(const QStringList &statements, QString *error) const
    {
        for (const QString &sql : statements) {
            QSqlQuery query(m_db);
            if (!query.exec(sql)) {
                if (error) {
                    *error = query.lastError().text();
                }
                return false;
            }
        }
        return true;
    }

    bool ensureSchema(QString *error)
    {
        const QStringList statements = {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS admins ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "username TEXT NOT NULL UNIQUE,"
                "password TEXT NOT NULL,"
                "created_at TEXT DEFAULT (datetime('now','localtime'))"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS users ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "phone TEXT NOT NULL UNIQUE,"
                "nickname TEXT NOT NULL DEFAULT '',"
                "avatar_path TEXT,"
                "balance REAL NOT NULL DEFAULT 0,"
                "status INTEGER NOT NULL DEFAULT 0,"
                "gmt_create TEXT DEFAULT (datetime('now','localtime')),"
                "gmt_modified TEXT DEFAULT (datetime('now','localtime'))"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS stations ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "name TEXT NOT NULL,"
                "address TEXT NOT NULL,"
                "longitude REAL NOT NULL DEFAULT 0,"
                "latitude REAL NOT NULL DEFAULT 0,"
                "total_piles INTEGER NOT NULL DEFAULT 0,"
                "online_rate REAL NOT NULL DEFAULT 0,"
                "base_price REAL NOT NULL DEFAULT 1.0,"
                "gmt_create TEXT DEFAULT (datetime('now','localtime'))"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS piles ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "station_id INTEGER NOT NULL REFERENCES stations(id),"
                "code TEXT NOT NULL UNIQUE,"
                "type TEXT NOT NULL DEFAULT '快充',"
                "power_kw REAL NOT NULL DEFAULT 0,"
                "state INTEGER NOT NULL DEFAULT 0,"
                "charge_count INTEGER NOT NULL DEFAULT 0,"
                "charge_seconds INTEGER NOT NULL DEFAULT 0"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS orders ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "user_id INTEGER NOT NULL REFERENCES users(id),"
                "pile_id INTEGER NOT NULL REFERENCES piles(id),"
                "station_id INTEGER NOT NULL REFERENCES stations(id),"
                "start_time TEXT,"
                "end_time TEXT,"
                "kwh REAL NOT NULL DEFAULT 0,"
                "price REAL NOT NULL DEFAULT 0,"
                "amount REAL NOT NULL DEFAULT 0,"
                "state INTEGER NOT NULL DEFAULT 0"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS marketing_strategy ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "station_id INTEGER NOT NULL REFERENCES stations(id),"
                "base_price REAL NOT NULL DEFAULT 1.0,"
                "discount REAL NOT NULL DEFAULT 1.0,"
                "rule_desc TEXT,"
                "is_active INTEGER NOT NULL DEFAULT 1,"
                "valid_from TEXT,"
                "valid_to TEXT,"
                "gmt_create TEXT DEFAULT (datetime('now','localtime'))"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS pile_health_metrics ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "pile_id INTEGER NOT NULL UNIQUE REFERENCES piles(id),"
                "avg_power REAL NOT NULL DEFAULT 0,"
                "low_threshold REAL NOT NULL DEFAULT 0,"
                "high_threshold REAL NOT NULL DEFAULT 0,"
                "sample_count INTEGER NOT NULL DEFAULT 0,"
                "updated_at TEXT DEFAULT (datetime('now','localtime'))"
                ")"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS pile_power_logs ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "pile_id INTEGER NOT NULL REFERENCES piles(id),"
                "order_id INTEGER REFERENCES orders(id),"
                "real_power REAL NOT NULL DEFAULT 0,"
                "logged_at TEXT DEFAULT (datetime('now','localtime'))"
                ")"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_piles_station ON piles(station_id)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_orders_user ON orders(user_id)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_orders_pile ON orders(pile_id)")
        };
        return runStatements(statements, error);
    }

    int scalarInt(const QString &sql, const QVariantList &binds = {}, QString *error = nullptr) const
    {
        QSqlQuery query(m_db);
        query.prepare(sql);
        for (const QVariant &bind : binds) {
            query.addBindValue(bind);
        }
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return 0;
        }
        if (!query.next()) {
            return 0;
        }
        return query.value(0).toInt();
    }

    double scalarDouble(const QString &sql, const QVariantList &binds = {}, QString *error = nullptr) const
    {
        QSqlQuery query(m_db);
        query.prepare(sql);
        for (const QVariant &bind : binds) {
            query.addBindValue(bind);
        }
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return 0.0;
        }
        if (!query.next()) {
            return 0.0;
        }
        return query.value(0).toDouble();
    }

    int insertRowId(const QString &sql, const QVariantList &binds, QString *error = nullptr)
    {
        QSqlQuery query(m_db);
        query.prepare(sql);
        for (const QVariant &bind : binds) {
            query.addBindValue(bind);
        }
        if (!query.exec()) {
            if (error) {
                *error = query.lastError().text();
            }
            return -1;
        }
        return query.lastInsertId().toInt();
    }

    QVector<PileInfo> currentPileInfos(QString *error = nullptr) const
    {
        QVector<PileInfo> infos;
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral(
            "SELECT p.id, p.station_id, p.code, p.type, p.power_kw "
            "FROM piles p ORDER BY p.id ASC"))) {
            if (error) {
                *error = query.lastError().text();
            }
            return infos;
        }
        while (query.next()) {
            PileInfo info;
            info.id = query.value(0).toInt();
            info.stationId = query.value(1).toInt();
            info.code = query.value(2).toString();
            info.type = query.value(3).toString();
            info.powerKw = query.value(4).toDouble();
            infos.push_back(info);
        }
        return infos;
    }

    QHash<int, double> stationBasePriceMap(QString *error = nullptr) const
    {
        QHash<int, double> map;
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral("SELECT id, base_price FROM stations ORDER BY id ASC"))) {
            if (error) {
                *error = query.lastError().text();
            }
            return map;
        }
        while (query.next()) {
            map.insert(query.value(0).toInt(), query.value(1).toDouble());
        }
        return map;
    }

    bool recalculateStationStats(int stationId, QString *error = nullptr)
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT COUNT(*), COALESCE(SUM(CASE WHEN state <> 2 THEN 1 ELSE 0 END), 0) "
            "FROM piles WHERE station_id = ?"));
        query.addBindValue(stationId);
        if (!query.exec() || !query.next()) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        const int total = query.value(0).toInt();
        const int online = query.value(1).toInt();
        const double onlineRate = total > 0 ? (online * 100.0 / total) : 0.0;

        QSqlQuery update(m_db);
        update.prepare(QStringLiteral("UPDATE stations SET total_piles = ?, online_rate = ? WHERE id = ?"));
        update.addBindValue(total);
        update.addBindValue(onlineRate);
        update.addBindValue(stationId);
        if (!update.exec()) {
            if (error) {
                *error = update.lastError().text();
            }
            return false;
        }
        return true;
    }

    bool recalculateAllStationStats(QString *error = nullptr)
    {
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral("SELECT id FROM stations ORDER BY id ASC"))) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        while (query.next()) {
            if (!recalculateStationStats(query.value(0).toInt(), error)) {
                return false;
            }
        }
        return true;
    }

    bool seedDemoData(QString *error)
    {
        struct StationSeed {
            QString name;
            QString address;
            double longitude;
            double latitude;
            double basePrice;
        };
        const QVector<StationSeed> stationSeeds = {
            { QStringLiteral("东软软件园A站"), QStringLiteral("沈阳市浑南区软件园路1号"), 123.43, 41.75, 1.20 },
            { QStringLiteral("大学城快充站"), QStringLiteral("沈阳市浑南区学城街88号"), 123.44, 41.77, 1.08 },
            { QStringLiteral("园区慢充站"), QStringLiteral("沈阳市和平区创新路18号"), 123.40, 41.79, 0.98 }
        };

        QVector<StationRow> stationRows = stations(error);
        if (stationRows.isEmpty() && scalarInt(QStringLiteral("SELECT COUNT(*) FROM stations"), {}, error) == 0) {
            for (const auto &seed : stationSeeds) {
                const int id = insertRowId(QStringLiteral(
                    "INSERT INTO stations(name, address, longitude, latitude, total_piles, online_rate, base_price) "
                    "VALUES(?, ?, ?, ?, 0, 0, ?)"),
                    { seed.name, seed.address, seed.longitude, seed.latitude, seed.basePrice }, error);
                if (id < 0) {
                    return false;
                }
                stationRows.push_back({ id, seed.name, seed.address, seed.longitude, seed.latitude, 0, 0.0, seed.basePrice });
            }
        }
        if (stationRows.isEmpty()) {
            stationRows = stations(error);
        }

        if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM admins"), {}, error) == 0) {
            if (insertRowId(QStringLiteral("INSERT INTO admins(username, password) VALUES(?, ?)"),
                { QStringLiteral("admin"), QStringLiteral("123456") }, error) < 0) {
                return false;
            }
        }

        struct UserSeed {
            QString phone;
            QString nickname;
            double balance;
        };
        const QVector<UserSeed> userSeeds = {
            { QStringLiteral("13800000001"), QStringLiteral("张晨"), 218.20 },
            { QStringLiteral("13800000002"), QStringLiteral("李婷"), 126.50 },
            { QStringLiteral("13800000003"), QStringLiteral("王磊"), 326.80 },
            { QStringLiteral("13800000004"), QStringLiteral("赵敏"), 95.40 },
            { QStringLiteral("13800000005"), QStringLiteral("周扬"), 452.60 }
        };
        QVector<int> userIds;
        if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM users"), {}, error) == 0) {
            for (const auto &seed : userSeeds) {
                const int id = insertRowId(QStringLiteral(
                    "INSERT INTO users(phone, nickname, balance, status) VALUES(?, ?, ?, 0)"),
                    { seed.phone, seed.nickname, seed.balance }, error);
                if (id < 0) {
                    return false;
                }
                userIds.push_back(id);
            }
        }
        if (userIds.isEmpty()) {
            QSqlQuery query(m_db);
            if (!query.exec(QStringLiteral("SELECT id FROM users ORDER BY id ASC"))) {
                if (error) {
                    *error = query.lastError().text();
                }
                return false;
            }
            while (query.next()) {
                userIds.push_back(query.value(0).toInt());
            }
        }

        struct PileSeed {
            int stationIndex;
            QString code;
            QString type;
            double powerKw;
            int state;
        };
        const QVector<PileSeed> pileSeeds = {
            { 0, QStringLiteral("A-01"), QStringLiteral("快充"), 120.0, 1 },
            { 0, QStringLiteral("A-02"), QStringLiteral("快充"), 90.0, 0 },
            { 0, QStringLiteral("A-03"), QStringLiteral("慢充"), 7.0, 0 },
            { 1, QStringLiteral("B-01"), QStringLiteral("快充"), 80.0, 0 },
            { 1, QStringLiteral("B-02"), QStringLiteral("快充"), 60.0, 2 },
            { 1, QStringLiteral("B-03"), QStringLiteral("慢充"), 11.0, 0 },
            { 2, QStringLiteral("C-01"), QStringLiteral("慢充"), 7.0, 0 },
            { 2, QStringLiteral("C-02"), QStringLiteral("慢充"), 7.0, 1 }
        };
        if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM piles"), {}, error) == 0) {
            for (const auto &seed : pileSeeds) {
                const int stationId = stationRows.value(seed.stationIndex).id;
                if (insertRowId(QStringLiteral(
                    "INSERT INTO piles(station_id, code, type, power_kw, state, charge_count, charge_seconds) "
                    "VALUES(?, ?, ?, ?, ?, 0, 0)"),
                    { stationId, seed.code, seed.type, seed.powerKw, seed.state }, error) < 0) {
                    return false;
                }
            }
        }

        stationRows = stations(error);
        QVector<PileInfo> pileInfos = currentPileInfos(error);
        if (pileInfos.isEmpty()) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("未能读取电桩基础数据");
            }
            return false;
        }

        if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM marketing_strategy"), {}, error) == 0) {
            for (int i = 0; i < std::min(2, static_cast<int>(stationRows.size())); ++i) {
                const auto &station = stationRows.at(i);
                const double discount = i == 0 ? 0.88 : 0.92;
                const QString rule = i == 0
                    ? QStringLiteral("工作日早晚高峰外 8.8 折")
                    : QStringLiteral("夜间 22:00 后 9.2 折");
                if (insertRowId(QStringLiteral(
                    "INSERT INTO marketing_strategy(station_id, base_price, discount, rule_desc, is_active, valid_from, valid_to) "
                    "VALUES(?, ?, ?, ?, 1, datetime('now','localtime'), NULL)"),
                    { station.id, station.basePrice, discount, rule }, error) < 0) {
                    return false;
                }
            }
        }

        if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM pile_health_metrics"), {}, error) == 0) {
            for (const auto &pile : pileInfos) {
                const double avg = pile.powerKw * 0.72;
                if (insertRowId(QStringLiteral(
                    "INSERT INTO pile_health_metrics(pile_id, avg_power, low_threshold, high_threshold, sample_count) "
                    "VALUES(?, ?, ?, ?, 12)"),
                    { pile.id, avg, avg * 0.65, avg * 1.35 }, error) < 0) {
                    return false;
                }
            }
        }

        if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM orders"), {}, error) == 0) {
            QHash<int, int> chargeCountByPile;
            QHash<int, int> chargeSecondsByPile;
            QRandomGenerator rng(42);
            const QDate today = QDate::currentDate();

            for (int dayOffset = 29; dayOffset >= 0; --dayOffset) {
                const int orderCount = (dayOffset % 3 == 0) ? 2 : 1;
                const QDate date = today.addDays(-dayOffset);
                for (int orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
                    const int pileIndex = rng.bounded(pileInfos.size());
                    const auto pile = pileInfos.at(pileIndex);
                    const auto station = std::find_if(stationRows.begin(), stationRows.end(), [&](const StationRow &row) {
                        return row.id == pile.stationId;
                    });
                    const int userId = userIds.at(rng.bounded(userIds.size()));
                    const int state = (dayOffset % 8 == 0) ? 2 : ((dayOffset % 7 == 0 && orderIndex == 0) ? 0 : 1);
                    const QTime startTime(8 + ((dayOffset + orderIndex) % 10), (dayOffset * 7 + orderIndex * 11) % 60);
                    const QDateTime start(date, startTime);
                    QString endText;
                    double kwh = 0.0;
                    double price = 0.0;
                    double amount = 0.0;
                    int durationSec = 0;

                    if (state == static_cast<int>(cp::OrderState::Finished)) {
                        kwh = 12.0 + rng.bounded(36) / 2.0;
                        const double stationBasePrice = (station != stationRows.end()) ? station->basePrice : 1.0;
                        price = stationBasePrice * (pile.type == QStringLiteral("快充") ? 1.18 : 0.92);
                        amount = kwh * price;
                        durationSec = 1800 + rng.bounded(3600);
                        endText = start.addSecs(durationSec).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                        chargeCountByPile[pile.id] += 1;
                        chargeSecondsByPile[pile.id] += durationSec;
                    } else if (state == static_cast<int>(cp::OrderState::Charging)) {
                        price = 0.0;
                    } else {
                        price = 1.0;
                        endText = start.addSecs(300).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    }

                    const int orderId = insertRowId(QStringLiteral(
                        "INSERT INTO orders(user_id, pile_id, station_id, start_time, end_time, kwh, price, amount, state) "
                        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                        { userId, pile.id, pile.stationId, start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                          endText.isEmpty() ? QVariant() : QVariant(endText), kwh, price, amount, state }, error);
                    if (orderId < 0) {
                        return false;
                    }

                    if (state == static_cast<int>(cp::OrderState::Finished) && durationSec > 0) {
                        const double realPower = kwh / (durationSec / 3600.0);
                        if (insertRowId(QStringLiteral(
                            "INSERT INTO pile_power_logs(pile_id, order_id, real_power, logged_at) "
                            "VALUES(?, ?, ?, ?)"),
                            { pile.id, orderId, realPower, endText }, error) < 0) {
                            return false;
                        }
                    }
                }
            }

            for (const auto &pile : pileInfos) {
                QSqlQuery query(m_db);
                query.prepare(QStringLiteral("UPDATE piles SET charge_count = ?, charge_seconds = ? WHERE id = ?"));
                query.addBindValue(chargeCountByPile.value(pile.id, 0));
                query.addBindValue(chargeSecondsByPile.value(pile.id, 0));
                query.addBindValue(pile.id);
                if (!query.exec()) {
                    if (error) {
                        *error = query.lastError().text();
                    }
                    return false;
                }
            }
        }

        return recalculateAllStationStats(error);
    }

    QSqlDatabase m_db;
    bool m_initialized = false;
};

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

namespace pcserver {

bool showAdminLogin(QWidget *parent, QString *userName)
{
    return buildLoginDialog(parent, userName);
}

} // namespace pcserver

struct MainWindow::Private
{
    Ui::MainWindow *ui = nullptr;
    QString adminName;
    pcserver::StationStore *store = nullptr;

    QTabWidget *tabs = nullptr;
    QLabel *adminLabel = nullptr;

    QComboBox *salesRangeCombo = nullptr;
    QLabel *salesTodayValue = nullptr;
    QLabel *salesMonthValue = nullptr;
    QLabel *salesTotalValue = nullptr;
    QChartView *salesChartView = nullptr;
    QTableWidget *ordersTable = nullptr;

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
    d->store = store;
    d->adminName = adminName;
    d->ui = new Ui::MainWindow;
    d->ui->setupUi(this);
    buildUi();
    refreshAll();
    statusBar()->showMessage(QStringLiteral("登录成功：%1").arg(adminName), 5000);
}

MainWindow::~MainWindow()
{
    delete d->ui;
    delete d;
}

void MainWindow::buildUi()
{
    auto *rootLayout = d->ui->verticalLayout;
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);
    clearLayout(rootLayout);
    d->ui->placeholderLabel = nullptr;

    auto *header = new QFrame(d->ui->centralwidget);
    header->setFrameShape(QFrame::StyledPanel);
    header->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #dbe3ea; border-radius: 6px; }"));
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

    auto *statusPage = new QWidget(d->tabs);
    auto *statusLayout = new QVBoxLayout(statusPage);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);

    auto *statusCards = new QGridLayout;
    statusCards->setHorizontalSpacing(10);
    statusCards->setVerticalSpacing(10);
    statusCards->addWidget(createMetricCard(QStringLiteral("电桩总数"), &d->statusTotalValue), 0, 0);
    statusCards->addWidget(createMetricCard(QStringLiteral("闲置"), &d->statusIdleValue), 0, 1);
    statusCards->addWidget(createMetricCard(QStringLiteral("充电中"), &d->statusChargingValue), 0, 2);
    statusCards->addWidget(createMetricCard(QStringLiteral("故障"), &d->statusFaultValue), 0, 3);

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
    manageFormPanel->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #dbe3ea; border-radius: 6px; }"));
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

    manageLayout->addWidget(manageFormPanel, 0);
    manageLayout->addWidget(d->manageTable, 1);
    d->tabs->addTab(managePage, QStringLiteral("充电桩管理"));

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

void MainWindow::refreshAll()
{
    refreshSales();
    refreshPileStatus();
    refreshPileManagement();
    refreshStations();
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
    d->statusSetIdleButton->setEnabled(hasStatusSelection);
    d->statusSetChargingButton->setEnabled(hasStatusSelection);
    d->statusSetFaultButton->setEnabled(hasStatusSelection);
    d->manageUpdateButton->setEnabled(hasManageSelection);
    d->manageDeleteButton->setEnabled(hasManageSelection);
    d->manageRestartButton->setEnabled(hasManageSelection);
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
        d->stationTimer->start();
    }
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
        d->stationTable->setItem(
            row, 6, new QTableWidgetItem(QStringLiteral("%1%").arg(
                                             QString::number(station.onlineRate, 'f', 1))));
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
