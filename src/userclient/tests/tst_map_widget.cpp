#include <QtTest>
#include <QComboBox>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QLabel>
#include <QUrlQuery>
#include "../mappage.h"
class MapWidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void onlineRoutes() {
        if (qEnvironmentVariableIntValue("RUN_ONLINE_MAP_TESTS") != 1)
            QSKIP("Set RUN_ONLINE_MAP_TESTS=1 to explicitly consume online API quota");
        if (qEnvironmentVariableIsEmpty("TENCENT_MAP_KEY"))
            QSKIP("Online acceptance requires TENCENT_MAP_KEY");
        MapPage page;
        page.resize(600,850);
        page.show();
        auto *mode=page.findChild<QComboBox*>("travelMode");
        auto *browser=page.findChild<QWebEngineView*>("embeddedMap");
        Station target;
        target.name=QStringLiteral("中关村软件园");
        target.latitude=40.047; target.longitude=116.297;
        gUserLocation.lat=40.040; gUserLocation.lng=116.300;
        gUserLocation.label=QStringLiteral("当前位置");
        const auto routeReady=[&] {
            for(auto *label:page.findChildren<QLabel*>()) {
                const auto text=label->text();
                if(text.contains(QStringLiteral("途经"))
                   && text.startsWith(mode->currentText())) return true;
            }
            return false;
        };
        page.setRoute(target);
        QTRY_VERIFY_WITH_TIMEOUT(routeReady(),20000);
        auto *map=page.findChild<QLabel*>("mapImage");
        QTRY_VERIFY_WITH_TIMEOUT(!map->pixmap(Qt::ReturnByValue).isNull(),20000);
        QVERIFY(page.grab().save("online-driving.png"));
        mode->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(routeReady(),20000);
        QVERIFY(page.grab().save("online-walking.png"));
        QSignalSpy loaded(browser,&QWebEngineView::loadFinished);
        QVERIFY(QMetaObject::invokeMethod(&page,"openMap"));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),30000);
        QVERIFY2(loaded.last().first().toBool(),"Tencent embedded route page failed to load");
        QTest::qWait(3000);
        QVERIFY(page.grab().save("online-embedded.png"));
    }
    void embeddedBrowserAndModeSelector() {
        MapPage page;
        auto *mode=page.findChild<QComboBox*>("travelMode");
        auto *browser=page.findChild<QWebEngineView*>("embeddedMap");
        QVERIFY(mode); QVERIFY(browser);
        QCOMPARE(mode->count(),2);
        QCOMPARE(mode->itemText(0),QStringLiteral("驾车"));
        QCOMPARE(mode->itemText(1),QStringLiteral("步行"));
        QSignalSpy loaded(browser,&QWebEngineView::loadFinished);
        browser->setHtml("<html><body>Navigation renderer ready</body></html>");
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),15000);
        QVERIFY(loaded.last().first().toBool());
        mode->setCurrentIndex(1);
        QCOMPARE(mode->currentText(),QStringLiteral("步行"));
    }
};
QTEST_MAIN(MapWidgetTest)
#include "tst_map_widget.moc"
