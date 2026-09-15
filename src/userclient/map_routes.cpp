#include "map_routes.h"
#include <QUrlQuery>
#include <cmath>
namespace userclient {
bool validCoordinates(double latitude, double longitude)
{
    return std::isfinite(latitude) && std::isfinite(longitude)
           && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}
QUrl routePlanUrl(double fromLat, double fromLng, const QString &fromName,
                  double toLat, double toLng, const QString &toName, TravelMode mode)
{
    if (!validCoordinates(fromLat,fromLng) || !validCoordinates(toLat,toLng))
        return {};
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    QUrlQuery query;
    query.addQueryItem("type",mode == TravelMode::Walking ? "walk" : "drive");
    query.addQueryItem("from",fromName);
    query.addQueryItem("fromcoord",QStringLiteral("%1,%2").arg(fromLat,0,'f',6).arg(fromLng,0,'f',6));
    query.addQueryItem("to",toName);
    query.addQueryItem("tocoord",QStringLiteral("%1,%2").arg(toLat,0,'f',6).arg(toLng,0,'f',6));
    query.addQueryItem("referer","ChargingPilePlatform");
    url.setQuery(query);
    return url;
}
QVector<QPair<double,double>> decodeTencentPolyline(const QJsonArray &values)
{
    QVector<QPair<double,double>> result;
    if (values.size()<2 || values.size()%2) return result;
    // Tencent direction API: first coordinate in degrees, following deltas / 1e6.
    double lat=0,lng=0;
    for(int i=0;i<values.size();i+=2) {
        if(!values[i].isDouble() || !values[i+1].isDouble()) return {};
        if(i==0) { lat=values[i].toDouble(); lng=values[i+1].toDouble(); }
        else { lat+=values[i].toDouble()/1e6; lng+=values[i+1].toDouble()/1e6; }
        if(!validCoordinates(lat,lng)) return {};
        result.append({lat,lng});
    }
    return result;
}
}
