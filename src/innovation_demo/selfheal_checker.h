#ifndef ID_SELFHEAL_CHECKER_H
#define ID_SELFHEAL_CHECKER_H

#include "../common/common.h"

/**
 * 创新点2 自愈检查状态机（纯逻辑，便于单测）
 */
namespace SelfHealLogic {
struct State { int streak = 0; };
inline bool isLow(double powerKw, double lowThreshold)
{
    return powerKw < lowThreshold;
}
inline void step(State &s, bool low)
{
    s.streak = low ? s.streak + 1 : 0;
}
inline bool shouldAlert(const State &s)
{
    return s.streak >= cp::SelfHeal::kConsecutiveCount; // 连续 3 次
}
}

#endif
