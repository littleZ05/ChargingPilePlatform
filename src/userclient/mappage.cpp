#include "mappage.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPixmap>
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
#include <QJsonValue>

#include "tencentkey.h"

namespace {

/** 静态图请求尺寸（竖版，贴近地图区约 420×640 的宽高比） */
constexpr int kMapWidth  = 480;
constexpr int kMapHeight = 720;

/** path 折线最多保留的坐标点数，避免 URL 过长 */
constexpr int kMaxPathPoints = 40;

/** 解码腾讯 direction API 的 polyline 为 (纬度, 经度) 坐标序列。
 *  常见格式为扁平数组 [lat*1e6, lng*1e6, ...]，兼容嵌套 [[lat,lng], ...]。 */
QVector<QPair<double, double>> decodePolyline(const QJsonValue &v)
{
    QVector<QPair<double, double>> pts;
    if (!v.isArray()) return pts;

    const QJsonArray arr = v.toArray();
    if (arr.isEmpty()) return pts;

    const bool nested = arr.first().isArray();
    auto append = [&pts](double lat, double lng) {
        if (lat != 0.0 || lng != 0.0)
            pts.append(qMakePair(lat, lng));
    };

    if (nested) {
        for (const QJsonValue &e : arr) {
            const QJsonArray p = e.toArray();
            if (p.size() >= 2)
                append(p.at(0).toDouble(), p.at(1).toDouble());
        }
    } else {
        for (int i = 0; i + 1 < arr.size(); i += 2)
            append(arr.at(i).toDouble() / 1e6, arr.at(i + 1).toDouble() / 1e6);
    }
    return pts;
}

/** 均匀降采样，保证终点精确落在最后一点。 */
QVector<QPair<double, double>> downsample(const QVector<QPair<double, double>> &pts,
                                          int maxPts)
{
    if (pts.size() <= maxPts || maxPts <= 0) return pts;

    QVector<QPair<double, double>> out;
    out.reserve(maxPts);
    const int n = pts.size();
    const double step = double(n - 1) / double(maxPts - 1);
    for (int i = 0; i < maxPts; ++i)
        out.append(pts.at(qRound(i * step)));
    out.last() = pts.last();
    return out;
}

} // namespace

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

    // ---- 静态地图区（图片由静态图 API 返回，直接用 QLabel 显示）----
    auto *mapArea = new QFrame(this);
    mapArea->setObjectName(QStringLiteral("mapArea"));
    auto *mv = new QVBoxLayout(mapArea);
    mv->setContentsMargins(0, 0, 0, 0);

    m_mapLabel = new QLabel(QStringLiteral("地图加载中…"), mapArea);
    m_mapLabel->setObjectName(QStringLiteral("mapImage"));
    m_mapLabel->setAlignment(Qt::AlignCenter);
    m_mapLabel->setScaledContents(true);
    m_mapLabel->setMinimumSize(300, 300);
    mv->addWidget(m_mapLabel);
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
    m_routePath.clear();

    m_routeLabel->setText(QStringLiteral("我的位置 → %1").arg(station.name));
    m_infoLabel->setText(QStringLiteral("路线计算中…"));

    loadStaticMap(); // 先显示底图 + 起终点标注
    fetchRoute();    // 再算路线，成功后带 path 折线重刷
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

    // 解码路线坐标，作为静态图 path 画出真实驾车路线
    m_routePath = downsample(decodePolyline(route.value(QStringLiteral("polyline"))),
                             kMaxPathPoints);
    loadStaticMap();
}

void MapPage::loadStaticMap()
{
    ++m_mapSeq;
    QNetworkRequest req(QUrl::fromEncoded(staticMapUrl().toUtf8()));
    req.setRawHeader("User-Agent", "UserClient/1.0");
    QNetworkReply *reply = m_nam->get(req);
    reply->setProperty("seq", m_mapSeq);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onStaticMapReply(reply); });
}

QString MapPage::staticMapUrl() const
{
    const double d = distanceKm(gUserLocation.lat, gUserLocation.lng,
                                m_station.latitude, m_station.longitude);
    const double centerLat = (gUserLocation.lat + m_station.latitude) / 2.0;
    const double centerLng = (gUserLocation.lng + m_station.longitude) / 2.0;

    // 单中文字 label 需手动百分号编码，其余分隔符（| : , *）按官方示例原样拼接
    auto esc = [](const QString &s) { return QString::fromUtf8(QUrl::toPercentEncoding(s)); };

    QStringList query;
    query << QStringLiteral("center=%1,%2").arg(centerLat).arg(centerLng);
    query << QStringLiteral("zoom=%1").arg(autoZoom(d));
    query << QStringLiteral("size=%1*%2").arg(kMapWidth).arg(kMapHeight);
    query << QStringLiteral("maptype=roadmap");

    // 标注：起点（绿，label「我」）+ 终点（红，label「站」）
    query << QStringLiteral("markers=%1").arg(
        QStringLiteral("size:mid|color:green|label:%1|%2,%3")
            .arg(esc(QStringLiteral("我")), QString::number(gUserLocation.lat), QString::number(gUserLocation.lng)));
    query << QStringLiteral("markers=%1").arg(
        QStringLiteral("size:mid|color:red|label:%1|%2,%3")
            .arg(esc(QStringLiteral("站")), QString::number(m_station.latitude), QString::number(m_station.longitude)));

    // 路线折线（polyline 解码成功才有）
    if (!m_routePath.isEmpty()) {
        QStringList pts;
        pts.reserve(m_routePath.size());
        for (const auto &p : m_routePath)
            pts << QStringLiteral("%1,%2").arg(p.first).arg(p.second);
        query << QStringLiteral("path=%1").arg(
            QStringLiteral("color:0x0ea5e9|weight:5|") + pts.join(QLatin1Char('|')));
    }

    query << QStringLiteral("key=%1").arg(QLatin1String(kTencentMapKey));

    return QStringLiteral("https://apis.map.qq.com/ws/staticmap/v2/?")
           + query.join(QLatin1Char('&'));
}

int MapPage::autoZoom(double d) const
{
    if (d <= 0.5) return 16;
    if (d <= 1.0) return 15;
    if (d <= 2.0) return 14;
    if (d <= 5.0) return 13;
    if (d <= 10.0) return 12;
    if (d <= 25.0) return 11;
    if (d <= 50.0) return 10;
    return 9;
}

void MapPage::onStaticMapReply(QNetworkReply *reply)
{
    const bool stale = (reply->property("seq").toInt() != m_mapSeq);
    const bool ok = !stale && reply->error() == QNetworkReply::NoError;
    const QByteArray data = ok ? reply->readAll() : QByteArray();
    reply->deleteLater();

    if (stale) return;

    if (!ok || data.isEmpty()) {
        m_mapLabel->setText(QStringLiteral("地图加载失败\n可点击下方按钮导航"));
        return;
    }

    QPixmap pm;
    if (!pm.loadFromData(data)) {
        m_mapLabel->setText(QStringLiteral("地图图片解析失败"));
        return;
    }
    m_mapLabel->setPixmap(pm);
}

void MapPage::showFallbackInfo()
{
    const double km = distanceKm(gUserLocation.lat, gUserLocation.lng, m_station.latitude, m_station.longitude);
    const int minutes = qMax(1, qRound(km / 40.0 * 60.0));
    m_infoLabel->setText(QStringLiteral("驾车导航 · 距离约 %1 km · 预计 %2 分钟（直线估算）")
                             .arg(QString::number(km, 'f', 1))
                             .arg(minutes));

    m_routePath.clear();
    loadStaticMap(); // 无路线时只保留起终点标注
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
