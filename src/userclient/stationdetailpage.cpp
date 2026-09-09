#include "stationdetailpage.h"
#include "pcserver_session.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QMessageBox>
#include <QScrollArea>
#include <QDateTime>

namespace {

QString formatDuration(int sec)
{
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    const int s = sec % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

StationDetailPage::StationDetailPage(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(12);
    scroll->setWidget(content);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto *title = new QLabel(QStringLiteral("充电站详情"), content);
    title->setObjectName(QStringLiteral("pageTitle"));
    v->addWidget(title);

    // ---- 电站信息卡 ----
    auto *infoCard = new QFrame(content);
    infoCard->setObjectName(QStringLiteral("card"));
    auto *iv = new QVBoxLayout(infoCard);
    iv->setContentsMargins(16, 16, 16, 16);
    iv->setSpacing(6);

    m_nameLabel = new QLabel(QStringLiteral("请先在「首页」选择充电站"), infoCard);
    m_nameLabel->setStyleSheet(QStringLiteral("color:#ffffff; font-size:17px; font-weight:bold;"));
    iv->addWidget(m_nameLabel);

    m_addrLabel = new QLabel(infoCard);
    m_addrLabel->setObjectName(QStringLiteral("hintText"));
    m_addrLabel->setWordWrap(true);
    iv->addWidget(m_addrLabel);

    auto *priceRow = new QHBoxLayout;
    m_priceLabel = new QLabel(infoCard);
    m_priceLabel->setTextFormat(Qt::RichText);
    m_onlineLabel = new QLabel(infoCard);
    m_onlineLabel->setObjectName(QStringLiteral("hintText"));
    priceRow->addWidget(m_priceLabel);
    priceRow->addStretch();
    priceRow->addWidget(m_onlineLabel);
    iv->addLayout(priceRow);
    v->addWidget(infoCard);

    // ---- 导航按钮 ----
    auto *navBtn = new QPushButton(QStringLiteral("导航"), content);
    navBtn->setObjectName(QStringLiteral("primaryButton"));
    navBtn->setFixedHeight(40);
    v->addWidget(navBtn);

    // ---- 费用预估卡（创新点1） ----
    auto *estCard = new QFrame(content);
    estCard->setObjectName(QStringLiteral("card"));
    auto *ev = new QVBoxLayout(estCard);
    ev->setContentsMargins(16, 14, 16, 14);
    ev->setSpacing(8);

    auto *estTitle = new QLabel(QStringLiteral("费用预估"), estCard);
    estTitle->setStyleSheet(QStringLiteral("color:#ffffff; font-size:14px; font-weight:bold;"));

    auto *sliderRow = new QHBoxLayout;
    m_estSlider = new QSlider(Qt::Horizontal, estCard);
    m_estSlider->setRange(5, 60);
    m_estSlider->setValue(30);
    m_estSlider->setSingleStep(5);
    m_estSlider->setPageStep(10);
    m_estKwhLabel = new QLabel(QStringLiteral("30 kWh"), estCard);
    m_estKwhLabel->setStyleSheet(QStringLiteral("color:#0ea5e9; font-size:14px; font-weight:bold;"));
    m_estKwhLabel->setMinimumWidth(64);
    m_estKwhLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sliderRow->addWidget(m_estSlider, 1);
    sliderRow->addWidget(m_estKwhLabel);

    m_estResult = new QLabel(QStringLiteral("预计费用 ¥-- · 约 -- 小时"), estCard);
    m_estResult->setStyleSheet(QStringLiteral("color:#a5b4cf; font-size:13px;"));
    m_estNote = new QLabel(estCard);
    m_estNote->setObjectName(QStringLiteral("hintText"));

    ev->addWidget(estTitle);
    ev->addLayout(sliderRow);
    ev->addWidget(m_estResult);
    ev->addWidget(m_estNote);
    v->addWidget(estCard);

    // ---- 站内电桩 ----
    m_pileTitle = new QLabel(QStringLiteral("站内电桩"), content);
    m_pileTitle->setStyleSheet(QStringLiteral("color:#ffffff; font-size:15px; font-weight:bold;"));
    v->addWidget(m_pileTitle);

    m_pileLayout = new QVBoxLayout;
    m_pileLayout->setSpacing(8);
    v->addLayout(m_pileLayout);

    // ---- 充电卡 ----
    auto *chargeCard = new QFrame(content);
    chargeCard->setObjectName(QStringLiteral("card"));
    auto *cv = new QVBoxLayout(chargeCard);
    cv->setContentsMargins(16, 16, 16, 16);
    cv->setSpacing(8);

    m_chargingPile = new QLabel(QStringLiteral("当前电桩：未选择"), chargeCard);
    m_chargingPile->setStyleSheet(QStringLiteral("color:#a5b4cf; font-size:13px;"));
    m_chargingPile->setAlignment(Qt::AlignCenter);

    m_timeLabel = new QLabel(QStringLiteral("00:00:00"), chargeCard);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setStyleSheet(QStringLiteral("color:#ffffff; font-size:32px; font-weight:bold;"));

    m_kwhLabel = new QLabel(QStringLiteral("已充 0.00 kWh"), chargeCard);
    m_kwhLabel->setAlignment(Qt::AlignCenter);
    m_kwhLabel->setObjectName(QStringLiteral("hintText"));

    m_costLabel = new QLabel(QStringLiteral("¥ 0.00"), chargeCard);
    m_costLabel->setAlignment(Qt::AlignCenter);
    m_costLabel->setStyleSheet(QStringLiteral("color:#22c55e; font-size:20px; font-weight:bold;"));

    m_endBtn = new QPushButton(QStringLiteral("结束充电"), chargeCard);
    m_endBtn->setObjectName(QStringLiteral("primaryButton"));
    m_endBtn->setEnabled(false);
    connect(m_endBtn, &QPushButton::clicked, this, &StationDetailPage::endCharging);

    cv->addWidget(m_chargingPile);
    cv->addWidget(m_timeLabel);
    cv->addWidget(m_kwhLabel);
    cv->addWidget(m_costLabel);
    cv->addWidget(m_endBtn);
    v->addWidget(chargeCard);

    v->addStretch();

    // ---- 连接 ----
    connect(navBtn, &QPushButton::clicked, this, [this] {
        if (m_hasStation) emit navigateRequested(m_station);
    });
    connect(m_estSlider, &QSlider::valueChanged, this, &StationDetailPage::updateEstimate);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &StationDetailPage::onTick);

    m_estSlider->setEnabled(false);
}

void StationDetailPage::setStation(const Station &station)
{
    m_station = station;
    m_hasStation = true;
    m_charging = false;
    m_timer->stop();
    m_elapsedSec = 0;
    m_kwh = 0.0;
    m_activePile.clear();
    m_activePower = 0.0;

    m_nameLabel->setText(station.name);
    m_addrLabel->setText(station.address);

    if (isOnSale(station)) {
        m_priceLabel->setText(QStringLiteral(
            "<span style='color:#22c55e;font-weight:bold;'>¥%1/度</span> "
            "<s style='color:#6b7a99;'>¥%2</s>")
            .arg(QString::number(effectivePrice(station), 'f', 2))
            .arg(QString::number(station.price, 'f', 2)));
    } else {
        m_priceLabel->setText(QStringLiteral(
            "<span style='color:#22c55e;font-weight:bold;'>¥%1/度</span>")
            .arg(QString::number(station.price, 'f', 2)));
    }
    m_onlineLabel->setText(QStringLiteral("在线率 %1%").arg(QString::number(station.onlineRate, 'f', 0)));

    m_pileTitle->setText(QStringLiteral("站内电桩 · 共%1桩 | 空闲%2")
                             .arg(station.totalPiles)
                             .arg(station.idlePiles));

    rebuildPiles();

    m_estSlider->setEnabled(true);
    updateEstimate();

    resetChargingView();
    m_endBtn->setEnabled(false);
}

void StationDetailPage::setServerSession(userclient::PcServerSession *session)
{
    if (m_session == session)
        return;
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    if (m_session) {
        connect(m_session, &userclient::PcServerSession::orderReportResult,
                this, &StationDetailPage::onOrderReportResult);
    }
}

void StationDetailPage::rebuildPiles()
{
    while (m_pileLayout->count() > 0) {
        QLayoutItem *item = m_pileLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const Pile &p : m_station.piles) {
        m_pileLayout->addWidget(makePileCard(p));
    }
}

QWidget *StationDetailPage::makePileCard(const Pile &p)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    auto *h = new QHBoxLayout(card);
    h->setContentsMargins(14, 12, 14, 12);
    h->setSpacing(10);

