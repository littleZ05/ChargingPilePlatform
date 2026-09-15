#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include <memory>

QVariant evaluate(QWebEnginePage *page,const QString &script)
{
    auto value=std::make_shared<QVariant>();
    QEventLoop loop;
    QPointer<QEventLoop> guard(&loop);
    page->runJavaScript(script,[guard,value](const QVariant &result){
        *value=result;
        if(guard)guard->quit();
    });
    QTimer::singleShot(4000,&loop,&QEventLoop::quit);
    loop.exec();
    return *value;
}
class DashboardBrowserTest : public QObject
{
    Q_OBJECT
private slots:
    void liveOfflineRecoveryAndZeroData();
};
void DashboardBrowserTest::liveOfflineRecoveryAndZeroData()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost,0));
    const quint16 port=server.serverPort();
    int revenue=42, total=10;
    connect(&server,&QTcpServer::newConnection,[&]{
        auto *socket=server.nextPendingConnection();
        connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        connect(socket,&QTcpSocket::readyRead,socket,[&,socket]{
            socket->readAll();
            QJsonObject kpi{{"total_piles",total},{"idle_piles",total},{"charging_piles",0},
                {"fault_piles",0},{"today_revenue_yuan",revenue},{"forecast_horizon",24}};
            QJsonObject data{{"kpi",kpi},{"meta",QJsonObject{{"load_demo_fallback",false}}},
                {"forecast",QJsonObject{{"horizon",24},{"kw",QJsonArray{0,0}}}}};
            const QByteArray body=QJsonDocument(QJsonObject{{"code",0},{"data",data}}).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
            socket->disconnectFromHost();
        });
    });
    QWebEngineView view;
    view.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,true);
    view.resize(1440,1000); view.show();
    const QString path=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../src/webdashboard/index.html");
    QVERIFY(QFileInfo::exists(path));
    QUrl url=QUrl::fromLocalFile(path);
    QUrlQuery query;
    query.addQueryItem("api",QStringLiteral("http://127.0.0.1:%1/api/dashboard/overview").arg(port));
    url.setQuery(query);
    QSignalSpy loaded(&view,&QWebEngineView::loadFinished);
    view.load(url);
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),15000);
    QVERIFY(loaded.last().first().toBool());
    const QString source="document.getElementById('dataSource').textContent";
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),source).toString().contains(QStringLiteral("SQLite 实时聚合")),15000);
    QCOMPARE(evaluate(view.page(),"document.getElementById('kpiRevenueValue').getAttribute('data-count')").toString(),QStringLiteral("42"));
    QVERIFY(evaluate(view.page(),"document.querySelectorAll('canvas').length").toInt()>=3);
    QCOMPARE(evaluate(view.page(),"document.getElementById('forecastKpiTitle').textContent").toString(),QStringLiteral("未来 24h 预测峰值"));
    QTest::qWait(1000);
    QVERIFY(view.grab().save("dashboard-live.png"));
    server.close();
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),source).toString().contains(QStringLiteral("连接中断")),15000);
    QCOMPARE(evaluate(view.page(),"document.getElementById('kpiRevenueValue').getAttribute('data-count')").toString(),QStringLiteral("42"));
    revenue=0; total=0;
    QVERIFY(server.listen(QHostAddress::LocalHost,port));
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),source).toString().contains(QStringLiteral("SQLite 实时聚合")),15000);
    QCOMPARE(evaluate(view.page(),"document.getElementById('kpiRevenueValue').getAttribute('data-count')").toString(),QStringLiteral("0"));
    QCOMPARE(evaluate(view.page(),"document.getElementById('kpiTotalValue').getAttribute('data-count')").toString(),QStringLiteral("0"));
    view.resize(1024,768);
    QTest::qWait(300);
    QVERIFY(evaluate(view.page(),"document.querySelector('.page.active .chart-host').getBoundingClientRect().height").toDouble()>=300);
    QVERIFY(evaluate(view.page(),"document.documentElement.scrollHeight > window.innerHeight").toBool());
    evaluate(view.page(),"window.scrollTo(0,document.documentElement.scrollHeight)");
    QTest::qWait(300);
    QVERIFY(view.grab().save("dashboard-zero-1024.png"));
}
int main(int argc,char **argv)
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS","--disable-gpu");
    qputenv("QT_QUICK_BACKEND","software");
    QApplication application(argc,argv);
    DashboardBrowserTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "tst_dashboard_browser.moc"
