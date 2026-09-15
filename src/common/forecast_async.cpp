#include "forecast_async.h"
#include <QtConcurrentRun>
#include <QThread>
namespace cp {
QFuture<ForecastCalculation> forecastAsync(LoadForecastInput input)
{
    return QtConcurrent::run([input] {
        return ForecastCalculation{forecastLoad(input),
            reinterpret_cast<quintptr>(QThread::currentThreadId())};
    });
}
}
