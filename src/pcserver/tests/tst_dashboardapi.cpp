#include <QtTest/QtTest>

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>

#include <cmath>

#include "../dashboard_api.h"
#include "../stationstore.h"
#include "test_dbpath.h"

using namespace pcserver;

/** NO.16 Web 大屏：平台级聚合口径 + HTTP/JSON 服务测试 */
class TstDashboardApi : public QObject
{
    Q_OBJECT

private slots:
    void snapshotMatchesSeedPilesAndUsesLoadFallback();
    void snapshotAggregatesTodayAndSevenDayOrders();
    void platformLoadPrefersRealLogsWhenSamplesEnough();
    void dashboardApiServesOverviewWithCors();
    void dashboardApiHealthAndNotFoundPaths();
    void dashboardApiRefusesStartWithoutOpenStore();
};

namespace {

bool near(double a, double b, double eps = 1e-6)
{
    return qAbs(a - b) <= eps;
}

int freeLocalPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
}

struct HttpResponse
{
    int status = 0;
    QByteArray headers;
    QByteArray body;
};

HttpResponse httpGet(int port, const QByteArray &path)
{
    HttpResponse response;
    if (port <= 0)
        return response;

    QTcpSocket socket;
    QByteArray raw;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&]() {
        raw += socket.readAll();
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&]() {
        socket.write("GET " + path + " HTTP/1.1\r\n"
                     "Host: 127.0.0.1\r\n"
                     "Connection: close\r\n\r\n");
        socket.flush();
    });

    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(port));
    timeout.start();
    loop.exec();
    if (socket.state() != QAbstractSocket::UnconnectedState)
        return response;
    raw += socket.readAll();

    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return response;
    response.headers = raw.left(headerEnd);
    response.body = raw.mid(headerEnd + 4);
    const QList<QByteArray> headLines = response.headers.split('\r');
    const QList<QByteArray> statusParts =
        headLines.value(0).trimmed().split(' ');
    response.status = statusParts.value(1).toInt();
    return response;
}

} // namespace

void TstDashboardApi::snapshotMatchesSeedPilesAndUsesLoadFallback()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("dash_seed.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    DashboardSnapshot snap;
    QVERIFY2(store.dashboardSnapshot(&snap, 6, &error), qPrintable(error));

    // 桩状态总量与种子数据一致（全部电站合计）
    int total = 0;
    int idle = 0;
    int charging = 0;
    int fault = 0;
    for (const StationInfo &station : store.listStations()) {
        total += station.totalPiles;
        for (const PileInfo &pile : store.listPiles(station.id)) {
            if (pile.state == cp::PileState::Idle)
                ++idle;
            else if (pile.state == cp::PileState::Charging)
                ++charging;
            else
                ++fault;
        }
    }
    QCOMPARE(snap.totalPiles, total);
    QCOMPARE(snap.idlePiles, idle);
    QCOMPARE(snap.chargingPiles, charging);
    QCOMPARE(snap.faultPiles, fault);
    QVERIFY(snap.totalPiles > 0);
    QVERIFY(snap.chargingPiles > 0);   // 种子含充电中桩 → 实时总负荷 > 0
    QVERIFY(snap.totalCapacityKw > 0.0);
    QVERIFY(snap.currentLoadKw > 0.0);
    QVERIFY(near(snap.onlineRate,
                 100.0 * (total - fault) / total, 0.05));

    // 无功率日志 → 24h 负荷走仿真兜底，但形状完整、值合法
    QVERIFY(snap.loadUsedDemoFallback);
    QCOMPARE(snap.load24hKw.size(), 24);
    for (double v : snap.load24hKw) {
        QVERIFY(std::isfinite(v));
        QVERIFY(v >= 0.0);
        QVERIFY(v <= snap.totalCapacityKw + 1e-6);
    }

    // NO.17 预测在兜底曲线上可正常产出（24 点 ≥ 3 样本）
    QVERIFY(snap.forecastOk);
    QCOMPARE(snap.forecastKw.size(), 6);
    QCOMPARE(snap.forecastHorizon, 6);
    QVERIFY(!snap.forecastModelName.isEmpty());
    QVERIFY(!snap.forecastTrendText.isEmpty());
    QVERIFY(snap.forecastPeakKw >= 0.0);
    QVERIFY(snap.forecastPeakHour >= 1 && snap.forecastPeakHour <= 6);

    // 无订单 → 7 日营收/订单为 0
    QCOMPARE(snap.revenue7d.size(), 7);
    QCOMPARE(snap.order7d.size(), 7);
    QCOMPARE(snap.todayOrders, 0);
    QVERIFY(near(snap.todayRevenueYuan, 0.0));
}

