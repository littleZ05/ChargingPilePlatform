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

/** 金额格式化辅助（保留两位小数） */
inline QString money(double v)
{
    return QString::number(v, 'f', 2);
}

} // namespace cp

#endif // CHARGINGPILE_COMMON_H
