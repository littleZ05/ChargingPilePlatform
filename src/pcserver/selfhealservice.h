#ifndef PC_SELFHEALSERVICE_H
#define PC_SELFHEALSERVICE_H
//
// 创新点2 落地版：自愈检查自动服务（服务器端定时任务）
//
// 真实口径（不含任何伪造数据）：
// - 判定依据只来自 pile_power_logs 中“设备实际上报”的功率样本；
// - 连续 cp::SelfHeal::kConsecutiveCount 次低于阈值 → piles.health_level 置 1(预警/需检查)，
//   自动执行一次远程重启（把电桩运行状态复位为闲置），并写入 selfheal_events 留痕；
// - 重启后仍持续低功率 → health_level 升级为 2(故障)，需人工介入；
// - 功率样本恢复正常 → health_level 回到 0(正常)，并记录“预警解除”事件。
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
    int healthLevelOf(int pileId);
    void processPile(int pileId);
    void recordEvent(int pileId, int stationId, const QString &pileCode, int level,
                     double threshold, double realPower, const QString &action);

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
