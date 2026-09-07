#ifndef USERCLIENT_MAPPAGE_H
#define USERCLIENT_MAPPAGE_H

#include <QWidget>
#include "station.h"

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

/** 地图导航页（NO.5，负责人：葛伊诺）。
 *  顶部返回栏 + 地图底图占位区 + 底部路线信息 + 打开腾讯地图。
 *  路线距离/时长来自腾讯 WebService 驾车路线规划 API（key 见 mappage.cpp 顶部）。
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

private:
    void fetchRoute();
    void showFallbackInfo();

    Station m_station;
    int     m_seq = 0;
    QLabel *m_routeLabel = nullptr;
    QLabel *m_infoLabel  = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};

#endif // USERCLIENT_MAPPAGE_H
