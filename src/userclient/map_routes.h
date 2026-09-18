#ifndef USERCLIENT_MAP_ROUTES_H
#define USERCLIENT_MAP_ROUTES_H
#include <QJsonArray>
#include <QPair>
#include <QUrl>
#include <QVector>
#include <QString>
namespace userclient {
bool validCoordinates(double latitude, double longitude);
// 路线规划仅保留驾车（2026-09-18 产品决定，移除步行模式）
QUrl routePlanUrl(double fromLat, double fromLng, const QString &fromName,
                  double toLat, double toLng, const QString &toName);
QVector<QPair<double,double>> decodeTencentPolyline(const QJsonArray &values);
}
#endif
