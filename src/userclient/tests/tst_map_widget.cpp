#include <QtTest>
#include <QComboBox>
#include <QWebEngineView>
#include "../mappage.h"
class MapWidgetTest : public QObject
{
    Q_OBJECT
private slots:
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
