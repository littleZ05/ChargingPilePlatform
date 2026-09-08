#ifndef PC_SELFHEALSERVICE_H
#define PC_SELFHEALSERVICE_H
//
// 创新点2 落地版：自愈检查自动服务（服务器端定时任务）
// - 周期扫描每桩最近功率日志：连续 cp::SelfHeal 次低于阈值 → 需检查并自动模拟远程重启
// - 阈值优先取 pile_health_metrics，缺失用功率日志均值*80% 兜底
//
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>

namespace pcserver {
class SelfHealService : public QObject
{
    Q_OBJECT
public:
    explicit SelfHealService(const QString &dbPath, QObject *parent = nullptr);
    ~SelfHealService() override;
    bool start(int intervalMs = 10000, QString *err = nullptr);
    void stop();
    void runOnce();
signals:
    void message(const QString &text);
private:
    double thresholdOf(int pileId);
    void processPile(int pileId);
    QSqlDatabase m_db;
    QTimer m_timer;
    QString m_conn;
};
}
#endif
