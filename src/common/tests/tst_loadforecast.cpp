#include <QtTest/QtTest>

#include <cmath>

#include "../loadforecast.h"

/** NO.17 充电负荷智能预测 简化模型 单元测试（负责人：吴羽桐） */
class TestLoadForecast : public QObject
{
    Q_OBJECT
private slots:
    void olsLinearRampForecastFollowsSlope();
    void olsConstantHistoryStaysFlatWithoutDivisionByZero();
    void wmaLevelForecastIsStableAndFinite();
    void insufficientHistoryIsRejected();
    void invalidValuesAreRejected();
    void capacityClampsForecast();
    void horizonIsClampedToSupportedRange();
    void windowSizeVariantsStayReasonable();
};

namespace {

QVector<double> ramp(int count, double start, double step)
{
    QVector<double> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i)
        out << start + i * step;
    return out;
}

bool near(double a, double b, double eps = 1e-6)
{
    return qAbs(a - b) <= eps;
}

} // namespace

void TestLoadForecast::olsLinearRampForecastFollowsSlope()
{
    // y = 100 + 2*i（i=0..23）：OLS 应恢复斜率 2，水平 146（最近点拟合值）
    const QVector<double> history = ramp(24, 100.0, 2.0);
    cp::LoadForecastInput in;
    in.historyKw = history;
    in.horizonHours = 6;
    in.model = cp::ForecastModel::OLS;

    const cp::LoadForecastResult r = cp::forecastLoad(in);
    QVERIFY(r.ok);
    QCOMPARE(r.samplesUsed, 24);
    QVERIFY(near(r.slopeKwPerHour, 2.0));
    QVERIFY(near(r.levelKw, 146.0));
    QCOMPARE(r.trend, cp::LoadTrend::Rising);
    QCOMPARE(r.forecastKw.size(), 6);

    // 未来第 h 小时 = 146 + 2*h
    for (int h = 1; h <= 6; ++h)
        QVERIFY2(near(r.forecastKw.at(h - 1), 146.0 + 2.0 * h),
                 qPrintable(QStringLiteral("h=%1 预测=%2").arg(h).arg(r.forecastKw.at(h - 1))));

    QCOMPARE(r.peakHourOffset, 6);
    QVERIFY(near(r.peakForecastKw, 158.0));
    QVERIFY(r.modelName.contains(QStringLiteral("OLS")));
}

void TestLoadForecast::olsConstantHistoryStaysFlatWithoutDivisionByZero()
{
    QVector<double> history;
    for (int i = 0; i < 12; ++i)
        history << 60.0;

    cp::LoadForecastInput in;
    in.historyKw = history;
    in.horizonHours = 6;

    const cp::LoadForecastResult r = cp::forecastLoad(in);
    QVERIFY(r.ok);
    QVERIFY(std::isfinite(r.slopeKwPerHour));
    QVERIFY(near(r.slopeKwPerHour, 0.0));   // 零方差不除零，斜率按 0 处理
    QVERIFY(near(r.levelKw, 60.0));
    QCOMPARE(r.trend, cp::LoadTrend::Steady);
    for (double v : r.forecastKw)
        QVERIFY(near(v, 60.0));
    QVERIFY(near(r.peakForecastKw, 60.0));
}

void TestLoadForecast::wmaLevelForecastIsStableAndFinite()
{
    // 线性上升序列下 WMA 只给出加权水平，输出恒有限且非负
    const QVector<double> history = ramp(24, 100.0, 2.0);
    cp::LoadForecastInput in;
    in.historyKw = history;
    in.horizonHours = 6;
    in.model = cp::ForecastModel::WMA;

    const cp::LoadForecastResult r = cp::forecastLoad(in);
    QVERIFY(r.ok);
    QVERIFY(near(r.slopeKwPerHour, 0.0));
    QCOMPARE(r.trend, cp::LoadTrend::Steady);
    QCOMPARE(r.forecastKw.size(), 6);

    // 最近 6 点 136..146、线性权重 1..6：均值 = 2996/21
    QVERIFY(near(r.levelKw, 2996.0 / 21.0));
    for (double v : r.forecastKw) {
        QVERIFY(std::isfinite(v));
        QVERIFY(v >= 0.0);
        QVERIFY(near(v, r.levelKw));
    }
    QVERIFY(r.modelName.contains(QStringLiteral("WMA")));
}

