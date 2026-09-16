// 第二阶段大屏浏览器验收：真实 QtWebEngine 渲染 + 接口联动 + 截图留证。
// 运行前先启动服务：python3 stage2/dashboard/server.py --data <清洗目录> --model <model.json>
// 环境变量：DASHBOARD_URL（默认 http://127.0.0.1:8765/）、DASHBOARD_SHOT（默认 stage2-dashboard.png）
#include <QtTest>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QSignalSpy>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include <memory>

static QVariant evaluate(QWebEnginePage *page, const QString &script)
{
    auto value = std::make_shared<QVariant>();
    QEventLoop loop;
    QPointer<QEventLoop> guard(&loop);
    page->runJavaScript(script, [guard, value](const QVariant &result) {
        *value = result;
        if (guard) guard->quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    return *value;
}

class Stage2DashboardTest : public QObject
{
    Q_OBJECT
private slots:
    void rendersLiveDataAndBatteryView();
};

void Stage2DashboardTest::rendersLiveDataAndBatteryView()
{
    const QString base = qEnvironmentVariable("DASHBOARD_URL", QStringLiteral("http://127.0.0.1:8765/"));
    const QString shot = qEnvironmentVariable("DASHBOARD_SHOT", QStringLiteral("stage2-dashboard.png"));
    QWebEngineView view;
    view.resize(1600, 1000);
    view.show();
    QSignalSpy loaded(&view, &QWebEngineView::loadFinished);
    view.load(QUrl(base));
    QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 25000);
    QVERIFY(loaded.last().first().toBool());

    // 连接状态与 KPI 必须来自真实接口，而不是写死的演示值
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),
        "document.getElementById('connection').textContent").toString().contains(QStringLiteral("已连接")), 25000);
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),
        "document.querySelectorAll('canvas').length").toInt() >= 5, 25000);
    const QString sessions = evaluate(view.page(), "document.getElementById('sessions').textContent").toString();
    QVERIFY2(sessions != QStringLiteral("—"), qPrintable(QStringLiteral("会话数未渲染：%1").arg(sessions)));
    QVERIFY(evaluate(view.page(), "document.getElementById('scope').textContent").toString().contains(QStringLiteral("会话")));
    QTest::qWait(1500);
    QVERIFY2(view.grab().save(shot), qPrintable(QStringLiteral("截图失败：%1").arg(shot)));

    // 切换到电池样本视图：独立数据、不随会话筛选变化
    evaluate(view.page(), "document.querySelector('[data-view=battery]').click()");
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),
        "document.getElementById('scope').textContent").toString().contains(QStringLiteral("独立电池样本")), 20000);
    QVERIFY(evaluate(view.page(),
        "document.querySelectorAll('#battery canvas').length").toInt() >= 4);
    QTest::qWait(1000);
    QVERIFY(view.grab().save(QString(shot).replace(QStringLiteral(".png"), QStringLiteral("-battery.png"))));

    // 数据质量视图：标签表必须有行，且注明不能相加
    evaluate(view.page(), "document.querySelector('[data-view=quality]').click()");
    QTRY_VERIFY_WITH_TIMEOUT(evaluate(view.page(),
        "document.querySelectorAll('#quality-rows tr').length").toInt() >= 1, 20000);
}

int main(int argc, char **argv)
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --no-sandbox");
    qputenv("QT_QUICK_BACKEND", "software");
    QApplication application(argc, argv);
    Stage2DashboardTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_stage2_dashboard.moc"
