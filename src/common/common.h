#ifndef CHARGINGPILE_COMMON_H
#define CHARGINGPILE_COMMON_H

/**
 * common：用户端与服务器端共享的常量 / 枚举 / 公共定义。
 * 注意：本文件属于公共契约，改动需小组评审后合入 main。
 */
#include <QString>

namespace cp {

/** Socket 通信默认端口（服务器端监听 / 用户端连接） */
inline constexpr int kServerPort = 9999;

/** 电桩状态（与 database/schema.sql 中 piles.state 对应） */
enum class PileState {
    Idle = 0,      // 闲置
    Charging = 1,  // 充电中
    Fault = 2      // 故障
};

/** 充电订单状态 */
enum class OrderState {
    Charging = 0,  // 充电中（未结算）
    Finished = 1,  // 已完成
    Canceled = 2   // 已取消
};

/** 用户端请求类型（Socket 协议帧 type 字段，按需扩展） */
namespace MsgType {
inline constexpr int kHeartbeat  = 0;   // 心跳
inline constexpr int kStationQuery = 1; // 附近电站查询
inline constexpr int kOrderReport = 2;  // 充电订单上报
}

/**
 * 创新点1：闲时“反向激励”动态计费规则参数（集中定义，便于运营调整）
 * 维护人：张芮萌（feat/littlez05）
 */
namespace Pricing {
inline constexpr double kIdleRateThreshold = 0.60;  // 预测空闲率 > 60% 触发打折
inline constexpr double kDiscount = 0.80;            // 折扣率：8 折
inline constexpr int    kPredictHours = 1;           // 预测未来 1 小时
}

/**
 * 创新点2：异常检测“自愈”告警参数
 * 维护人：张芮萌（feat/littlez05）
 */
namespace SelfHeal {
inline constexpr double kLowPowerRatio = 0.20;   // 功率低于历史均值 20% 判为异常
inline constexpr int    kConsecutiveCount = 3;   // 连续 3 次触发“需检查”
inline constexpr int    kCheckIntervalSec = 60;  // 自愈检查定时器周期（秒，可调）
}

/** 自愈检查状态分级（与服务器端定时任务返回值对应） */
enum class HealLevel {
    Normal = 0,   // 正常
    Warning = 1,  // 预警（需检查）
    Fault = 2     // 故障（自动重启失败后置为故障）
};

/** 电桩状态 -> 中文显示（界面/日志通用） */
inline QString pileStateText(PileState s)
{
    switch (s) {
    case PileState::Idle:     return QStringLiteral("闲置");
    case PileState::Charging: return QStringLiteral("充电中");
    case PileState::Fault:    return QStringLiteral("故障");
    }
    return QStringLiteral("未知");
}

/** 订单状态 -> 中文显示 */
inline QString orderStateText(OrderState s)
{
    switch (s) {
    case OrderState::Charging: return QStringLiteral("充电中");
    case OrderState::Finished: return QStringLiteral("已完成");
    case OrderState::Canceled: return QStringLiteral("已取消");
    }
    return QStringLiteral("未知");
}

/** 金额格式化辅助（保留两位小数） */
inline QString money(double v)
{
    return QString::number(v, 'f', 2);
}

} // namespace cp

#endif // CHARGINGPILE_COMMON_H
