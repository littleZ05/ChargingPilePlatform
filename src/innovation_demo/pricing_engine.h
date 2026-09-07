#ifndef ID_PRICING_ENGINE_H
#define ID_PRICING_ENGINE_H

#include <QtGlobal>
#include "../common/common.h"

/**
 * 创新点1 价格策略判定（纯逻辑，便于单测；实际写库在 DemoStore）
 */
namespace PricingEngine {
inline bool shouldDiscount(double idleRatePercent)
{
    return idleRatePercent > cp::Pricing::kIdleRateThreshold * 100.0; // >60%
}
inline double discountRate() { return cp::Pricing::kDiscount; }       // 8 折
}

#endif
