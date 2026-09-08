#ifndef PC_PRICINGSERVICE_H
#define PC_PRICINGSERVICE_H
//
// 创新点1 落地版：价格策略自动引擎（服务器端定时任务）
// - 数据源默认“简化预测=电站当前空闲率”，可注入 ML provider 替换（吴羽桐 NO.17）
// - 空闲率 > cp::Pricing 阈值 → 自动 8 折写 marketing_strategy；低于则恢复基础价
//
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <functional>

namespace pcserver {
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
