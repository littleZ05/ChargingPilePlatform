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
    if (m_orderId > 0 || m_phase == Phase::Starting)
        return;
    m_station = station;
    m_hasStation = true;
    m_charging = false;
    m_timer->stop();
    m_elapsedSec = 0;
    m_kwh = 0.0;
    m_activePile.clear();
    m_myPileCode.clear();
    m_activePower = 0.0;
    m_serverUnitPrice = 0.0;

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
        connect(m_session, &userclient::PcServerSession::businessResult, this,
                [this](int type, const QJsonObject &r) {
            if (type == cp::MsgType::kStartCharge && r.value("code").toInt(-1) == 0) {
                // Receipt replay may refer to an old start; query authoritative active order
                // rather than restarting the timer from a stale success receipt.
                m_phase = Phase::Idle;
                m_chargingPile->setText(QStringLiteral("建单已确认，正在恢复订单状态…"));
            }
            if (type == cp::MsgType::kStartCharge && r.value("code").toInt() != 0) {
                m_phase = Phase::Idle;
                m_chargingPile->setText(r.value("message").toString());
                releaseMyPile();   // 服务器拒绝，桩标识回滚为「预约充电」
            }
            if (type != cp::MsgType::kOrderReport && type != cp::MsgType::kStopCharge)
                return;
            if (r.value("code").toInt(-1) != 0) {
                m_phase = Phase::SettlementFailed;
                m_endBtn->setEnabled(true);
                m_endBtn->setText(QStringLiteral("重试结算"));
                m_chargingPile->setText(r.value("message").toString());
                return;
            }
            m_timer->stop();
            m_phase = Phase::Completed;
            m_orderId = 0;
            m_charging = false;
            m_endBtn->setEnabled(false);
            m_endBtn->setText(QStringLiteral("已结算"));
            m_costLabel->setText(QStringLiteral("已结算 ¥%1").arg(r.value("amount").toDouble(), 0, 'f', 2));
            m_chargingPile->setText(QStringLiteral("服务器已确认订单 #%1").arg(r.value("order_id").toInt()));
        });
        connect(m_session, &userclient::PcServerSession::startChargeResult,
                this, &StationDetailPage::onStartChargeResult);
    }
}

void StationDetailPage::setPhone(const QString &phone)
{
    m_phone = phone;
}

void StationDetailPage::applyServerPiles(const QVector<userclient::ServerPile> &piles)
{

    m_station.piles.clear();
    for (const userclient::ServerPile &sp : piles) {
        Pile p;
        p.code    = sp.code;
        p.type    = sp.type;
        p.powerKw = sp.powerKw;
        switch (sp.state) {
        case 0:  p.state = QStringLiteral("空闲"); break;
        case 1:  p.state = QStringLiteral("使用中"); break;
        default: p.state = QStringLiteral("故障"); break;
        }
        m_station.piles.append(p);
    }
    m_station.totalPiles = m_station.piles.size();
    int idle = 0;
    for (const Pile &p : m_station.piles) {
        if (p.state == QStringLiteral("空闲"))
            ++idle;
    }
    m_station.idlePiles = idle;
    if (m_pileTitle) {
        m_pileTitle->setText(QStringLiteral("站内电桩 · 共%1桩 | 空闲%2（服务器实时数据）")
                                 .arg(m_station.totalPiles)
                                 .arg(m_station.idlePiles));
    }
    rebuildPiles();
}

