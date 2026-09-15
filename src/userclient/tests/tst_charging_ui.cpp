#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QScopeGuard>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include "../mainwindow.h"
#include "../user_theme.h"
#include "../stationdetailpage.h"
#include "../pcserver_session.h"

class ChargingUiTest : public QObject
{
    Q_OBJECT
private slots:
    void restartInsufficientBalanceRechargeAndDashboard();
};
void ChargingUiTest::restartInsufficientBalanceRechargeAndDashboard()
{
    applyUserTheme(*qApp);
    QTemporaryDir temp; QVERIFY(temp.isValid());
    const auto saveLog=qScopeGuard([&]{QFile::remove("charging-server.log");QFile::copy(temp.filePath("server.log"),"charging-server.log");});
    QTcpServer socketProbe, httpProbe;
    QVERIFY(socketProbe.listen(QHostAddress::LocalHost,0));
    QVERIFY(httpProbe.listen(QHostAddress::LocalHost,0));
    const auto port=socketProbe.serverPort(), http=httpProbe.serverPort();
    socketProbe.close(); httpProbe.close();
    qputenv("PCSERVER_PORT",QByteArray::number(port));
    auto clearEnvironment=qScopeGuard([]{qunsetenv("PCSERVER_PORT");});
    QProcess server;
    auto environment=QProcessEnvironment::systemEnvironment();
    environment.insert("PCSERVER_DB_PATH",temp.filePath("ui.db"));
    environment.insert("PCSERVER_PORT",QString::number(port));
    environment.insert("PCSERVER_DASH_PORT",QString::number(http));
    environment.insert("PCSERVER_AUTOLOGIN","1");
    environment.insert("QT_QPA_PLATFORM","offscreen");
    environment.remove("LD_PRELOAD");
    server.setProcessEnvironment(environment);
    server.setStandardOutputFile(temp.filePath("server.log"));
    server.setStandardErrorFile(temp.filePath("server.log"),QIODevice::Append);
    const QString executable=QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../src__pcserver__pcserver/PcServer");
    QVERIFY2(QFileInfo::exists(executable),"Build PcServer with tools/verify.py first");
    server.start(executable); QVERIFY(server.waitForStarted());
    auto stop=qScopeGuard([&]{server.terminate(); if(!server.waitForFinished(3000)) {server.kill();server.waitForFinished();}});
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(temp.filePath("ui.db")),5000);
    QSqlDatabase db=QSqlDatabase::addDatabase("QSQLITE","charging-ui-check");
    db.setDatabaseName(temp.filePath("ui.db")); QVERIFY(db.open());
    auto cleanupDb=qScopeGuard([&]{db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase("charging-ui-check");});
    auto scalar=[&](const QString &sql) {
        QSqlQuery query(db);
        if(!query.exec(sql)||!query.next())return -1.0;
        return query.value(0).toDouble();
    };
    QTRY_VERIFY_WITH_TIMEOUT(scalar("SELECT COUNT(*) FROM piles")>0,5000);
    QTimer dismiss;
    connect(&dismiss,&QTimer::timeout,[]{
        for(auto *widget:QApplication::topLevelWidgets())
            if(auto *message=qobject_cast<QMessageBox*>(widget))message->accept();
    });
    dismiss.start(20);
    {
        MainWindow window; window.show();
        auto *session=window.serverSession();
        connect(session,&userclient::PcServerSession::businessResult,[](int type,const QJsonObject &r){
            qInfo() << "UI business" << type << r.value("code") << r.value("message");
        });
        QTRY_VERIFY_WITH_TIMEOUT(session->isConnected(),5000);
        QSignalSpy login(session,&userclient::PcServerSession::loginResult);
        QVERIFY(QMetaObject::invokeMethod(&window,"showMain",Q_ARG(QString,QStringLiteral("13800138001"))));
        QTRY_COMPARE(login.size(),1);
        auto *page=window.findChild<StationDetailPage*>(); QVERIFY(page);
        Station station; station.id=1; station.name="Test station";
        station.piles={{"S001-P01",QStringLiteral("快充"),120,QStringLiteral("空闲")}};
        page->setStation(station);
        QPushButton *start=nullptr;
        for(auto *button:page->findChildren<QPushButton*>())
            if(button->text()==QStringLiteral("预约充电"))start=button;
        QVERIFY(start); start->click();
        QTRY_VERIFY_WITH_TIMEOUT(page->hasActiveOrder(),5000);
        Station other=station; other.id=2; page->setStation(other);
        QVERIFY(page->hasActiveOrder()); // Switching station cannot discard the active order.
        QTest::qWait(1200);
    }
    QCOMPARE(scalar("SELECT COUNT(*) FROM orders WHERE state=0"),1.0);
    {
        MainWindow window; window.show();
        auto *session=window.serverSession();
        connect(session,&userclient::PcServerSession::businessResult,[](int type,const QJsonObject &r){
            qInfo() << "UI business" << type << r.value("code") << r.value("message");
        });
        QTRY_VERIFY_WITH_TIMEOUT(session->isConnected(),5000);
        QVERIFY(QMetaObject::invokeMethod(&window,"showMain",Q_ARG(QString,QStringLiteral("13800138001"))));
        auto *page=window.findChild<StationDetailPage*>();
        QTRY_VERIFY_WITH_TIMEOUT(page->hasActiveOrder(),5000);
        {QSqlQuery q(db); QVERIFY(q.exec("UPDATE users SET balance=0 WHERE phone='13800138001'"));}
        QSignalSpy results(session,&userclient::PcServerSession::businessResult);
        QVERIFY(QMetaObject::invokeMethod(page,"endCharging"));
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty(),5000);
        QCOMPARE(scalar("SELECT state FROM orders"),0.0);
        QVERIFY(page->hasActiveOrder());
        QSignalSpy recharge(session,&userclient::PcServerSession::rechargeResult);
        QVERIFY(session->recharge("13800138001",10));
        QTRY_COMPARE(recharge.size(),1);
        QVERIFY(QMetaObject::invokeMethod(page,"endCharging"));
        QTRY_VERIFY_WITH_TIMEOUT(!page->hasActiveOrder(),5000);
        QCOMPARE(scalar("SELECT state FROM orders"),1.0);
        bool confirmed=false;
        for(auto *label:page->findChildren<QLabel*>())
            confirmed=confirmed||label->text().contains(QStringLiteral("已结算"));
        QVERIFY(confirmed);
        QVERIFY(window.grab().save("charging-settled.png"));
    }
    const double amount=scalar("SELECT amount FROM orders");
    QVERIFY(amount>0);
    QVERIFY(qAbs(scalar("SELECT balance FROM users WHERE phone='13800138001'")-(10-amount))<1e-6);
    QNetworkAccessManager manager;
    auto *reply=manager.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/api/dashboard/overview").arg(http))));
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(),5000);
    QCOMPARE(reply->error(),QNetworkReply::NoError);
    const auto response=QJsonDocument::fromJson(reply->readAll()).object();
    QVERIFY(!response.isEmpty());
    reply->deleteLater();
    // Actual DB and dashboard snapshot amounts are checked after the UI confirms settlement.
    const auto data=response.value("data").toObject();
    QCOMPARE(data.value("kpi").toObject().value("today_revenue_yuan").toDouble(-1),amount);
}
QTEST_MAIN(ChargingUiTest)
#include "tst_charging_ui.moc"
