#ifndef PCSERVER_STATIONSTORE_H
#define PCSERVER_STATIONSTORE_H

#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <functional>

#include "../common/common.h"

namespace pcserver {

/** 充电站列表行（数据访问层返回结构） */
struct StationInfo
{
    int    id = 0;
    QString name;
    QString address;
    double longitude = 0.0;
    double latitude  = 0.0;
    int    totalPiles  = 0;   // 实际电桩数（以 piles 表为准）
    int    idlePiles   = 0;   // 空闲电桩数（state = 闲置）
    int    onlinePiles = 0;   // 在线电桩数（state != 故障）
    double onlineRate  = 0.0; // 当前在线率 0~100
    double basePrice   = 0.0; // 基础电价（元/度）
    double currentPrice = 0.0; // 当前执行价（基础价 × 最新生效营销折扣）
    double discount    = 1.0; // 最新生效折扣（1.0=无折扣）
    bool   onSale      = false; // 是否处于“闲时特惠”
};

/** 站内电桩实时状态（数据访问层返回结构） */
struct PileInfo
{
    int          id = 0;
    int          stationId = 0;
    QString      code;        // 电桩编号，如 S001-P01
    QString      type;        // 快充 / 慢充
    double       powerKw = 0.0;
    cp::PileState state = cp::PileState::Idle;
    int          chargeCount = 0;
    qint64       chargeSeconds = 0;
};

/**
 * 充电站数据访问层（SQLite）。
 * 表结构统一来自 src/database/schema.sql（编译为资源），不单独复制建表语句，
 * 以遵守“schema.sql 为公共契约”的约定。
 */
class StationStore
{
public:
    StationStore();
    ~StationStore();

    bool open(const QString &dbPath, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    QString connectionName() const { return m_connectionName; }

    /**
     * 事务执行器：回调返回 true 提交、false 回滚。
     * 数据库管理/批量写入统一经由此入口，避免半写脏数据。
     */
    bool runInTransaction(const std::function<bool(QSqlDatabase &)> &fn,
                          QString *error = nullptr);

    /** SQLite 完整性检查（PRAGMA integrity_check），返回 "ok" 表示通过 */
    bool integrityCheck(QString *report = nullptr);

    /** 备份当前库到 destPath（VACUUM INTO；路径含单引号等危险字符时拒绝） */
    bool backupTo(const QString &destPath, QString *error = nullptr);

    /** 统一 prepare + bindValue 执行入口：业务 SQL 一律参数化，禁止拼接用户输入 */
    bool execPrepared(const QString &sql, const QVariantList &binds,
                      QString *error = nullptr);

    /** 电站为空时写入 3 个演示电站及其模拟电桩，方便界面演示/自测 */
    bool seedDemoIfEmpty(QString *error = nullptr);

    QVector<StationInfo> listStations();
    QVector<PileInfo>    listPiles(int stationId);

    /**
     * NO.17 负荷预测数据源
     * - ratedCapacityKw：电站额定可用容量 = Σ(piles.power_kw)，预测钳制上界；
     * - currentLoadKw：当前实时负荷 = Σ(充电中电桩 power_kw)；
     * - hourlyLoadSamples：返回最近 hours 个整点小时负荷（旧→新，单位 kW）。
     *   数据策略：先真实聚合 pile_power_logs（电站维度按小时求和）；
     *   有效样本不足（< max(3, hours/3)）时回退到确定性仿真采样曲线，
     *   并置 usedDemoFallback=true（UI 上如实标注“演示采样”）。
     */
    double ratedCapacityKw(int stationId, QString *error = nullptr) const;
    double currentLoadKw(int stationId, QString *error = nullptr) const;
    QVector<double> hourlyLoadSamples(int stationId, int hours,
                                      bool *usedDemoFallback = nullptr,
                                      QString *error = nullptr) const;

    /** 按电桩编码精确查找（piles.code 唯一）；未找到返回 false，找到时可选回填 out */
    bool findPileByCode(const QString &code, PileInfo *out = nullptr);

    /**
     * 新增充电站（模拟新增）：写入 stations 一行，
     * 并按 pileCount 批量生成属于该站的模拟电桩（快慢充/初始状态按固定规则分布）。
     * 成功返回新站 id，失败返回 -1 并通过 error 返回原因。
     */
    int addStation(const QString &name,
                   const QString &address,
                   double longitude,
                   double latitude,
                   int pileCount,
                   QString *error = nullptr);

    /** 更新电桩状态并同步刷新所属电站的当前在线率（写入 stations.online_rate） */
    bool setPileState(int pileId, cp::PileState state, QString *error = nullptr);

    /**
     * 结算上报（联调闭环）：完成该桩“充电中”订单并落库。
     * 事务内：订单 state 0->1、写 end_time/kwh/amount、扣减用户余额、
     *         更新桩状态为闲置并累加充电次数/时长；成功后刷新电站在线率。
     */
    bool settleChargingOrderByCode(const QString &pileCode, double kwh, double amount,
                                   int *orderIdOut = nullptr,
                                   double *balanceOut = nullptr,
                                   QString *error = nullptr);

    /** 输入校验：与新增电站共用一套规则，避免界面/数据层校验不一致 */
    static bool validateInput(const QString &name,
                              const QString &address,
                              double longitude,
                              double latitude,
                              int pileCount,
                              QString *error = nullptr);

    static QString pileStateText(cp::PileState state);
    /** 演示用状态迁移：闲置→充电中→闲置、故障→闲置（供“实时”模拟定时器使用） */
    static cp::PileState nextSimulatedState(cp::PileState current);

private:
    bool executeSchema(QString *error);
    bool refreshOnlineRate(int stationId, QString *error = nullptr);

    QSqlDatabase m_db;
    QString      m_connectionName;
};

} // namespace pcserver

#endif // PCSERVER_STATIONSTORE_H
