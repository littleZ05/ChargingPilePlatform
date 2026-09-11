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
#include "opsconsole.h"
#include "uitheme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ChargingPilePlatform"));
    QCoreApplication::setApplicationName(QStringLiteral("PcServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    // NO.20：尽早接管 Qt 日志，把告警/错误收进「运行日志」页（控制台输出保留）
    pcserver::installRunLogHandler();

    // NO.18：应用启动即注入内置深色主题（登录框与主窗口一致；失败自动兜底）
    pcserver::applyUiTheme(&app);

    // P0 统一数据源：全进程只有一个 SQLite 库文件
    // 优先 PCSERVER_DB_PATH（测试/隔离演示），否则 <程序目录>/chargingpile.db
    QString dbPath;
    const QByteArray envDb = qgetenv("PCSERVER_DB_PATH");
    if (!envDb.isEmpty())
        dbPath = QString::fromLocal8Bit(envDb);
    else
        dbPath = QDir(QCoreApplication::applicationDirPath())
                     .filePath(QStringLiteral("chargingpile.db"));

    pcserver::StationStore store;
    QString error;
    if (!store.open(dbPath, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("数据库初始化失败"), error);
        return 1;
    }

    // 关键：管理后台（登录/业绩/桩状态/桩管理/用户管理）与 StationStore 使用同一个库，
    // 消除原先“后台读另一个库、用户端订单永远进不来”的双库分裂问题。
    pcserver::setAdminDatabasePath(dbPath);

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
    // NO.17 → 创新点1 闭环：负荷预测换算“未来 1 小时预测空闲率”，
    // 与「价格策略」页展示的口径完全一致（同一个 predictIdleRatePercent）。
    pricing.setIdleRateProvider([&store](int stationId) -> double {
        return pcserver::predictIdleRatePercent(store, stationId);
    });
    pricing.start(10000);

    pcserver::SelfHealService selfHeal(dbPath);
    QObject::connect(&selfHeal, &pcserver::SelfHealService::message,
                     [](const QString &m) { qInfo().noquote() << m; });
    selfHeal.start(10000);

    return app.exec();
}
