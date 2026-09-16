#include <QtTest>
#include <QUrlQuery>
#include <limits>
#include "../map_routes.h"
class MapRoutesTest : public QObject
{
    Q_OBJECT
private slots:
    void routeModesAndCoordinates();
    void deltaPolyline();
    void rejectsInvalidData();
};
void MapRoutesTest::routeModesAndCoordinates()
{
    using namespace userclient;
    const QString name=QStringLiteral("东软 & 北门");
    auto url=routePlanUrl(39.9,116.3,name,40.1,116.5,QStringLiteral("目标站"));
    QCOMPARE(url.scheme(),QStringLiteral("https"));
    QUrlQuery query(QUrl::fromEncoded(url.toEncoded()));
    QCOMPARE(query.queryItemValue("from"),name);
    QCOMPARE(query.queryItemValue("fromcoord"),QStringLiteral("39.900000,116.300000"));
    QCOMPARE(query.queryItemValue("tocoord"),QStringLiteral("40.100000,116.500000"));
    QCOMPARE(query.queryItemValue("type"),QStringLiteral("drive"));
}
void MapRoutesTest::deltaPolyline()
{
    auto points=userclient::decodeTencentPolyline({39.9,116.3,10000,-20000,-5000,3000});
    QCOMPARE(points.size(),3);
    QVERIFY(qAbs(points[1].first-39.91)<1e-8);
    QVERIFY(qAbs(points[1].second-116.28)<1e-8);
    QVERIFY(qAbs(points[2].first-39.905)<1e-8);
    QVERIFY(qAbs(points[2].second-116.283)<1e-8);
}
void MapRoutesTest::rejectsInvalidData()
{
    using namespace userclient;
    QVERIFY(!validCoordinates(std::numeric_limits<double>::quiet_NaN(),1));
    QVERIFY(routePlanUrl(91,0,"a",0,0,"b").isEmpty());
    QVERIFY(decodeTencentPolyline({39,116,1}).isEmpty());
    QVERIFY(decodeTencentPolyline({39,116,"bad",1}).isEmpty());
    QVERIFY(decodeTencentPolyline({39,116,1000000000,1}).isEmpty());
}
QTEST_GUILESS_MAIN(MapRoutesTest)
#include "tst_map_routes.moc"
