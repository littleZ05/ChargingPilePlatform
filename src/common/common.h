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

/**
 * 业务消息类型（Socket 帧 type 字段，NO.15 维护人：陈庚泉）
 * 约定：0-9 心跳/业务上报；10-19 账号类请求；20-29 管理类请求；
 *       100-199 对应响应；499 统一错误响应。值一经发布保持稳定，只增不改。
 */
namespace MsgType {
inline constexpr int kHeartbeat       = 0;   // 心跳（客户端→服务端）
inline constexpr int kStationQuery    = 1;   // 附近电站查询
inline constexpr int kOrderReport     = 2;   // 充电订单上报
inline constexpr int kPileStateReport = 3;   // 电桩实时状态上报
inline constexpr int kStartCharge     = 4;   // 开始充电
inline constexpr int kStopCharge      = 5;   // 停止充电
inline constexpr int kProfileQuery    = 6;   // 用户资料查询
inline constexpr int kProfileUpdate   = 7;   // 用户资料修改

inline constexpr int kLoginRequest    = 10;  // 管理员/用户登录
inline constexpr int kRegisterRequest = 11;  // 用户注册

inline constexpr int kRechargeRequest = 30;  // NO.6 用户余额充值（真实落库，模拟支付成功）

inline constexpr int kPileManageRequest  = 20; // 充电桩管理（服务端）
inline constexpr int kSalesQueryRequest  = 21; // 销售业绩查询
inline constexpr int kUserManageRequest  = 22; // 用户管理
inline constexpr int kStationManageRequest = 23; // 充电站管理
inline constexpr int kAdminQueryRequest  = 24; // 管理员/权限查询

inline constexpr int kHeartbeatResponse       = 100;
inline constexpr int kStationQueryResponse    = 101;
inline constexpr int kOrderReportResponse     = 102;
inline constexpr int kPileStateReportResponse = 103;
inline constexpr int kStartChargeResponse     = 104;
inline constexpr int kStopChargeResponse      = 105;
inline constexpr int kProfileQueryResponse    = 106;
inline constexpr int kProfileUpdateResponse   = 107;
inline constexpr int kLoginResponse           = 110;
inline constexpr int kRegisterResponse        = 111;
inline constexpr int kRechargeResponse        = 130;
inline constexpr int kPileManageResponse      = 120;
inline constexpr int kSalesQueryResponse      = 121;
inline constexpr int kUserManageResponse      = 122;
inline constexpr int kStationManageResponse   = 123;
inline constexpr int kAdminQueryResponse      = 124;

inline constexpr int kErrorResponse = 499;   // 统一错误响应帧类型
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
