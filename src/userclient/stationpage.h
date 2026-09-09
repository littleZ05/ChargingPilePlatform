#ifndef USERCLIENT_STATIONPAGE_H
#define USERCLIENT_STATIONPAGE_H

#include <QWidget>
#include "station.h"
#include "pcserver_session.h"

class QListWidget;
class QLineEdit;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

/** 附近充电站页（「首页」tab，NO.4/NO.5，负责人：葛伊诺）。
 *  定位栏 + 搜索（地址解析重新定位，地点搜索兜底）+ 卡片列表（按距离排序）。
 */
class StationPage : public QWidget
{
    Q_OBJECT
public:
    explicit StationPage(QWidget *parent = nullptr);
    /** NO.4 联调：用服务器电站列表替换（为空/失败时回退腾讯 POI 逻辑） */
    void applyServerStations(const QVector<userclient::ServerStation> &stations);

signals:
    void stationSelected(const Station &station);

private slots:
    void relocate();
    void onGeocodeReply(QNetworkReply *reply);
    void onRelocatePlaceReply(QNetworkReply *reply);
    void onPlaceSearchReply(QNetworkReply *reply);
    void onIpLocationReply(QNetworkReply *reply);

private:
    void     sortByDistance();
    void     rebuildList();
    void     searchNearbyStations();
    void     locateByIp();
    void     relocateByPlaceSearch(const QString &addr);
    QWidget *makeStationCard(const Station &station);

    QListWidget          *m_list = nullptr;
    QLineEdit            *m_searchEdit = nullptr;
    QLabel               *m_locLabel = nullptr;
    QLabel               *m_listTitle = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    int                   m_geoSeq = 0;
    int                   m_placeSeq = 0;
    int                   m_ipSeq = 0;
    QVector<Station>      m_stations;
};

#endif // USERCLIENT_STATIONPAGE_H
