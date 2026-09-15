#ifndef PC_SELFHEALSERVICE_H
#define PC_SELFHEALSERVICE_H
//
// 创新点2 落地版：自愈检查自动服务（服务器端定时任务）
//
// 功率样本来自充电结算或显式演示注入；这是模拟运维，不向物理设备发送指令。
// 持久化样本游标避免重复消费；升级故障需要预警之后的三个新异常样本。
// 状态、游标与事件原子提交；活动订单保持占用，健康等级控制是否可新开单。
// 新正常样本可解除自愈状态。
//
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>

namespace pcserver {

class SelfHealService;

/** 当前活动自愈服务实例（main.cpp 注册）；「自愈告警」页注入样本后用它即时跑一次检查 */
SelfHealService *&activeSelfHealService();

/** 电桩健康级别中文（界面/日志共用，与 piles.health_level 对应） */
QString healLevelText(int level);

class SelfHealService : public QObject
{
    Q_OBJECT
public:
    explicit SelfHealService(const QString &dbPath, QObject *parent = nullptr);
    ~SelfHealService() override;

    bool start(int intervalMs = 10000, QString *err = nullptr);
    void stop();

    /** 立即扫描全部电桩（界面“立即检查”与定时器共用同一入口） */
    void runOnce();
    bool rebuildThreshold(int pileId, QString *error = nullptr);

    /** 最近一次扫描统计（供「自愈告警」页 KPI 展示） */
    int lastScanned() const { return m_lastScanned; }
    int lastWarning() const { return m_lastWarning; }
    int lastFault() const { return m_lastFault; }
    int lastRecovered() const { return m_lastRecovered; }
    QString lastRunAt() const { return m_lastRunAt; }

signals:
    void message(const QString &text);
    /** 产生一条已落库的自愈事件：pileCode/level(0恢复,1预警,2故障)/action */
    void selfHealEvent(const QString &pileCode, int level, const QString &action);

private:
    double thresholdOf(int pileId);
    void processPile(int pileId);


    QSqlDatabase m_db;
    QTimer m_timer;
    QString m_conn;

    int m_lastScanned = 0;
    int m_lastWarning = 0;
    int m_lastFault = 0;
    int m_lastRecovered = 0;
    QString m_lastRunAt;
};

} // namespace pcserver

#endif // PC_SELFHEALSERVICE_H
