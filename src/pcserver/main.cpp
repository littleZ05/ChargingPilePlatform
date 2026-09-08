#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>

#include "mainwindow.h"
#include "stationstore.h"
#include "pricingservice.h"
#include "selfhealservice.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ChargingPilePlatform"));
    QCoreApplication::setApplicationName(QStringLiteral("PcServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    const QString dbPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("chargingpile.db"));

    QString error;
    pcserver::StationStore store;
    if (!store.open(dbPath, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("数据库初始化失败"), error);
        return 1;
    }

    QString seedError;
    if (!store.seedDemoIfEmpty(&seedError)) {
        qWarning() << "[station] 演示数据初始化失败:" << seedError;
    }

    QString adminName;
    if (!pcserver::showAdminLogin(nullptr, &adminName)) {
        return 0;
    }

    MainWindow window(&store, adminName);
    window.show();

    // 创新点落地：服务器端自动服务（价格策略 + 自愈检查），无需人工操作
    pcserver::PricingService pricing(dbPath);
    QObject::connect(&pricing, &pcserver::PricingService::message,
                     [](const QString &m) { qInfo().noquote() << m; });
    pricing.start(30000);

    pcserver::SelfHealService selfHeal(dbPath);
    QObject::connect(&selfHeal, &pcserver::SelfHealService::message,
                     [](const QString &m) { qInfo().noquote() << m; });
    selfHeal.start(10000);

    return app.exec();
}
