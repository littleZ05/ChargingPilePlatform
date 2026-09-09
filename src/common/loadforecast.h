#ifndef CHARGINGPILE_LOADFORECAST_H
#define CHARGINGPILE_LOADFORECAST_H
//
// NO.17 充电负荷智能预测（简化时序模型）
// - 仅依赖 QtCore，不引入外部 ML/heavy 依赖；
// - 支持最小二乘线性回归（OLS，默认）与加权移动平均（WMA）两种轻量模型；
// - 输入：近 12/24 小时逐小时采样负荷（kW，旧 → 新）；
// - 输出：未来 1~6 小时负荷预测 + 趋势 / 峰值；
// - 全程值类型 + RAII，无裸指针；对不足样本、非法值、除零均有防护。
//
#include <QString>
#include <QVector>

namespace cp {

/** 负荷预测可选模型 */
enum class ForecastModel {
    OLS = 0,  // 最小二乘线性回归（趋势外推，默认）
    WMA = 1   // 加权移动平均（水平延续）
};

/** 预测趋势方向 */
enum class LoadTrend {
    Rising = 0,   // 上升
    Falling = 1,  // 下降
    Steady = 2    // 平稳
};

/** NO.17 模型常量与保护阈值（集中定义，便于答辩讲解与测试） */
namespace LoadForecast {
inline constexpr int    kMinSamples = 3;              // 少于 3 个采样点不做预测
inline constexpr int    kMaxHorizonHours = 6;         // 最多预测未来 6 小时
inline constexpr double kTrendToleranceKwPerHour = 0.5; // |斜率| < 0.5 kW/h 判为平稳
inline constexpr double kZeroVarianceEpsilon = 1e-9;  // 除零保护阈值
}

/** 预测输入：逐小时负荷序列（旧 → 新） */
struct LoadForecastInput {
    QVector<double> historyKw;       // 近 12/24h 采样（单位 kW），空/过短时返回错误
    int horizonHours = 6;            // 未来 1~6 小时，越界自动收敛
    ForecastModel model = ForecastModel::OLS;
    double capacityKw = 0.0;         // 电站额定可用容量；>0 时预测钳制到 [0, capacityKw]
};

/** 预测结果 */
struct LoadForecastResult {
    bool ok = false;
    QString error;                     // ok=false 时的中文原因

    double levelKw = 0.0;              // 当前时刻负荷水平（回归截距 / WMA 加权均值）
    double slopeKwPerHour = 0.0;       // 每小时趋势斜率（WMA 恒为 0）
    LoadTrend trend = LoadTrend::Steady;

    QVector<double> forecastKw;        // 长度 = horizonHours：索引 0 对应未来第 1 小时
    double peakForecastKw = 0.0;       // 预测窗口内峰值
    int peakHourOffset = 0;            // 峰值出现于未来第几小时（1..horizonHours）

    QString modelName;                 // 实际采用的模型中文名
    int samplesUsed = 0;               // 实际使用采样点数
};

/**
 * 执行轻量时序预测。
 * - OLS：最小二乘拟合最近 m 个点（t 归一到最近点 t=0），
 *        未来第 h 小时 ŷ_h = a + b*h；
 * - WMA：取最近 min(m,6) 个点做线性加权平均作为水平，预测值水平延续；
 * - 统一防护：非法/负数/不足样本返回错误；斜率分母≈0 时按 0 处理；
 * - 预测值钳制 ≥0，capacityKw>0 时再钳制 ≤capacityKw。
 */
LoadForecastResult forecastLoad(const LoadForecastInput &input);

/** 趋势方向中文描述（界面/日志共用） */
QString loadTrendText(LoadTrend trend);

} // namespace cp

#endif // CHARGINGPILE_LOADFORECAST_H
