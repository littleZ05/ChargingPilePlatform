#include <QtTest/QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>

#include "../addstationdialog.h"
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
    void addStationDialogRejectsInvalidInput();
    void addStationDialogAcceptsValidInput();
    void realtimeSimulationTickChangesPileState();
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

void TstStationUi::addStationDialogRejectsInvalidInput()
{
    AddStationDialog dialog;
    auto *nameEdit = dialog.findChild<QLineEdit *>(QStringLiteral("stationNameEdit"));
    auto *errorLabel = dialog.findChild<QLabel *>(QStringLiteral("addStationErrorLabel"));
    QVERIFY(nameEdit && errorLabel);

    nameEdit->clear();
    QString error;
    QVERIFY(!dialog.tryAccept(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!errorLabel->text().isEmpty());
    QVERIFY(dialog.result() != QDialog::Accepted);
}

void TstStationUi::addStationDialogAcceptsValidInput()
{
    AddStationDialog dialog;
    auto *nameEdit = dialog.findChild<QLineEdit *>(QStringLiteral("stationNameEdit"));
    auto *addressEdit = dialog.findChild<QLineEdit *>(QStringLiteral("stationAddressEdit"));
    auto *longitudeSpin =
        dialog.findChild<QDoubleSpinBox *>(QStringLiteral("stationLongitudeSpin"));
    auto *latitudeSpin =
        dialog.findChild<QDoubleSpinBox *>(QStringLiteral("stationLatitudeSpin"));
    auto *pileCountSpin =
        dialog.findChild<QSpinBox *>(QStringLiteral("stationPileCountSpin"));
    QVERIFY(nameEdit && addressEdit && longitudeSpin && latitudeSpin && pileCountSpin);

    nameEdit->setText(QStringLiteral(" 新增对话框测试站 "));
    addressEdit->setText(QStringLiteral(" 沈阳市测试路 99 号 "));
    longitudeSpin->setValue(123.654321);
    latitudeSpin->setValue(41.876543);
    pileCountSpin->setValue(8);

    QString error;
    QVERIFY2(dialog.tryAccept(&error), qPrintable(error));
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(dialog.stationName(), QStringLiteral("新增对话框测试站"));
    QCOMPARE(dialog.address(), QStringLiteral("沈阳市测试路 99 号"));
    QCOMPARE(dialog.longitude(), 123.654321);
    QCOMPARE(dialog.latitude(), 41.876543);
    QCOMPARE(dialog.pileCount(), 8);
}

void TstStationUi::realtimeSimulationTickChangesPileState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    StationStore store;
    QVERIFY2(store.open(dir.filePath(QStringLiteral("ui6.db")), &error), qPrintable(error));
    QVERIFY2(store.seedDemoIfEmpty(&error), qPrintable(error));

    MainWindow window(&store);
    auto *stationTable = window.findChild<QTableWidget *>(QStringLiteral("stationTable"));
    QVERIFY(stationTable);

    const auto stations = store.listStations();
    QVERIFY(!stations.isEmpty());
    stationTable->selectRow(0);

    const auto before = store.listPiles(stations.at(0).id);
    QVERIFY(!before.isEmpty());

    QVERIFY2(QMetaObject::invokeMethod(&window, "simulateRealtimeOnce"),
             "无法调用 simulateRealtimeOnce");

    const auto after = store.listPiles(stations.at(0).id);
    bool changed = false;
    for (int i = 0; i < after.size(); ++i) {
        if (after.at(i).state != before.at(i).state) {
            changed = true;
            break;
        }
    }
    QVERIFY2(changed, "实时模拟调用后应至少有一根电桩状态发生变化");
}

QTEST_MAIN(TstStationUi)

#include "tst_stationui.moc"
