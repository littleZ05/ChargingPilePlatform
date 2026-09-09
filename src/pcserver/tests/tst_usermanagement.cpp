#include <QtTest/QtTest>

#include <QLineEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>

#include "../mainwindow.h"
#include "../stationstore.h"

using namespace pcserver;

class TstUserManagement : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_dbPath;

private slots:
    void initTestCase();
    void userTableHeadersAndSeedRows();
    void fuzzyPhoneSearchAndLikeEscape();
    void freezeAndUnfreezeUser();
    void cleanupTestCase();
};

void TstUserManagement::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("user-management-test.db"));
    qputenv("PCSERVER_DB_PATH", m_dbPath.toLocal8Bit());
}

void TstUserManagement::userTableHeadersAndSeedRows()
{
    QString error;
    StationStore store;
    QVERIFY2(store.open(m_dir.filePath(QStringLiteral("station-test.db")), &error),
             qPrintable(error));

    MainWindow window(&store);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("userTable"));
    QVERIFY(table);

    QCOMPARE(table->columnCount(), 6);
    const QStringList expected = {
        QStringLiteral("用户ID"), QStringLiteral("手机号"), QStringLiteral("昵称"),
        QStringLiteral("钱包余额(元)"), QStringLiteral("注册时间"), QStringLiteral("状态")
    };
    for (int col = 0; col < expected.size(); ++col) {
        QCOMPARE(table->horizontalHeaderItem(col)->text(), expected.at(col));
    }

    // DatabaseManager 首次初始化会灌入 5 个演示用户
    QCOMPARE(table->rowCount(), 5);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("13800000001"));
    QCOMPARE(table->item(0, 5)->text(), QStringLiteral("正常"));
}

void TstUserManagement::fuzzyPhoneSearchAndLikeEscape()
{
    QString error;
    StationStore store;
    QVERIFY2(store.open(m_dir.filePath(QStringLiteral("station-test2.db")), &error),
             qPrintable(error));

    MainWindow window(&store);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("userTable"));
    auto *edit = window.findChild<QLineEdit *>(QStringLiteral("userSearchEdit"));
    QVERIFY(table && edit);

    edit->setText(QStringLiteral("00000002"));
    window.refreshUsers();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("13800000002"));

    edit->setText(QStringLiteral("138000000"));
    window.refreshUsers();
    QCOMPARE(table->rowCount(), 5);

    // % 和 _ 应作为普通字符，不能变成通配符扩大结果
    edit->setText(QStringLiteral("%"));
    window.refreshUsers();
    QCOMPARE(table->rowCount(), 0);
    edit->setText(QStringLiteral("1380000000_"));
    window.refreshUsers();
    QCOMPARE(table->rowCount(), 0);
}

void TstUserManagement::freezeAndUnfreezeUser()
{
    QString error;
    StationStore store;
    QVERIFY2(store.open(m_dir.filePath(QStringLiteral("station-test3.db")), &error),
             qPrintable(error));

    MainWindow window(&store);
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("userTable"));
    auto *freezeButton = window.findChild<QPushButton *>(QStringLiteral("userFreezeButton"));
    auto *unfreezeButton = window.findChild<QPushButton *>(QStringLiteral("userUnfreezeButton"));
    QVERIFY(table && freezeButton && unfreezeButton);

    // 选中“13800000002”，先冻结
    int targetRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 1)->text() == QStringLiteral("13800000002")) {
            targetRow = row;
            break;
        }
    }
    QVERIFY(targetRow >= 0);
    table->selectRow(targetRow);
    QCOMPARE(table->item(targetRow, 5)->text(), QStringLiteral("正常"));
    QVERIFY(freezeButton->isEnabled());    // 正常 -> 可冻结
    QVERIFY(!unfreezeButton->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&window, "changeSelectedUserStatus",
                                      Q_ARG(int, 1)));
    QCOMPARE(table->item(targetRow, 5)->text(), QStringLiteral("冻结"));
    QVERIFY(unfreezeButton->isEnabled());
    QVERIFY(!freezeButton->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&window, "changeSelectedUserStatus",
                                      Q_ARG(int, 0)));
    QCOMPARE(table->item(targetRow, 5)->text(), QStringLiteral("正常"));
    QVERIFY(freezeButton->isEnabled());
    QVERIFY(!unfreezeButton->isEnabled());

    // 落库持久化验证：直接查询同一数据库文件
    QSqlDatabase checkDb = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("usermanage_check"));
    checkDb.setDatabaseName(m_dbPath);
    QVERIFY(checkDb.open());
    QSqlQuery query(checkDb);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT status FROM users WHERE phone = '13800000002'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
    query.clear();
    checkDb.close();
    QSqlDatabase::removeDatabase(QStringLiteral("usermanage_check"));
}

void TstUserManagement::cleanupTestCase()
{
    qunsetenv("PCSERVER_DB_PATH");
}

QTEST_MAIN(TstUserManagement)

#include "tst_usermanagement.moc"