void TstDashboardApi::snapshotAggregatesTodayAndSevenDayOrders()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("dash_orders.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    const auto stations = store.listStations();
    QVERIFY(!stations.isEmpty());
    const auto piles = store.listPiles(stations.first().id);
    QVERIFY(piles.size() >= 3);

    QSqlDatabase db = QSqlDatabase::database(store.connectionName());
    QSqlQuery q(db);
    const QStringList setup = {
        QStringLiteral("INSERT INTO users(id,phone,nickname,balance,status) "
                       "VALUES(1,'13800000001','大屏测试用户',100,0)"),
        QStringLiteral("INSERT INTO orders(user_id,pile_id,station_id,start_time,end_time,"
                       "kwh,price,amount,state) "
                       "VALUES(1,%1,%2,datetime('now','localtime','-40 minutes'),"
                       "datetime('now','localtime','-5 minutes'),12.5,1.2,15.00,1)")
            .arg(piles.at(0).id).arg(stations.first().id),
        QStringLiteral("INSERT INTO orders(user_id,pile_id,station_id,start_time,end_time,"
                       "kwh,price,amount,state) "
                       "VALUES(1,%1,%2,datetime('now','localtime','-1 day','-40 minutes'),"
                       "datetime('now','localtime','-1 day','-5 minutes'),10,1.1,11.00,1)")
            .arg(piles.at(1).id).arg(stations.first().id),
        QStringLiteral("INSERT INTO orders(user_id,pile_id,station_id,start_time,end_time,"
                       "kwh,price,amount,state) "
                       "VALUES(1,%1,%2,datetime('now','localtime','-6 days','-40 minutes'),"
                       "datetime('now','localtime','-6 days','-5 minutes'),20,1.0,20.00,1)")
            .arg(piles.at(2).id).arg(stations.first().id),
        // 未完成/已取消订单不参与营收统计
        QStringLiteral("INSERT INTO orders(user_id,pile_id,station_id,start_time,end_time,"
                       "kwh,price,amount,state) "
                       "VALUES(1,%1,%2,datetime('now','localtime','-20 minutes'),NULL,"
                       "5,1.2,0,0)")
            .arg(piles.at(0).id).arg(stations.first().id)
    };
    for (const QString &statement : setup) {
        QVERIFY2(q.exec(statement), q.lastError().text().toUtf8());
    }

    DashboardSnapshot snap;
    QVERIFY2(store.dashboardSnapshot(&snap, 6, &error), qPrintable(error));

    QCOMPARE(snap.todayOrders, 1);
    QVERIFY(near(snap.todayRevenueYuan, 15.00));
    QVERIFY(near(snap.todayEnergyKwh, 12.5));
    QCOMPARE(snap.revenue7d.size(), 7);
    QCOMPARE(snap.order7d.size(), 7);
    QCOMPARE(snap.revenue7d.at(5), 11.00);  // 昨天
    QCOMPARE(snap.order7d.at(5), 1);
    QCOMPARE(snap.revenue7d.at(0), 20.00);  // 6 天前
    QCOMPARE(snap.order7d.at(0), 1);
    QCOMPARE(snap.revenue7d.at(6), 15.00);  // 今天
    QCOMPARE(snap.order7d.at(6), 1);
}

void TstDashboardApi::platformLoadPrefersRealLogsWhenSamplesEnough()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("dash_real.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    // 近 9 个整点各写一条全平台功率日志（24h 窗口需 ≥8 个有效桶）
    const auto stations = store.listStations();
    const auto piles0 = store.listPiles(stations.at(0).id);
    const auto piles1 = store.listPiles(stations.at(1).id);
    QVERIFY(!piles0.isEmpty() && !piles1.isEmpty());

    const QDateTime now = QDateTime::currentDateTime();
    for (int i = 1; i <= 9; ++i) {
        const QDateTime when = now.addSecs(-i * 3600);
        const double power = 40.0 + i * 10.0;   // 50..130
        const int pileId = (i % 2 == 0) ? piles0.first().id : piles1.first().id;
        const QVariantList binds = {
            pileId, power, when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        };
        QVERIFY2(store.execPrepared(
                     QStringLiteral(
                         "INSERT INTO pile_power_logs(pile_id, real_power, logged_at) "
                         "VALUES(?, ?, ?)"),
                     binds, &error),
                 qPrintable(error));
    }

    DashboardSnapshot snap;
    QVERIFY2(store.dashboardSnapshot(&snap, 6, &error), qPrintable(error));
    QVERIFY(!snap.loadUsedDemoFallback);   // 有效样本足够 → 真实聚合优先
    QCOMPARE(snap.load24hKw.size(), 24);

    int nonZero = 0;
    for (double v : snap.load24hKw) {
        QVERIFY(std::isfinite(v));
        QVERIFY(v >= 0.0);
        if (v > 0.0)
            ++nonZero;
    }
    QVERIFY(nonZero >= 9);
    QVERIFY(snap.forecastOk);
}