void StationDetailPage::onStartChargeResult(int code, const QString &message,
                                            const QString &pileCode, int orderId,
                                            double unitPrice)
{
    if (code != 0) {
        releaseMyPile();   // 服务器拒绝，桩标识回滚为「预约充电」
        QMessageBox::warning(this, QStringLiteral("开始充电失败"),
                             QStringLiteral("%1\n（错误码 %2）").arg(message).arg(code));
        return;
    }

    m_phase = Phase::Charging;
    m_orderId = orderId;
    m_endBtn->setText(QStringLiteral("结束并结算"));
    m_charging = true;
    m_activePile = pileCode.isEmpty() ? m_pendingPileCode : pileCode;
    m_activePower = m_pendingPower > 0.0 ? m_pendingPower : defaultPower();
    m_serverUnitPrice = unitPrice > 0.0 ? unitPrice : 0.0;
    m_elapsedSec = 0;
    m_kwh = 0.0;

    m_chargingPile->setText(QStringLiteral("当前电桩：%1 · %2kW（服务器已建单 #%3）")
                                .arg(m_activePile)
                                .arg(QString::number(m_activePower, 'f', 0))
                                .arg(orderId));
    m_timeLabel->setText(QStringLiteral("00:00:00"));
    m_kwhLabel->setText(QStringLiteral("已充 0.00 kWh"));
    m_costLabel->setText(QStringLiteral("¥ 0.00"));
    m_endBtn->setEnabled(true);
    m_timer->start();
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

void StationDetailPage::releaseMyPile()
{
    if (m_myPileCode.isEmpty())
        return;
    // 该桩本地状态还原为「空闲」（服务器回执可能已把它标为「使用中」，这里按用户操作即时释放）
    for (Pile &p : m_station.piles) {
        if (p.code == m_myPileCode && p.state != QStringLiteral("故障"))
            p.state = QStringLiteral("空闲");
    }
    m_myPileCode.clear();
    rebuildPiles();
}

QWidget *StationDetailPage::makePileCard(const Pile &p)
{
    const bool mine = (!m_myPileCode.isEmpty() && p.code == m_myPileCode);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    if (mine)
        card->setProperty("mine", true);   // 命中 QSS 高亮边框，区别于他人占用桩
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

    // 状态徽标：我的桩显示「充电中」（亮蓝），与他人「使用中」（琥珀）明显区分
    QString stateText = p.state;
    QString stateColor;
    if (mine) {
        stateText  = QStringLiteral("充电中");
        stateColor = QStringLiteral("#0ea5e9");
    } else if (p.state == QStringLiteral("空闲")) {
        stateColor = QStringLiteral("#22c55e");
    } else if (p.state == QStringLiteral("故障")) {
        stateColor = QStringLiteral("#ef4444");
    } else {
        stateColor = QStringLiteral("#f59e0b");
    }
    auto *state = new QLabel(stateText, card);
    state->setAlignment(Qt::AlignCenter);
    state->setFixedSize(44, 20);
    state->setStyleSheet(QStringLiteral(
        "background-color:%1; color:#0a1025; border-radius:10px; font-size:11px; font-weight:bold;")
        .arg(stateColor));

    const bool idle = (p.state == QStringLiteral("空闲"));
    auto *btn = new QPushButton(mine ? QStringLiteral("充电中")
                                     : (idle ? QStringLiteral("预约充电") : QStringLiteral("已占用")), card);
    btn->setObjectName(QStringLiteral("ghostButton"));
    btn->setFixedWidth(80);
    btn->setEnabled(idle && !mine);
    if (idle && !mine) {
        connect(btn, &QPushButton::clicked, this, [this, p] { startCharging(p); });
    }

    h->addLayout(left, 1);
    h->addWidget(state);
    h->addWidget(btn);
    return card;
}

void StationDetailPage::startCharging(const Pile &pile)
{
    if (!m_hasStation || m_orderId > 0 || m_phase == Phase::Starting)
        return;
    m_pendingPileCode = pile.code;
    m_pendingPower = pile.powerKw;
    m_myPileCode = pile.code;   // 立即标记，桩卡片瞬间切换为「充电中」（乐观 UI）
    rebuildPiles();
    if (!m_session || !m_session->startCharge(m_phone, pile.code)) {
        releaseMyPile();        // 请求未发出，回滚为「预约充电」
        QMessageBox::warning(this, QStringLiteral("无法开始充电"),
                             QStringLiteral("请连接服务器并登录，或等待上一请求确认。"));
        return;
    }
    m_phase = Phase::Starting;
    m_chargingPile->setText(QStringLiteral("正在确认电桩…"));
    m_endBtn->setEnabled(false);
}

void StationDetailPage::endCharging()
{
    if (m_orderId <= 0 || m_phase == Phase::Settling)
        return;
    m_timer->stop();
    m_charging = false;
    m_phase = Phase::SettlementFailed;
    releaseMyPile();   // 结束充电：桩标识立即还原为「预约充电」
    if (!m_session || !m_session->reportOrder(QString::number(m_orderId), m_activePile,
                                              m_kwh, m_kwh * m_serverUnitPrice)) {
        m_chargingPile->setText(QStringLiteral("尚未确认结算，请连接服务器后重试"));
        m_endBtn->setEnabled(true);
        return;
    }
    m_phase = Phase::Settling;
    m_chargingPile->setText(QStringLiteral("结算待服务器确认…"));
    m_endBtn->setEnabled(false);
}

bool StationDetailPage::restoreOrder(const QJsonObject &order)
{
    const int id = order.value("order_id").toInt();
    if (id <= 0 || id == m_orderId)
        return false;
    m_timer->stop();
    m_orderId = id;
    const bool freshStart = !m_pendingPileCode.isEmpty()
                            && m_pendingPileCode == order.value("pile_code").toString();
    m_phase = freshStart ? Phase::Charging : Phase::SettlementFailed;
    m_charging = freshStart;
    m_pendingPileCode.clear();
    m_hasStation = true;
    m_station = Station{};
    m_station.id = order.value("station_id").toInt();
    // 回填站的地理信息（坐标/名称/地址），保证充电后「导航」与静态地图仍可用
    m_station.latitude  = order.value("latitude").toDouble();
    m_station.longitude = order.value("longitude").toDouble();
    m_station.name      = order.value("station_name").toString();
    m_station.address   = order.value("address").toString();
    m_activePile = order.value("pile_code").toString();
    m_activePower = order.value("power_kw").toDouble();
    m_serverUnitPrice = order.value("unit_price").toDouble();
    m_station.price = m_serverUnitPrice;
    m_station.type = m_activePower <= 7 ? QStringLiteral("慢充") : QStringLiteral("快充");
    m_priceLabel->setText(QStringLiteral("本单锁定价 ¥%1/度").arg(m_serverUnitPrice,0,'f',2));
    m_estSlider->setEnabled(true);
    updateEstimate();
    const auto started = QDateTime::fromString(order.value("start_time").toString(),
                                             QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_elapsedSec = qMax<qint64>(0, started.secsTo(QDateTime::currentDateTime()));
    m_kwh = m_activePower * m_elapsedSec / 3600.0;
    m_nameLabel->setText(order.value("station_name").toString());
    m_addrLabel->setText(order.value("address").toString());
    m_chargingPile->setText(QStringLiteral("您有未完成的充电订单 #%1，请先结算（按时长模拟电量）").arg(id));
    m_timeLabel->setText(formatDuration(m_elapsedSec));
    m_kwhLabel->setText(QStringLiteral("已充 %1 kWh").arg(m_kwh, 0, 'f', 2));
    m_costLabel->setText(QStringLiteral("预计 ¥%1").arg(m_kwh*m_serverUnitPrice, 0, 'f', 2));
    m_endBtn->setText(QStringLiteral("结算未完成订单"));
    m_endBtn->setEnabled(true);
    if (freshStart) {
        m_endBtn->setText(QStringLiteral("结束并结算"));
        m_chargingPile->setText(QStringLiteral("正在充电：%1，单价已锁定").arg(m_activePile));
        m_timer->start();
    }
    return !freshStart;
}

void StationDetailPage::clearOrder()
{
    m_timer->stop();
    m_orderId = 0;
    m_phase = Phase::Idle;
    m_charging = false;
    m_hasStation = false;
    m_kwh = 0;
    m_myPileCode.clear();
    resetChargingView();
    m_endBtn->setEnabled(false);
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
    // 费用展示用服务器返回的执行价（含闲时折扣），与实际结算口径一致
    const double unitPrice = m_serverUnitPrice;
    const double cost = m_kwh * unitPrice;
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