    auto *left = new QVBoxLayout;
    left->setSpacing(2);
    auto *code = new QLabel(p.code, card);
    code->setStyleSheet(QStringLiteral("color:#ffffff; font-size:14px; font-weight:bold;"));
    auto *spec = new QLabel(QStringLiteral("%1 - %2kW").arg(p.type).arg(QString::number(p.powerKw, 'f', 0)), card);
    spec->setObjectName(QStringLiteral("hintText"));
    left->addWidget(code);
    left->addWidget(spec);

    QString stateColor;
    if (p.state == QStringLiteral("空闲"))      stateColor = QStringLiteral("#22c55e");
    else if (p.state == QStringLiteral("故障")) stateColor = QStringLiteral("#ef4444");
    else                                        stateColor = QStringLiteral("#f59e0b");
    auto *state = new QLabel(p.state, card);
    state->setAlignment(Qt::AlignCenter);
    state->setFixedSize(44, 20);
    state->setStyleSheet(QStringLiteral(
        "background-color:%1; color:#0a1025; border-radius:10px; font-size:11px; font-weight:bold;")
        .arg(stateColor));

    const bool idle = (p.state == QStringLiteral("空闲"));
    auto *btn = new QPushButton(idle ? QStringLiteral("预约充电") : QStringLiteral("已占用"), card);
    btn->setObjectName(QStringLiteral("ghostButton"));
    btn->setFixedWidth(80);
    btn->setEnabled(idle);
    if (idle) {
        connect(btn, &QPushButton::clicked, this, [this, p] { startCharging(p); });
    }