void TestLoadForecast::insufficientHistoryIsRejected()
{
    cp::LoadForecastInput in;
    in.horizonHours = 6;

    cp::LoadForecastResult empty = cp::forecastLoad(in);
    QVERIFY(!empty.ok);
    QVERIFY(empty.error.contains(QStringLiteral("为空")));

    in.historyKw = { 100.0, 120.0 };
    cp::LoadForecastResult short_ = cp::forecastLoad(in);
    QVERIFY(!short_.ok);
    QVERIFY(short_.error.contains(QStringLiteral("至少需要 3")));
    QVERIFY(short_.forecastKw.isEmpty());
}

void TestLoadForecast::invalidValuesAreRejected()
{
    cp::LoadForecastInput in;
    in.horizonHours = 6;

    in.historyKw = { 100.0, 120.0, std::nan("") };
    QVERIFY(!cp::forecastLoad(in).ok);

    in.historyKw = { 100.0, 120.0, -5.0 };
    QVERIFY(!cp::forecastLoad(in).ok);
}

void TestLoadForecast::capacityClampsForecast()
{
    const QVector<double> history = ramp(12, 90.0, 10.0);  // 末端 200，趋势斜率 10
    cp::LoadForecastInput in;
    in.historyKw = history;
    in.horizonHours = 6;
    in.capacityKw = 180.0;

    const cp::LoadForecastResult r = cp::forecastLoad(in);
    QVERIFY(r.ok);
    for (double v : r.forecastKw) {
        QVERIFY(v >= 0.0);
        QVERIFY(v <= 180.0 + 1e-9);
    }
    QVERIFY(r.peakForecastKw <= 180.0 + 1e-9);
}

void TestLoadForecast::horizonIsClampedToSupportedRange()
{
    const QVector<double> history = ramp(12, 50.0, 1.0);

    cp::LoadForecastInput tooLarge;
    tooLarge.historyKw = history;
    tooLarge.horizonHours = 99;
    QCOMPARE(cp::forecastLoad(tooLarge).forecastKw.size(), 6);

    cp::LoadForecastInput tooSmall;
    tooSmall.historyKw = history;
    tooSmall.horizonHours = 0;
    QCOMPARE(cp::forecastLoad(tooSmall).forecastKw.size(), 1);
}

void TestLoadForecast::windowSizeVariantsStayReasonable()
{
    // 12h 与 24h 两种历史窗口都产出有限、单调一致的结果（答辩两种口径都可用）
    const QVector<double> history12 = ramp(12, 100.0, 2.0);
    const QVector<double> history24 = ramp(24, 76.0, 2.0);

    cp::LoadForecastInput in12;
    in12.historyKw = history12;
    in12.horizonHours = 6;
    const cp::LoadForecastResult r12 = cp::forecastLoad(in12);

    cp::LoadForecastInput in24;
    in24.historyKw = history24;
    in24.horizonHours = 6;
    const cp::LoadForecastResult r24 = cp::forecastLoad(in24);

    QVERIFY(r12.ok);
    QVERIFY(r24.ok);
    QVERIFY(near(r12.slopeKwPerHour, 2.0));
    QVERIFY(near(r24.slopeKwPerHour, 2.0));
    for (int h = 1; h <= 6; ++h) {
        QVERIFY(std::isfinite(r12.forecastKw.at(h - 1)));
        QVERIFY(std::isfinite(r24.forecastKw.at(h - 1)));
    }
    QCOMPARE(r12.trend, cp::LoadTrend::Rising);
    QCOMPARE(r24.trend, cp::LoadTrend::Rising);
}

QTEST_APPLESS_MAIN(TestLoadForecast)
#include "tst_loadforecast.moc"
