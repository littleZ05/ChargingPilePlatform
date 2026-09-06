#ifndef USERCLIENT_STATIONPAGE_H
#define USERCLIENT_STATIONPAGE_H

#include <QWidget>
#include "station.h"

class QListWidget;
class QLineEdit;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

/** 附近充电站页（「首页」tab，NO.4/NO.5，负责人：葛伊诺）。
 *  定位栏 + 搜索（腾讯地理编码重新定位）+ 卡片列表（按距离排序）。
 */
class StationPage : public QWidget
{
    Q_OBJECT
public:
    explicit StationPage(QWidget *parent = nullptr);

signals:
    void stationSelected(const Station &station);

private slots:
    void relocate();
    void onGeocodeReply(QNetworkReply *reply);

private:
    void     sortByDistance();
    void     rebuildList();
    QWidget *makeStationCard(const Station &station);

    QListWidget          *m_list = nullptr;
    QLineEdit            *m_searchEdit = nullptr;
    QLabel               *m_locLabel = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    int                   m_geoSeq = 0;
    QVector<Station>      m_stations;
};

#endif // USERCLIENT_STATIONPAGE_H
