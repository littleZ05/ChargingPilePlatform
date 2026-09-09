#ifndef PCSERVER_STATIONSTORE_H
#define PCSERVER_STATIONSTORE_H

#include <QSqlDatabase>
#include <QString>
#include <QVector>

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

    /** 电站为空时写入 3 个演示电站及其模拟电桩，方便界面演示/自测 */
    bool seedDemoIfEmpty(QString *error = nullptr);

    QVector<StationInfo> listStations();
    QVector<PileInfo>    listPiles(int stationId);

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
