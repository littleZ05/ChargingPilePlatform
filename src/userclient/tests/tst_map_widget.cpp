#include <QtTest>
#include <QWebEngineView>
#include "../mappage.h"
class MapWidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void embeddedBrowser() {
        MapPage page;
        auto *browser=page.findChild<QWebEngineView*>("embeddedMap");
        QVERIFY(browser);
        QSignalSpy loaded(browser,&QWebEngineView::loadFinished);
        browser->setHtml("<html><body>Navigation renderer ready</body></html>");
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),15000);
        QVERIFY(loaded.last().first().toBool());
    }
};
QTEST_MAIN(MapWidgetTest)
#include "tst_map_widget.moc"
