#ifndef CP_FORECAST_ASYNC_H
#define CP_FORECAST_ASYNC_H
#include "loadforecast.h"
#include <QFuture>
#include <QtGlobal>
namespace cp {
struct ForecastCalculation {
    LoadForecastResult result;
    quintptr threadId = 0;
};
/** Thread-pool computation takes an immutable snapshot, never a SQL connection. */
QFuture<ForecastCalculation> forecastAsync(LoadForecastInput input);
}
#endif
