#include "loadforecast.h"

#include <algorithm>
#include <cmath>

namespace cp {
namespace {

constexpr int kMaxSamplesPerModel = 24;  // WMA/OLS 内部最多使用最近 24 个点

double clampLoad(double value, double capacityKw)
{
    double v = std::max(0.0, value);
    if (capacityKw > 0.0)
        v = std::min(capacityKw, v);
    return v;
}

bool validateSamples(const QVector<double> &history, QString *error)
{
    if (history.isEmpty()) {
        if (error) *error = QStringLiteral("历史负荷采样为空，无法预测");
        return false;
    }
    if (history.size() < LoadForecast::kMinSamples) {
        if (error) {
            *error = QStringLiteral("历史负荷采样不足：至少需要 %1 个整点样本（当前 %2）")
                         .arg(LoadForecast::kMinSamples)
                         .arg(history.size());
        }
        return false;
    }
    for (double v : history) {
        if (!std::isfinite(v) || v < 0.0) {
            if (error)
                *error = QStringLiteral("历史负荷采样含非法值（负值或非数值），已拒绝预测");
            return false;
        }
    }
    return true;
}

/** OLS：以最近点 t=0，拟合 y = a + b*t，返回 a/b（除零时 b=0） */
void olsFit(const QVector<double> &history, int m, double *level, double *slope)
{
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    for (int i = 0; i < m; ++i) {
        const double t = static_cast<double>(i - (m - 1));
        const double y = history.at(i);
        sx += t;
        sy += y;
        sxx += t * t;
        sxy += t * y;
    }

    const double denom = m * sxx - sx * sx;
    double b = 0.0;
    if (std::abs(denom) > LoadForecast::kZeroVarianceEpsilon)
        b = (m * sxy - sx * sy) / denom;
    const double a = (sy - b * sx) / m;  // 最近采样点 t=0 处的拟合水平
    if (level) *level = a;
    if (slope) *slope = b;
}

double wmaLevel(const QVector<double> &history, int m)
{
    const int k = std::min(m, 6);
    double numerator = 0.0;
    for (int j = 1; j <= k; ++j)
        numerator += j * history.at(m - k + j - 1);
    return numerator * 2.0 / (k * (k + 1.0));
}

} // namespace

QString loadTrendText(LoadTrend trend)
{
    switch (trend) {
    case LoadTrend::Rising:
        return QStringLiteral("上升");
    case LoadTrend::Falling:
        return QStringLiteral("下降");
    case LoadTrend::Steady:
        break;
    }
    return QStringLiteral("平稳");
}

LoadForecastResult forecastLoad(const LoadForecastInput &input)
{
    LoadForecastResult result;

    const QVector<double> &history = input.historyKw;
    if (!validateSamples(history, &result.error))
        return result;

    const int horizon = std::max(1, std::min(input.horizonHours,
                                             LoadForecast::kMaxHorizonHours));
    const int m = std::min(static_cast<int>(history.size()), kMaxSamplesPerModel);
    result.samplesUsed = m;

    if (input.model == ForecastModel::WMA) {
        result.modelName = QStringLiteral("加权移动平均(WMA)");
        result.levelKw = wmaLevel(history, m);
        result.slopeKwPerHour = 0.0;
        result.trend = LoadTrend::Steady;
        result.forecastKw.reserve(horizon);
        for (int h = 1; h <= horizon; ++h)
            result.forecastKw.push_back(clampLoad(result.levelKw, input.capacityKw));
    } else {
        result.modelName = QStringLiteral("最小二乘回归(OLS)");
        olsFit(history, m, &result.levelKw, &result.slopeKwPerHour);
        const double tolerance = LoadForecast::kTrendToleranceKwPerHour;
        if (result.slopeKwPerHour > tolerance)
            result.trend = LoadTrend::Rising;
        else if (result.slopeKwPerHour < -tolerance)
            result.trend = LoadTrend::Falling;
        else
            result.trend = LoadTrend::Steady;

        result.forecastKw.reserve(horizon);
        for (int h = 1; h <= horizon; ++h) {
            const double raw = result.levelKw + result.slopeKwPerHour * h;
            result.forecastKw.push_back(clampLoad(raw, input.capacityKw));
        }
    }

    result.peakForecastKw = 0.0;
    result.peakHourOffset = 0;
    for (int i = 0; i < result.forecastKw.size(); ++i) {
        if (i == 0 || result.forecastKw.at(i) > result.peakForecastKw) {
            result.peakForecastKw = result.forecastKw.at(i);
            result.peakHourOffset = i + 1;
        }
    }

    result.ok = true;
    return result;
}

} // namespace cp
