#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>

#include <algorithm>

#include "mainwindow.h"
#include "stationstore.h"
#include "pricingservice.h"
#include "selfhealservice.h"
#include "loadforecast.h"

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

    // 开发/自动化演示：PCSERVER_AUTOLOGIN=1 时跳过登录（正常使用仍走登录框）
    QString adminName;
    if (qEnvironmentVariableIntValue("PCSERVER_AUTOLOGIN")) {
        adminName = QStringLiteral("admin");
    } else if (!pcserver::showAdminLogin(nullptr, &adminName)) {
        return 0;
    }

    MainWindow window(&store, adminName);
    window.show();

    // 创新点落地：服务器端自动服务（价格策略 + 自愈检查），无需人工操作
    pcserver::PricingService pricing(dbPath);
    QObject::connect(&pricing, &pcserver::PricingService::message,
                     [](const QString &m) { qInfo().noquote() << m; });
    // NO.17 → 创新点1 闭环：预测未来 1h 负荷 / 站额定容量 ⇒ 预测空闲率(%)
    pricing.setIdleRateProvider([&store](int stationId) -> double {
        QString err;
        const double capacityKw = store.ratedCapacityKw(stationId, &err);
        if (capacityKw <= 0.0)
            return 0.0;
        bool usedDemo = false;
        const QVector<double> history =
            store.hourlyLoadSamples(stationId, 12, &usedDemo, &err);
        cp::LoadForecastInput input;
        input.historyKw = history;
        input.horizonHours = 1;
        input.capacityKw = capacityKw;
        const cp::LoadForecastResult result = cp::forecastLoad(input);
        if (!result.ok || result.forecastKw.isEmpty())
            return 0.0;
        const double predictedKw = result.forecastKw.first();
        const double idlePercent =
            std::max(0.0, std::min(100.0, (1.0 - predictedKw / capacityKw) * 100.0));
        return idlePercent;
    });
    pricing.start(10000);

    pcserver::SelfHealService selfHeal(dbPath);
    QObject::connect(&selfHeal, &pcserver::SelfHealService::message,
                     [](const QString &m) { qInfo().noquote() << m; });
    selfHeal.start(10000);

    return app.exec();
}
