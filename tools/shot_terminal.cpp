// 把真实命令输出渲染成终端风格 PNG（离屏渲染，供 PPT 使用）。
// 用法：shot_terminal <标题> <输出.png> <输入.txt> [宽]
// 说明：输入文本必须是真实运行结果，工具只做排版，不改写内容。
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDateTime>

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    if (argc < 4) {
        QTextStream(stderr) << "用法：shot_terminal <标题> <输出.png> <输入.txt> [宽]\n";
        return 2;
    }
    const QString title = QString::fromUtf8(argv[1]);
    const QString target = QString::fromUtf8(argv[2]);
    const QString source = QString::fromUtf8(argv[3]);
    const int width = argc > 4 ? QString(argv[4]).toInt() : 1600;

    QFile file(source);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream(stderr) << "无法读取输入：" << source << "\n";
        return 3;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();

    QFont font(QStringLiteral("Noto Sans Mono"));
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(20);
    const QFontMetrics metrics(font);
    const int lineHeight = metrics.height() + 6;
    const int padding = 34;
    const int headerHeight = 74;
    const int footerHeight = 56;
    const int maxLines = 34;
    const bool truncated = lines.size() > maxLines;
    if (truncated)
        lines = lines.mid(lines.size() - maxLines);          // 保留尾部结论
    const int height = headerHeight + footerHeight + padding * 2 + lines.size() * lineHeight;

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#12161f"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // 标题栏
    painter.fillRect(QRect(0, 0, width, headerHeight), QColor("#1c2333"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ff5f57"));
    painter.drawEllipse(QPointF(34, headerHeight / 2), 9, 9);
    painter.setBrush(QColor("#febc2e"));
    painter.drawEllipse(QPointF(64, headerHeight / 2), 9, 9);
    painter.setBrush(QColor("#28c840"));
    painter.drawEllipse(QPointF(94, headerHeight / 2), 9, 9);
    painter.setPen(QColor("#e6ecf5"));
    QFont titleFont = font;
    titleFont.setPixelSize(22);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRect(130, 0, width - 160, headerHeight), Qt::AlignVCenter | Qt::AlignLeft,
                     title + QStringLiteral("   ·   第4组 第二阶段"));

    // 正文
    painter.setFont(font);
    int y = headerHeight + padding + metrics.ascent();
    for (const QString &raw : lines) {
        const QString line = raw.size() > 190 ? raw.left(187) + QStringLiteral("...") : raw;
        QColor color("#c9d4e3");
        if (line.startsWith(QLatin1String("$ ")))
            color = QColor("#6ee7a0");                        // 命令
        else if (line.contains(QLatin1String("失败")) || line.contains(QLatin1String("错误"))
                 || line.contains(QLatin1String("FAIL")))
            color = QColor("#ff8a80");
        else if (line.startsWith(QLatin1String("#")) || line.startsWith(QLatin1String("##")))
            color = QColor("#8ab4ff");
        else if (line.contains(QLatin1String("|")))
            color = QColor("#a5d6ff");
        painter.setPen(color);
        painter.drawText(padding, y, line);
        y += lineHeight;
    }

    // 页脚
    painter.setPen(QColor("#7d8ba3"));
    QFont footerFont = font;
    footerFont.setPixelSize(16);
    painter.setFont(footerFont);
    const QString footer = QStringLiteral("真实运行结果 · %1%2")
            .arg(truncated ? QStringLiteral("仅显示末尾若干行 · ") : QString())
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
    painter.drawText(QRect(padding, height - footerHeight, width - padding * 2, footerHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, footer);
    painter.end();

    if (!image.save(target)) {
        QTextStream(stderr) << "保存失败：" << target << "\n";
        return 4;
    }
    QTextStream(stdout) << target << " " << width << "x" << height << " lines=" << lines.size() << "\n";
    return 0;
}
