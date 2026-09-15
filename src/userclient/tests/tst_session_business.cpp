#include <QtTest>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include "pcserver_session.h"
#include "net_server.h"
#include "common.h"

class BusinessSessionTest : public QObject
{
    Q_OBJECT
private slots:
    void lostReplyReplayedAfterReconnect();
};
void BusinessSessionTest::lostReplyReplayedAfterReconnect()
{
    QTemporaryDir config;
    QVERIFY(config.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,config.path());
    QCoreApplication::setOrganizationName("SessionTest");
    QCoreApplication::setApplicationName("LostReply");
    QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost,0));
    const quint16 port=probe.serverPort(); probe.close();
    cp::NetServer server; QVERIFY(server.startServer(port));
    int logins=0, attempts=0, effects=0;
    QString requestId;
    connect(&server,&cp::NetServer::packetReceived,this,
            [&](QTcpSocket *socket,quint16 type,const QByteArray &body) {
        const auto request=QJsonDocument::fromJson(body).object();
        QJsonObject response{{"code",0},{"request_id",request.value("request_id")}};
        if (type==10) {
            ++logins;
            response["phone"]="13800138001";
        } else if(type==0) {
            response["message"]="pong";
        } else if(type==30) {
            ++attempts;
            const auto id=request.value("request_id").toString();
            QVERIFY(!id.isEmpty());
            if (requestId.isEmpty()) { requestId=id; ++effects; }
            QCOMPARE(id,requestId);
            if(attempts==1) {
                socket->disconnectFromHost(); // Committed, but ACK never reaches client.
                return;
            }
            response["amount"]=10; response["balance"]=110;
        }
        server.sendPacket(socket,type,QJsonDocument(response).toJson(QJsonDocument::Compact));
    });
    userclient::PcServerSession session;
    session.setServerAddress("127.0.0.1",port);
    session.setReconnectBaseIntervalMs(20);
    session.start();
    QTRY_VERIFY(session.isConnected());
    QSignalSpy login(&session,&userclient::PcServerSession::loginResult);
    QVERIFY(session.login("13800138001"));
    QTRY_COMPARE(login.size(),1);
    QSignalSpy recharge(&session,&userclient::PcServerSession::rechargeResult);
    QVERIFY(session.recharge("13800138001",10));
    QTRY_COMPARE_WITH_TIMEOUT(recharge.size(),1,5000);
    QCOMPARE(attempts,2); QCOMPARE(effects,1); QCOMPARE(logins,2);
    QCOMPARE(recharge.first().at(0).toInt(),0);
    QCOMPARE(recharge.first().at(3).toDouble(),110.0);
    QVERIFY(!QSettings("ChargingPilePlatform","UserClient").contains(QStringLiteral("business/127.0.0.1/%1/13800138001/30").arg(port)));
    session.stop(); server.stopServer();
}
QTEST_GUILESS_MAIN(BusinessSessionTest)
#include "tst_session_business.moc"
