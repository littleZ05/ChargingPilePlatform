// 第二阶段答辩演示：依次弹出"七环节效果图"与"实时大屏"窗口。
//
// 用法：
//   defense_demo --data <清洗结果目录> --model <model.json> --assets <PPT素材目录> [--auto 20] [--selftest --out DIR]
// 操作：
//   空格 / → / 回车：下一个窗口      ← / 退格：上一个窗口      R：重头开始      Esc / Q：退出
//   加 --auto N 则每 N 秒自动进入下一个窗口（仍可用空格手动推进）。
//
// 说明：程序会自行拉起 stage2/dashboard/server.py 并等待 /api/health，答辩时只需运行本程序。
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QKeyEvent>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QTextStream>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QThread>

struct Step
{
    QString kind;      // image / web
    QString title;     // 窗口标题（也是屏幕左上角提示）
    QString source;    // image: 文件路径；web: URL 路径（拼接 base）
    QString hint;      // 提示语（大字，放窗口底部）
    QString script;    // web 步骤在加载完成后执行的 JS（可选）
};

class Demo : public QObject
{
public:
    Demo(const QString &base, const QString &assets, int autoSeconds, bool selftest, const QString &outDir)
        : m_base(base), m_assets(assets), m_outDir(outDir), m_autoSeconds(autoSeconds), m_selftest(selftest)
    {
        m_steps = {
            {"image", "第二阶段演示 · 第 4 组", QString(), "空格：下一步　←：上一步　Esc：退出", QString()},
            {"image", "① 数据采集：四类真实数据源", "01-数据采集.png",
             "业务库 289 · 日志 187 · 课程 CSV 5094 · 设备事件 43 ＝ 5655 行", QString()},
            {"image", "② 数据清洗：六步流程与关键决策", "02-数据清洗.png",
             "3395 会话 / 1594 电池 / 105 站点全部保留并打标，隔离 0 条", QString()},
            {"image", "③ 分层存储：ODS/DWD/DWS/ADS", "03-分层存储.png",
             "41 张表 28366 行；对账 3395 会话 · 19723.69 kWh · 55 条零电量", QString()},
            {"image", "④ 数据分析：统计 / 相关 / 聚类 / 回归", "04-数据分析.png",
             "Pearson 0.316（弱相关）· 回归 R² 0.0999 · 高峰 11/12/17 点", QString()},
            {"image", "⑤ 数据预测：模型选择与评估", "05-数据预测.png",
             "季节基线 MAE 12.65 优于线性回归 20.78，模型自动选基线", QString()},
            {"image", "⑥ 业务应用：RESTful API 与即席查询", "07-接口实测.png",
             "6 个只读接口；启动时校验 manifest 哈希，失败即拒绝启动", QString()},
            {"web", "⑦ 实时大屏 · 运营概览（全量 3395 会话）", "",
             "五维筛选：站点 / 设施编码 / 平台 / 星期 / 电量", QString()},
            {"web", "⑦ 实时大屏 · 筛选联动（平台 iOS ＋ 正电量）", "",
             "会话 3395 → 2195，电量 12788.67 kWh，零电量归零",
             QStringLiteral("(function(){var p=document.getElementById('platform');p.value='ios';"
                            "p.dispatchEvent(new Event('change'));"
                            "setTimeout(function(){var e=document.getElementById('energy');"
                            "e.value='positive';e.dispatchEvent(new Event('change'));},900);})()")},
            {"web", "⑦ 实时大屏 · 电池样本（独立分析）", "",
             "1594 条电池样本，不与会话 ID 关联，可做 SOC/电压/电流/温度分布",
             QStringLiteral("document.querySelector('[data-view=battery]').click()")},
            {"web", "⑦ 实时大屏 · 数据质量与口径", "",
             "标签可重叠、不能相加成异常率；费用非营收、年份不可信",
             QStringLiteral("document.querySelector('[data-view=quality]').click()")},
            {"image", "演示结束 · 谢谢各位老师", QString(), "Esc 退出（窗口可留在屏幕上讲解）", QString()},
        };
    }

