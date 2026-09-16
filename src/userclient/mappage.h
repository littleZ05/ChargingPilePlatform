#ifndef USERCLIENT_MAPPAGE_H
#define USERCLIENT_MAPPAGE_H

#include <QPair>
#include <QVector>
#include <QWidget>

#include "station.h"
#include "map_routes.h"

class QLabel;
class QWebEngineView;
class QNetworkAccessManager;
class QNetworkReply;

/** 内嵌腾讯路线规划，支持驾车；静态图用于预览和加载失败兜底。 */
class MapPage : public QWidget
{
    Q_OBJECT
public:
    explicit MapPage(QWidget *parent = nullptr);
    void setRoute(const Station &station);

signals:
    void backRequested();

private slots:
    void openMap();
    void onRouteReply(QNetworkReply *reply);
    void onStaticMapReply(QNetworkReply *reply);

private:
    void     fetchRoute();
    void     loadStaticMap();
    QString  staticMapUrl() const;
    int      autoZoom(double distanceKm) const;
    void     showFallbackInfo();

    QUrl navigationUrl() const;
    QWebEngineView *m_web = nullptr;
    Station m_station;
    int     m_seq    = 0;  // 路线请求序号（丢弃过期响应）
    int     m_mapSeq = 0;  // 静态图请求序号（丢弃过期响应）
    QVector<QPair<double, double>> m_routePath; // 解码后的路线坐标（纬度, 经度）

    QLabel *m_routeLabel = nullptr;
    QLabel *m_mapLabel   = nullptr;
    QLabel *m_infoLabel  = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};

#endif // USERCLIENT_MAPPAGE_H
