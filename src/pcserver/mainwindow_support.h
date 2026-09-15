#ifndef PCSERVER_MAINWINDOW_SUPPORT_H
#define PCSERVER_MAINWINDOW_SUPPORT_H
#include "mainwindow.h"
#include "admin_repository.h"
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

namespace pcserver_ui {
using namespace pcserver_admin;
QString pileStateText(int state);
QString orderStateText(int state);
QString userStatusText(int status);
QColor pileStateColor(int state);
QColor orderStateColor(int state);
QColor onlineRateColor(double rate);
void tintStatusItem(QTableWidgetItem *item, const QColor &color);
QString durationText(int seconds);
QString moneyText(double value);
QString socketServerTimeText();
QJsonObject socketResponseEnvelope(int code, const QString &message);
bool parseSocketJsonObject(const QByteArray &body, QJsonObject *out);
QByteArray socketJsonCompact(const QJsonObject &object);
void clearLayout(QLayout *layout);
QFrame *createMetricCard(const QString &title, QLabel **valueLabel,
                         const QString &metricTone = QString());
QTableWidgetItem *makeItem(const QString &text, const QVariant &userData = QVariant());
int rowId(const QTableWidget *table, int row);
int findRowById(const QTableWidget *table, int id);
void configurePileTable(QTableWidget *table);
void configureOrdersTable(QTableWidget *table);
void configureUserTable(QTableWidget *table);
void fillUserTable(QTableWidget *table, const QVector<UserRow> &rows);
void fillPileTable(QTableWidget *table, const QVector<PileRow> &rows);
void fillOrdersTable(QTableWidget *table, const QVector<OrderRow> &rows);
void fillStationCombo(QComboBox *combo, const QVector<StationRow> &stations);
void setChartTheme(QChart *chart);
void setChartAxisTheme(QAbstractAxis *axis);
void fillRevenueChart(QChartView *view, const QVector<RevenuePoint> &points, int days);
QDateTime currentHourAnchor();
void fillLoadForecastChart(QChartView *view,
                           const QString &stationName,
                           const QDateTime &anchorHour,
                           const QVector<double> &history,
                           const cp::LoadForecastResult &result);
bool buildLoginDialog(QWidget *parent, QString *userName);
}
#endif