    bool start()
    {
        if (!m_selftest)
            QTimer::singleShot(0, this, &Demo::showCurrent);
        else
            QTimer::singleShot(0, this, &Demo::runSelfTest);
        return true;
    }

private:
    void showCurrent()
    {
        if (m_index >= m_steps.size()) {
            quit();
            return;
        }
        const Step &step = m_steps.at(m_index);
        QWidget *window = new QWidget;
        window->setWindowTitle(step.title);
        window->setStyleSheet(QStringLiteral("background:#0f1420;color:#e8eefb;"));
        auto *layout = new QVBoxLayout(window);
        layout->setContentsMargins(28, 24, 28, 20);
        auto *heading = new QLabel(step.title);
        heading->setStyleSheet(QStringLiteral("font-size:30px;font-weight:600;color:#e8eefb;"));
        layout->addWidget(heading);

        if (step.kind == QLatin1String("image")) {
            auto *label = new QLabel;
            label->setAlignment(Qt::AlignCenter);
            if (!step.source.isEmpty()) {
                const QString path = QDir(m_assets).filePath(step.source);
                QPixmap pixmap(path);
                if (pixmap.isNull())
                    label->setText(QStringLiteral("缺少素材：") + path);
                else
                    label->setPixmap(pixmap.scaledToWidth(1520, Qt::SmoothTransformation));
            } else {
                label->setText(QStringLiteral("按空格开始演示"));
                label->setStyleSheet(QStringLiteral("font-size:40px;color:#6ee7a0;"));
            }
            layout->addWidget(label, 1);
        } else {
            auto *view = new QWebEngineView;
            view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
            const QUrl url = step.source.isEmpty() ? QUrl(m_base) : QUrl(m_base + step.source);
            connect(view, &QWebEngineView::loadFinished, this, [this, view, step](bool ok) {
                if (!ok || step.script.isEmpty())
                    return;
                QTimer::singleShot(1200, this, [view, step] {
                    view->page()->runJavaScript(step.script);
                });
            });
            view->load(url);
            layout->addWidget(view, 1);
        }

        auto *footer = new QLabel(step.hint);
        footer->setStyleSheet(QStringLiteral("font-size:22px;color:#9fb3d1;"));
        footer->setWordWrap(true);
        layout->addWidget(footer);

        window->resize(1600, 1000);
        window->move(QGuiApplication::primaryScreen()->availableGeometry().center()
                     - window->rect().center());
        window->show();
        window->raise();
        window->activateWindow();
        window->installEventFilter(this);
        m_windows.append(window);
        if (m_windows.size() > 1) {                      // 只保留当前窗口，形成"依次弹出"
            QWidget *previous = m_windows.at(m_windows.size() - 2);
            if (previous) {
                previous->hide();
                previous->deleteLater();
            }
        }
        if (m_autoSeconds > 0)
            QTimer::singleShot(m_autoSeconds * 1000, this, &Demo::showCurrent);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::KeyPress)
            return QObject::eventFilter(object, event);
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Space || key == Qt::Key_Right || key == Qt::Key_Return
            || key == Qt::Key_Down || key == Qt::Key_PageDown)
            showCurrent();
        else if (key == Qt::Key_Left || key == Qt::Key_Backspace || key == Qt::Key_Up)
            step(-2);
        else if (key == Qt::Key_R)
            step(-1 - m_index);
        else if (key == Qt::Key_Escape || key == Qt::Key_Q)
            quit();
        else
            return QObject::eventFilter(object, event);
        return true;
    }

private:
    void step(int delta)
    {
        m_index = qBound(0, m_index + delta, m_steps.size() - 1);
        showCurrent();
    }

    void runSelfTest()
    {
        m_selftestIndex = 0;
        selfTestNext();
    }

    void selfTestNext()
    {
        if (m_selftestIndex >= m_steps.size()) {
            QTextStream(stdout) << "自检完成，共 " << m_steps.size() << " 个窗口\n";
            quit();
            return;
        }
        const Step &step = m_steps.at(m_selftestIndex);
        QString safeTitle = step.title;
        safeTitle.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'))
                 .replace(QLatin1Char(':'), QLatin1Char('-'));
        const QString target = QDir(m_outDir).filePath(
            QStringLiteral("%1-").arg(m_selftestIndex + 1, 2, 10, QLatin1Char('0')) + safeTitle + ".png");
        if (step.kind == QLatin1String("image")) {
            QWidget window;
            window.setStyleSheet(QStringLiteral("background:#0f1420;color:#e8eefb;"));
            auto *layout = new QVBoxLayout(&window);
            auto *heading = new QLabel(step.title);
            heading->setStyleSheet(QStringLiteral("font-size:30px;font-weight:600;"));
            layout->addWidget(heading);
            auto *label = new QLabel(QStringLiteral("按空格开始演示"));
            label->setAlignment(Qt::AlignCenter);
            if (!step.source.isEmpty()) {
                QPixmap pixmap(QDir(m_assets).filePath(step.source));
                if (!pixmap.isNull())
                    label->setPixmap(pixmap.scaledToWidth(1500, Qt::SmoothTransformation));
                else
                    label->setText(QStringLiteral("缺少素材"));
            }
            layout->addWidget(label, 1);
            auto *footer = new QLabel(step.hint);
            footer->setStyleSheet(QStringLiteral("font-size:22px;color:#9fb3d1;"));
            footer->setWordWrap(true);
            layout->addWidget(footer);
            window.resize(1600, 1000);
            window.show();
            QPixmap shot = window.grab();
            const bool saved = shot.save(target);
            QTextStream(stdout) << (saved ? "OK   " : "FAIL ") << target << "\n";
            m_selftestIndex++;
            QTimer::singleShot(200, this, &Demo::selfTestNext);
            return;
        }
        QWebEngineView *view = new QWebEngineView;
        view->resize(1600, 1000);
        view->show();
        const QUrl url = step.source.isEmpty() ? QUrl(m_base) : QUrl(m_base + step.source);
        connect(view, &QWebEngineView::loadFinished, this, [this, view, step, target](bool ok) {
            if (!ok) {
                QTextStream(stdout) << "FAIL 加载失败 " << step.title << "\n";
                m_selftestIndex++;
                QTimer::singleShot(200, this, &Demo::selfTestNext);
                return;
            }
            QTimer::singleShot(2500, this, [this, view, step, target] {
                if (!step.script.isEmpty())
                    view->page()->runJavaScript(step.script);
                QTimer::singleShot(2500, this, [this, view, target] {
                    const bool saved = view->grab().save(target);
                    QTextStream(stdout) << (saved ? "OK   " : "FAIL ") << target << "\n";
                    view->hide();
                    view->deleteLater();
                    m_selftestIndex++;
                    selfTestNext();
                });
            });
        });
        view->load(url);
    }

    void quit()
    {
        if (m_serverProcess) {
            m_serverProcess->terminate();
            m_serverProcess->waitForFinished(3000);
        }
        qApp->quit();
    }