    h->addLayout(left, 1);
    h->addWidget(state);
    h->addWidget(btn);
    return card;
}

void StationDetailPage::startCharging(const Pile &pile)
{
    if (!m_hasStation || m_charging) return;
    m_charging = true;
    m_activePile = pile.code;
    m_activePower = pile.powerKw;
    m_elapsedSec = 0;
    m_kwh = 0.0;

    m_chargingPile->setText(QStringLiteral("当前电桩：%1 · %2 %3kW")
                                .arg(pile.code, pile.type)
                                .arg(QString::number(pile.powerKw, 'f', 0)));
    m_timeLabel->setText(QStringLiteral("00:00:00"));
    m_kwhLabel->setText(QStringLiteral("已充 0.00 kWh"));
    m_costLabel->setText(QStringLiteral("¥ 0.00"));
    m_endBtn->setEnabled(true);
    m_timer->start();
}

void StationDetailPage::endCharging()
{
    if (!m_charging) return;
    m_timer->stop();
    m_charging = false;

    const double cost = m_kwh * effectivePrice(m_station);
    const QString pileCode = m_activePile;
    const QString dur = formatDuration(m_elapsedSec);
    const QString kwhText = QString::number(m_kwh, 'f', 2);
    const QString costText = QString::number(cost, 'f', 2);

    // NO.7 闭环：生成订单并追加到「我的」页订单列表（无论是否连上服务器）
    const QString orderNo = QStringLiteral("NO%1").arg(QDateTime::currentSecsSinceEpoch());
    Order order;
    order.orderNo  = orderNo;
    order.pileCode = pileCode;
    order.time     = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    order.kwh      = m_kwh;
    order.amount   = cost;
    order.state    = QStringLiteral("已结算");
    emit chargeCompleted(order);

    resetChargingView();
    m_endBtn->setEnabled(false);

    // NO.7 结算触发点：向服务器上报一次订单（结算落库），结果经 onOrderReportResult 回显
    bool reported = false;
    if (m_session && m_session->isConnected()) {
        m_pendingOrderNo = orderNo;
        reported = m_session->reportOrder(m_pendingOrderNo, pileCode, m_kwh, cost);
    }

    QString summary = QStringLiteral("电桩：%1\n时长：%2\n电量：%3 kWh\n费用：¥%4")
                          .arg(pileCode, dur, kwhText, costText);
    summary += reported
        ? QStringLiteral("\n\n结算已上报服务器，等待确认…")
        : QStringLiteral("\n\n（未连接服务器，仅本地模拟结算）");

    QMessageBox::information(this, QStringLiteral("充电完成"), summary);
}

