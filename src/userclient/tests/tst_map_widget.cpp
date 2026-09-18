#include <QtTest>
#include <QComboBox>
#include <QWebEngineView>
#include "../mappage.h"
class MapWidgetTest : public QObject
{
    Q_OBJECT
private slots:
    void embeddedBrowserWithoutModeSelector() {
        MapPage page;
        auto *browser=page.findChild<QWebEngineView*>("embeddedMap");
        QVERIFY(browser);
        // 导航仅保留驾车：出行方式下拉框已移除，接口不再对外暴露切换入口
        QVERIFY(!page.findChild<QComboBox*>("travelMode"));
        QSignalSpy loaded(browser,&QWebEngineView::loadFinished);
        browser->setHtml("<html><body>Navigation renderer ready</body></html>");
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(),15000);
        QVERIFY(loaded.last().first().toBool());
    }
};
QTEST_MAIN(MapWidgetTest)
#include "tst_map_widget.moc"
