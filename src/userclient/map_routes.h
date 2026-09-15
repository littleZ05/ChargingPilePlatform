#ifndef USERCLIENT_MAP_ROUTES_H
#define USERCLIENT_MAP_ROUTES_H
#include <QJsonArray>
#include <QPair>
#include <QUrl>
#include <QVector>
#include <QString>
namespace userclient {
enum class TravelMode { Driving, Walking };
bool validCoordinates(double latitude, double longitude);
QUrl routePlanUrl(double fromLat, double fromLng, const QString &fromName,
                  double toLat, double toLng, const QString &toName, TravelMode mode);
QVector<QPair<double,double>> decodeTencentPolyline(const QJsonArray &values);
}
#endif
