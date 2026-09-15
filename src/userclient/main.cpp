#include <QApplication>
#include "mainwindow.h"
#include "user_theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ChargingPilePlatform"));
    QCoreApplication::setApplicationName(QStringLiteral("UserClient"));
    applyUserTheme(app);

    MainWindow w;
    w.show();
    return app.exec();
}
