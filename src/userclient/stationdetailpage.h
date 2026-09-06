#ifndef USERCLIENT_STATIONDETAILPAGE_H
#define USERCLIENT_STATIONDETAILPAGE_H

#include <QWidget>
#include "station.h"

class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QVBoxLayout;

/** 充电站详情页（「充电」tab，NO.7，负责人：葛伊诺）。
 *  信息卡 + 导航 + 站内电桩 + 费用预估（创新点1）+ 充电模拟。
 */
class StationDetailPage : public QWidget
{
    Q_OBJECT
public:
    explicit StationDetailPage(QWidget *parent = nullptr);
    void setStation(const Station &station);

signals:
    void navigateRequested(const Station &station);

private slots:
    void startCharging(const Pile &pile);
    void endCharging();
    void onTick();
    void updateEstimate();

private:
    double   defaultPower() const;
    void     resetChargingView();
    void     rebuildPiles();
    QWidget *makePileCard(const Pile &pile);

    Station m_station;
    bool    m_hasStation = false;
    bool    m_charging   = false;
    int     m_elapsedSec = 0;
    double  m_kwh        = 0.0;
    QString m_activePile;
    double  m_activePower = 0.0;

    QVBoxLayout *m_pileLayout   = nullptr;
    QLabel      *m_nameLabel    = nullptr;
    QLabel      *m_addrLabel    = nullptr;
    QLabel      *m_priceLabel   = nullptr;
    QLabel      *m_onlineLabel  = nullptr;
    QLabel      *m_pileTitle    = nullptr;
    QLabel      *m_chargingPile = nullptr;
    QLabel      *m_timeLabel    = nullptr;
    QLabel      *m_kwhLabel     = nullptr;
    QLabel      *m_costLabel    = nullptr;
    QPushButton *m_endBtn       = nullptr;
    QSlider     *m_estSlider    = nullptr;
    QLabel      *m_estKwhLabel  = nullptr;
    QLabel      *m_estResult    = nullptr;
    QLabel      *m_estNote      = nullptr;
    QTimer      *m_timer        = nullptr;
};

#endif // USERCLIENT_STATIONDETAILPAGE_H
