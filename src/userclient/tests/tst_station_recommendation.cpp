#include <QtTest>
#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include "../stationpage.h"
class RecommendationTest : public QObject
{
    Q_OBJECT
private slots:
    void sortModesAndEmptyServerData() {
        StationPage page;
        userclient::ServerStation near, far, unknown;
        near.id=1; near.name="Near"; near.latitude=kUserLat; near.longitude=kUserLng;
        near.predictedIdleRate=20; near.predictedIdlePiles=2;
        far=near; far.id=2; far.name="Far"; far.latitude+=0.1; far.predictedIdleRate=90;
        unknown=near; unknown.id=3; unknown.name="Unknown"; unknown.latitude+=0.2;
        unknown.predictedIdleRate=-1;
        page.applyServerStations({far,unknown,near});
        auto *list=page.findChild<QListWidget*>("stationList");
        auto *mode=page.findChild<QComboBox*>("stationSortMode");
        QVERIFY(list); QVERIFY(mode); QCOMPARE(list->count(),3);
        auto firstContains=[&](const QString &name) {
            auto *widget=list->itemWidget(list->item(0));
            for(auto *label:widget->findChildren<QLabel*>())
                if(label->text()==name) return true;
            return false;
        };
        QVERIFY(firstContains("Near"));
        mode->setCurrentIndex(1); QVERIFY(firstContains("Far"));
        mode->setCurrentIndex(0); QVERIFY(firstContains("Near"));
        page.applyServerStations({}); QCOMPARE(list->count(),0);
    }
};
QTEST_MAIN(RecommendationTest)
#include "tst_station_recommendation.moc"
