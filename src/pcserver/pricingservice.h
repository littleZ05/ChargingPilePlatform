#ifndef PC_PRICINGSERVICE_H
#define PC_PRICINGSERVICE_H
//
// 创新点1 落地版：价格策略自动引擎（服务器端定时任务）
// - 定价依据 = 负荷预测换算出的“未来 1 小时预测空闲率”，统一由 predictIdleRatePercent 提供，
//   「价格策略」页与引擎共用同一口径，界面看到的依据就是引擎实际使用的依据；
// - 空闲率 > cp::Pricing 阈值 → 自动 8 折写 marketing_strategy；低于则恢复基础价；
// - 每次决策把 predicted_idle_rate / decided_at 落库，决策可追溯、可展示。
//
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <functional>

namespace pcserver {

class StationStore;
class PricingService;

/**
 * 预测空闲率(%)：取该站额定容量与近 12 小时负荷采样，调用 cp::forecastLoad 预测未来 1 小时
 * 负荷，换算为 (1 - 预测负荷/额定容量) × 100。容量或样本不可用时返回 -1，表示“依据不可用”。
 */
double predictIdleRatePercent(StationStore &store, int stationId);

/** 当前活动引擎实例（由 main.cpp 启动时注册）；「价格策略」页“立即重算”按钮用它触发即时决策 */
PricingService *&activePricingService();

class PricingService : public QObject
{
    Q_OBJECT
public:
    explicit PricingService(const QString &dbPath, QObject *parent = nullptr);
    ~PricingService() override;
    bool start(int intervalMs = 30000, QString *err = nullptr);
    void stop();
    void setIdleRateProvider(std::function<double(int)> provider);
    void runOnce();
signals:
    void message(const QString &text);
private:
    double currentIdleRate(int stationId);
    void applyStrategy(int stationId, double rate);
    QSqlDatabase m_db;
    QTimer m_timer;
    QString m_conn;
    std::function<double(int)> m_provider;
};
}
#endif