void TstDashboardApi::dashboardApiServesOverviewWithCors()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("dash_api.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    DashboardApiServer server(&store);
    const int port = freeLocalPort();
    QVERIFY(port > 0);
    QVERIFY2(server.start(port, &error), qPrintable(error));
    QVERIFY(server.isListening());
    QCOMPARE(server.port(), port);

    const HttpResponse response =
        httpGet(port, QByteArrayLiteral("/api/dashboard/overview"));
    QCOMPARE(response.status, 200);
    QVERIFY(response.headers.contains("Access-Control-Allow-Origin: *"));
    QVERIFY(response.headers.contains("Cache-Control: no-store"));

    const QJsonObject root = QJsonDocument::fromJson(response.body).object();
    QCOMPARE(root.value(QStringLiteral("code")).toInt(), 0);
    QVERIFY(root.contains(QStringLiteral("ts")));
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    const QJsonObject kpi = data.value(QStringLiteral("kpi")).toObject();
    QVERIFY(kpi.value(QStringLiteral("total_piles")).toInt() > 0);
    QVERIFY(kpi.contains(QStringLiteral("current_load_kw")));
    QVERIFY(kpi.contains(QStringLiteral("today_revenue_yuan")));
    QCOMPARE(data.value(QStringLiteral("pile_states")).toArray().size(), 3);
    QCOMPARE(data.value(QStringLiteral("load24h_kw")).toArray().size(), 24);
    QCOMPARE(data.value(QStringLiteral("revenue7d")).toArray().size(), 7);
    QCOMPARE(data.value(QStringLiteral("order7d")).toArray().size(), 7);
    const QJsonObject forecast = data.value(QStringLiteral("forecast")).toObject();
    QCOMPARE(forecast.value(QStringLiteral("kw")).toArray().size(), 6);
    QCOMPARE(data.value(QStringLiteral("days")).toArray().size(), 7);
    server.stop();
    QVERIFY(!server.isListening());
}

void TstDashboardApi::dashboardApiHealthAndNotFoundPaths()
{
    QString error;
    StationStore store;
    const QString dbPath = makeTestDatabasePath(QStringLiteral("dash_api2.db"));
    QVERIFY2(!dbPath.isEmpty(), "无法创建测试数据库目录");
    QVERIFY2(store.open(dbPath, &error), qPrintable(error));

    DashboardApiServer server(&store);
    const int port = freeLocalPort();
    QVERIFY(port > 0);
    QVERIFY(server.start(port, &error));

    const HttpResponse health =
        httpGet(port, QByteArrayLiteral("/api/dashboard/health"));
    QCOMPARE(health.status, 200);
    QJsonObject root = QJsonDocument::fromJson(health.body).object();
    QCOMPARE(root.value(QStringLiteral("code")).toInt(), 0);
    QVERIFY(root.value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("store_open")).toBool());

    const HttpResponse notFound =
        httpGet(port, QByteArrayLiteral("/api/nope"));
    QCOMPARE(notFound.status, 404);
    root = QJsonDocument::fromJson(notFound.body).object();
    QCOMPARE(root.value(QStringLiteral("code")).toInt(), 404);
    server.stop();
}

void TstDashboardApi::dashboardApiRefusesStartWithoutOpenStore()
{
    StationStore closedStore;
    DashboardApiServer server(&closedStore);
    QString error;
    QVERIFY(!server.start(8890, &error));
    QVERIFY(error.contains(QStringLiteral("数据库未就绪")));
    QVERIFY(!server.isListening());
}

QTEST_MAIN(TstDashboardApi)

#include "tst_dashboardapi.moc"
