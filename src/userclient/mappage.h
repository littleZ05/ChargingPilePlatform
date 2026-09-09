#ifndef USERCLIENT_MAPPAGE_H
#define USERCLIENT_MAPPAGE_H

#include <QPair>
#include <QVector>
#include <QWidget>

#include "station.h"

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

/** 地图导航页（NO.5，负责人：葛伊诺）。
 *  顶部返回栏 + 静态地图底图区 + 底部路线信息 + 打开腾讯地图。
 *  底图：腾讯「静态图」API（/ws/staticmap/v2）返回 PNG 图片，用 QLabel 显示，
 *        免去 QWebEngineView 依赖；起终点用 markers 标注。
 *  路线：距离/时长来自腾讯 WebService 驾车路线规划 API，
 *        polyline 解码后作为静态图 path 参数，画出真实驾车路线折线。
 */
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
