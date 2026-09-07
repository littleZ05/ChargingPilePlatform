#include "mappage.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "tencentkey.h"

MapPage::MapPage(QWidget *parent)
    : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // ---- 顶栏：返回 + 路线标题 + 定位 ----
    auto *top = new QFrame(this);
    top->setObjectName(QStringLiteral("topBar"));
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(8, 6, 8, 6);
    tl->setSpacing(8);

    auto *backBtn = new QPushButton(QStringLiteral("←"), top);
    backBtn->setObjectName(QStringLiteral("navButton"));
    connect(backBtn, &QPushButton::clicked, this, &MapPage::backRequested);

    m_routeLabel = new QLabel(QStringLiteral("我的位置 → 充电站"), top);
    m_routeLabel->setObjectName(QStringLiteral("routeText"));
    m_routeLabel->setAlignment(Qt::AlignCenter);

    auto *locBtn = new QPushButton(QStringLiteral("◎"), top);
    locBtn->setObjectName(QStringLiteral("navButton"));

    tl->addWidget(backBtn);
    tl->addWidget(m_routeLabel, 1);
    tl->addWidget(locBtn);
    v->addWidget(top);

    // ---- 占位地图区 ----
    auto *mapArea = new QFrame(this);
    mapArea->setObjectName(QStringLiteral("mapArea"));
    auto *mv = new QVBoxLayout(mapArea);
    mv->setAlignment(Qt::AlignCenter);
    auto *hint = new QLabel(QStringLiteral("地图底图占位\n\n路线距离/时长已接入腾讯地图 WebService"), mapArea);
    hint->setAlignment(Qt::AlignCenter);
    hint->setObjectName(QStringLiteral("hintText"));
    mv->addWidget(hint);
    v->addWidget(mapArea, 1);

    // ---- 底部：路线信息 + 导航按钮 ----
    auto *bottom = new QFrame(this);
    bottom->setObjectName(QStringLiteral("card"));
    auto *bv = new QVBoxLayout(bottom);
    bv->setContentsMargins(16, 14, 16, 14);
    bv->setSpacing(10);

    m_infoLabel = new QLabel(bottom);
    m_infoLabel->setStyleSheet(QStringLiteral("color:#ffffff; font-size:14px;"));

    auto *openBtn = new QPushButton(QStringLiteral("打开腾讯地图导航"), bottom);
    openBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(openBtn, &QPushButton::clicked, this, &MapPage::openMap);

    bv->addWidget(m_infoLabel);
    bv->addWidget(openBtn);
    v->addWidget(bottom);

    m_nam = new QNetworkAccessManager(this);
}

void MapPage::setRoute(const Station &station)
{
    m_station = station;
    ++m_seq;

    m_routeLabel->setText(QStringLiteral("我的位置 → %1").arg(station.name));
    m_infoLabel->setText(QStringLiteral("路线计算中…"));

    fetchRoute();
}

void MapPage::fetchRoute()
{
    const QString url = QStringLiteral(
        "https://apis.map.qq.com/ws/direction/v1/driving/"
        "?from=%1,%2&to=%3,%4&key=%5")
        .arg(gUserLocation.lat).arg(gUserLocation.lng)
        .arg(m_station.latitude).arg(m_station.longitude)
        .arg(QLatin1String(kTencentMapKey));

    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("User-Agent", "UserClient/1.0");
    QNetworkReply *reply = m_nam->get(req);
    reply->setProperty("seq", m_seq);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onRouteReply(reply); });
}

void MapPage::onRouteReply(QNetworkReply *reply)
{
    const bool stale = (reply->property("seq").toInt() != m_seq);
    const bool ok = !stale && reply->error() == QNetworkReply::NoError;
    const QByteArray data = ok ? reply->readAll() : QByteArray();
    reply->deleteLater();

    if (stale) return;                 // 过期响应，丢弃
    if (!ok) { showFallbackInfo(); return; }

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { showFallbackInfo(); return; }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("status")).toInt() != 0) { showFallbackInfo(); return; }

    const QJsonArray routes = root.value(QStringLiteral("result")).toObject()
                                  .value(QStringLiteral("routes")).toArray();
    if (routes.isEmpty()) { showFallbackInfo(); return; }

    const QJsonObject route = routes.first().toObject();
    const double km = route.value(QStringLiteral("distance")).toDouble() / 1000.0; // 米 → km
    const int minutes = qMax(1, qRound(route.value(QStringLiteral("duration")).toDouble())); // 分钟
    const int lights = route.value(QStringLiteral("traffic_light_count")).toInt();

    m_infoLabel->setText(QStringLiteral("驾车导航 · 距离约 %1 km · 预计 %2 分钟 · 途经 %3 个红绿灯")
                             .arg(QString::number(km, 'f', 1))
                             .arg(minutes)
                             .arg(lights));
}

void MapPage::showFallbackInfo()
{
    const double km = distanceKm(gUserLocation.lat, gUserLocation.lng, m_station.latitude, m_station.longitude);
    const int minutes = qMax(1, qRound(km / 40.0 * 60.0));
    m_infoLabel->setText(QStringLiteral("驾车导航 · 距离约 %1 km · 预计 %2 分钟（直线估算）")
                             .arg(QString::number(km, 'f', 1))
                             .arg(minutes));
}

void MapPage::openMap()
{
    // 腾讯地图 URI API（Web）：浏览器打开真实驾车导航。
    // 桌面端用 https:// 网页导航（qqmap:// 需装腾讯地图 App，桌面无此协议处理）。
    QUrl url(QStringLiteral("https://apis.map.qq.com/uri/v1/routeplan"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("drive"));
    q.addQueryItem(QStringLiteral("from"), gUserLocation.label);
    q.addQueryItem(QStringLiteral("fromcoord"),
                   QStringLiteral("%1,%2").arg(gUserLocation.lat).arg(gUserLocation.lng));
    q.addQueryItem(QStringLiteral("to"), m_station.name);
    q.addQueryItem(QStringLiteral("tocoord"),
                   QStringLiteral("%1,%2").arg(m_station.latitude).arg(m_station.longitude));
    url.setQuery(q);

    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("无法打开浏览器，请检查默认浏览器设置。\n目标：%1").arg(m_station.name));
    }
}