public:
    void startServer(const QString &script, const QString &data, const QString &model, int port)
    {
        m_serverProcess = new QProcess(this);
        QStringList args{script, QStringLiteral("--data"), data, QStringLiteral("--port"), QString::number(port)};
        if (!model.isEmpty())
            args << QStringLiteral("--model") << model;
        m_serverProcess->setProcessChannelMode(QProcess::ForwardedChannels);
        m_serverProcess->start(QStringLiteral("python3"), args);
        QTextStream(stdout) << "已启动大屏服务：python3 " << args.join(' ') << "\n";
    }

private:
    QString m_base, m_assets, m_outDir;
    int m_autoSeconds = 0, m_index = 0, m_selftestIndex = 0;
    bool m_selftest = false;
    QList<QPointer<QWidget>> m_windows;
    QList<Step> m_steps;
    QProcess *m_serverProcess = nullptr;
};

static bool waitForHealth(const QString &base, int timeoutMs)
{
    QNetworkAccessManager manager;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        QNetworkRequest request(QUrl(base + QStringLiteral("/api/health")));
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        QNetworkReply *reply = manager.get(request);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        const bool ok = reply->error() == QNetworkReply::NoError
                && reply->readAll().contains("\"status\": \"ok\"");
        reply->deleteLater();
        if (ok)
            return true;
        QThread::msleep(700);
    }
    return false;
}

static QString resolveServerScript()
{
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../stage2/dashboard/server.py")),
        QDir::current().filePath(QStringLiteral("stage2/dashboard/server.py")),
        QStringLiteral("/home/bit/桌面/dongruan_ws/ChargingPilePlatform/stage2/dashboard/server.py")};
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).canonicalFilePath();
    }
    return QString();
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);   // 逐帧自检与切窗时不要因窗口销毁而退出
    argc = application.arguments().size();
    QString data, model, assets, out = QStringLiteral("defense-selftest");
    int port = 8765, autoSeconds = 0;
    bool selftest = false, launch = true, noServer = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = application.arguments().at(i);
        const bool hasNext = i + 1 < argc;
        if (arg == "--data" && hasNext) data = application.arguments().at(++i);
        else if (arg == "--model" && hasNext) model = application.arguments().at(++i);
        else if (arg == "--assets" && hasNext) assets = application.arguments().at(++i);
        else if (arg == "--port" && hasNext) port = application.arguments().at(++i).toInt();
        else if (arg == "--auto" && hasNext) autoSeconds = application.arguments().at(++i).toInt();
        else if (arg == "--out" && hasNext) out = application.arguments().at(++i);
        else if (arg == "--no-server") { launch = false; noServer = true; }
        else if (arg == "--selftest") selftest = true;
    }
    if (assets.isEmpty()) {
        QTextStream(stderr) << "必须提供 --assets <PPT素材目录>\n";
        return 2;
    }
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(port);
    Demo demo(base, assets, autoSeconds, selftest, out);
    if (launch && !selftest) {
        const QString script = resolveServerScript();
        if (QFileInfo::exists(script) && !waitForHealth(base, 500))
            demo.startServer(script, data, model, port);
        if (!waitForHealth(base, 15000)) {
            QTextStream(stderr) << "大屏服务未就绪，请先手动启动 server.py\n";
            return 3;
        }
    } else if (!selftest && !waitForHealth(base, 5000)) {
        QTextStream(stderr) << "无法连接 " << base << "，请先启动大屏服务\n";
        return 3;
    }
    if (selftest) {
        QDir().mkpath(out);
        if (!noServer)
            demo.startServer(resolveServerScript(), data, model, port);
        if (!waitForHealth(base, 20000)) {
            QTextStream(stderr) << "自检无法连接大屏服务\n";
            return 3;
        }
    }
    demo.start();
    return application.exec();
}