void StationDetailPage::onOrderReportResult(int code, const QString &message,
                                            const QString &orderNo,
                                            const QString &pileCode,
                                            bool received, bool pileFreed)
{
    const QString no = orderNo.isEmpty() ? m_pendingOrderNo : orderNo;
    if (code == 0 && received) {
        QString text = QStringLiteral("结算成功\n订单号：%1\n电桩：%2")
                           .arg(no, pileCode);
        if (pileFreed)
            text += QStringLiteral("\n电桩已由「充电中」释放为「闲置」");
        QMessageBox::information(this, QStringLiteral("服务器结算结果"), text);
    } else {
        QMessageBox::warning(this, QStringLiteral("服务器结算结果"),
                             QStringLiteral("结算失败（code=%1）\n%2\n订单号：%3")
                                 .arg(code).arg(message, no));
    }
}

void StationDetailPage::onTick()
{
    ++m_elapsedSec;
    m_kwh += m_activePower / 3600.0;
    const double cost = m_kwh * effectivePrice(m_station);
    m_timeLabel->setText(formatDuration(m_elapsedSec));
    m_kwhLabel->setText(QStringLiteral("已充 %1 kWh").arg(QString::number(m_kwh, 'f', 2)));
    m_costLabel->setText(QStringLiteral("¥ %1").arg(QString::number(cost, 'f', 2)));
}

void StationDetailPage::updateEstimate()
{
    if (!m_hasStation) return;
    const double targetKwh = m_estSlider->value();
    const double price = effectivePrice(m_station);
    const double cost = targetKwh * price;
    const double hours = targetKwh / defaultPower();

    m_estKwhLabel->setText(QStringLiteral("%1 kWh").arg(targetKwh));
    m_estResult->setText(QStringLiteral("预计费用 ¥%1 · 约 %2 小时")
                             .arg(QString::number(cost, 'f', 2))
                             .arg(QString::number(hours, 'f', 1)));

    if (isOnSale(m_station)) {
        m_estNote->setText(QStringLiteral("%1 %2kW · ¥%3/度（闲时8折，原价¥%4）")
                               .arg(m_station.type)
                               .arg(defaultPower())
                               .arg(QString::number(price, 'f', 2))
                               .arg(QString::number(m_station.price, 'f', 2)));
    } else {
        m_estNote->setText(QStringLiteral("%1 %2kW · ¥%3/度")
                               .arg(m_station.type)
                               .arg(defaultPower())
                               .arg(QString::number(m_station.price, 'f', 2)));
    }
}

double StationDetailPage::defaultPower() const
{
    return (m_station.type == QStringLiteral("慢充")) ? 7.0 : 120.0;
}

void StationDetailPage::resetChargingView()
{
    m_chargingPile->setText(QStringLiteral("当前电桩：未选择"));
    m_timeLabel->setText(QStringLiteral("00:00:00"));
    m_kwhLabel->setText(QStringLiteral("已充 0.00 kWh"));
    m_costLabel->setText(QStringLiteral("¥ 0.00"));
}
