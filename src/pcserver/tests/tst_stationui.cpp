#include <QtTest/QtTest>

#include <QLabel>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>

#include "../mainwindow.h"
#include "../stationstore.h"

using namespace pcserver;

class TstStationUi : public QObject
{
    Q_OBJECT

private slots:
    void stationListHeadersMatchRequirement();
    void stationListShowsSeededRows();
    void stationListRefreshesAfterStoreInsert();
    void selectingStationRowShowsItsPiles();
    void switchingStationRowSwitchesPileDetail();
};

void TstStationUi::stationListHeadersMatchRequirement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    StationStore store;
    QVERIFY2(store.open(dir.filePath(QStringLiteral("ui.db")), &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    MainWindow window(&store);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QVERIFY(table);

    QCOMPARE(table->columnCount(), 7);
    const QStringList expected = {
        QStringLiteral("站ID"), QStringLiteral("站名"), QStringLiteral("地址"),
        QStringLiteral("经度"), QStringLiteral("纬度"),
        QStringLiteral("总电桩数"), QStringLiteral("当前在线率")
    };
    for (int col = 0; col < expected.size(); ++col)
        QCOMPARE(table->horizontalHeaderItem(col)->text(), expected.at(col));
}

void TstStationUi::stationListShowsSeededRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    StationStore store;
    QVERIFY2(store.open(dir.filePath(QStringLiteral("ui2.db")), &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    MainWindow window(&store);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QVERIFY(table);

    const auto stations = store.listStations();
    QCOMPARE(table->rowCount(), stations.size());
    QVERIFY(stations.size() > 0);

    // 抽查第一行与数据层一致：ID/站名/地址/总桩数/在线率
    const StationInfo &first = stations.at(0);
    QCOMPARE(table->item(0, 0)->text(), QString::number(first.id));
    QCOMPARE(table->item(0, 1)->text(), first.name);
    QCOMPARE(table->item(0, 2)->text(), first.address);
    QCOMPARE(table->item(0, 5)->text(), QString::number(first.totalPiles));
    QCOMPARE(table->item(0, 6)->text(),
             QStringLiteral("%1%").arg(QString::number(first.onlineRate, 'f', 1)));
}

void TstStationUi::stationListRefreshesAfterStoreInsert()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    StationStore store;
    QVERIFY2(store.open(dir.filePath(QStringLiteral("ui3.db")), &error), qPrintable(error));

    MainWindow window(&store);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 0);

    const int newId = store.addStation(
        QStringLiteral("新增测试站"), QStringLiteral("沈阳市新增路 1 号"),
        123.300000, 41.900000, 5, &error);
    QVERIFY(newId > 0);

    window.refreshStations();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 0)->text(), QString::number(newId));
    QCOMPARE(table->item(0, 5)->text(), QStringLiteral("5"));
}

void TstStationUi::selectingStationRowShowsItsPiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    StationStore store;
    QVERIFY2(store.open(dir.filePath(QStringLiteral("ui4.db")), &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    MainWindow window(&store);
    auto *stationTable = window.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    auto *pileTable = window.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    auto *hintLabel = window.findChild<QLabel *>(QStringLiteral("pileHintLabel"));
    QVERIFY(stationTable && pileTable && hintLabel);

    const auto stations = store.listStations();
    QVERIFY(stations.size() >= 2);
    const auto pilesOfFirst = store.listPiles(stations.at(0).id);
    QVERIFY(pilesOfFirst.size() > 0);

    stationTable->selectRow(0);
    QCOMPARE(pileTable->rowCount(), pilesOfFirst.size());

    // 抽查第一根桩的关键字段
    QCOMPARE(pileTable->item(0, 1)->text(), pilesOfFirst.at(0).code);
    QCOMPARE(pileTable->item(0, 2)->text(), pilesOfFirst.at(0).type);
    QCOMPARE(pileTable->item(0, 4)->text(),
             StationStore::pileStateText(pilesOfFirst.at(0).state));
    QVERIFY(hintLabel->text().contains(QString::number(stations.at(0).id)));
}

void TstStationUi::switchingStationRowSwitchesPileDetail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    StationStore store;
    QVERIFY2(store.open(dir.filePath(QStringLiteral("ui5.db")), &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    MainWindow window(&store);
    auto *stationTable = window.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    auto *pileTable = window.findChild<QTableWidget *>(QStringLiteral("pileTable"));
    QVERIFY(stationTable && pileTable);

    const auto stations = store.listStations();
    stationTable->selectRow(0);
    QCOMPARE(pileTable->rowCount(), store.listPiles(stations.at(0).id).size());

    stationTable->selectRow(1);
    QCOMPARE(pileTable->rowCount(), store.listPiles(stations.at(1).id).size());
    QCOMPARE(pileTable->item(0, 1)->text(), store.listPiles(stations.at(1).id).at(0).code);
}

QTEST_MAIN(TstStationUi)

#include "tst_stationui.moc"
