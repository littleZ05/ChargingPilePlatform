#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>

#include "mainwindow.h"
#include "stationstore.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 演示数据库放在可执行文件同级目录（*.db 已被 .gitignore 排除，不进入版本库）
    const QString dbPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("chargingpile.db"));

    QString error;
    pcserver::StationStore store;
    if (!store.open(dbPath, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("数据库初始化失败"), error);
        return 1;
    }

    QString seedError;
    if (!store.seedDemoIfEmpty(&seedError))
        qWarning() << "[station] 演示数据初始化失败:" << seedError;

    MainWindow w(&store);
    w.show();
    return app.exec();
}
