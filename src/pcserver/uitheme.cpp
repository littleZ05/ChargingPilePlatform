#include "uitheme.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace pcserver {
namespace {

const QString kThemeResource = QStringLiteral(":/styles/theme.qss");

void applyDarkPalette(QApplication *app)
{
    QPalette palette;

    const QColor window(QStringLiteral("#121A24"));
    const QColor base(QStringLiteral("#0E1722"));
    const QColor alternateBase(QStringLiteral("#16212F"));
    const QColor button(QStringLiteral("#263241"));
    const QColor text(QStringLiteral("#E6EDF3"));
    const QColor secondaryText(QStringLiteral("#8FA0B2"));
    const QColor disabledText(QStringLiteral("#5B6B7C"));
    const QColor highlight(QStringLiteral("#1890FF"));
    const QColor border(QStringLiteral("#2A3547"));

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternateBase);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, secondaryText);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#2E3C4E")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#263241")));
    palette.setColor(QPalette::Dark, border);
    palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#0A0F16")));
    palette.setColor(QPalette::Light, QColor(QStringLiteral("#3B4B60")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#1F2B3B")));
    palette.setColor(QPalette::ToolTipText, text);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Highlight,
                     QColor(QStringLiteral("#223042")));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     QColor(QStringLiteral("#5B6B7C")));

    app->setPalette(palette);
}

} // namespace

bool applyUiTheme(QApplication *app, QString *error)
{
    static bool s_installed = false;
    if (s_installed) {
        if (error) {
            error->clear();
        }
        return true;
    }

    if (!app) {
        const QString message =
            QStringLiteral("QApplication 不可用，跳过主题注入");
        if (error) {
            *error = message;
        }
        qWarning().noquote() << "[ui] 主题注入失败：" << message;
        return false;
    }

    QFile file(kThemeResource);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString message = QStringLiteral("无法读取内置主题资源 %1（%2）")
                                    .arg(kThemeResource, file.errorString());
        if (error) {
            *error = message;
        }
        qWarning().noquote() << "[ui] 主题加载失败：" << message
                             << "（已安全回退到系统默认样式）";
        return false;
    }

    const QByteArray qssBytes = file.readAll();
    file.close();

    // Fusion 保证 QSS 的圆角/边框/伪状态在各平台渲染一致；不存在时静默跳过。
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app->setStyle(fusion);
    }
    applyDarkPalette(app);
    app->setStyleSheet(QString::fromUtf8(qssBytes));
    s_installed = true;

    qInfo().noquote()
        << QStringLiteral("[ui] 暗色工控主题已加载（%1，%2 bytes）")
               .arg(kThemeResource)
               .arg(qssBytes.size());
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace pcserver
