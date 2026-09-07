#include "stationpage.h"

#include <algorithm>

#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

#include "tencentkey.h"

StationPage::StationPage(QWidget *parent)
    : QWidget(parent)
    , m_stations(mockStations())
{
    sortByDistance();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 0);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("附近充电站"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    m_locLabel = new QLabel(QStringLiteral("当前定位：定位中…"), this);
    m_locLabel->setObjectName(QStringLiteral("hintText"));
    layout->addWidget(m_locLabel);

    // 搜索 + 定位
    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName(QStringLiteral("input"));
    m_searchEdit->setPlaceholderText(QStringLiteral("输入地址重新定位(如: 浑南科技园)"));
    m_searchEdit->setFixedHeight(38);
    auto *locBtn = new QPushButton(QStringLiteral("定位"), this);
    locBtn->setObjectName(QStringLiteral("primaryButton"));
    locBtn->setFixedSize(64, 38);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(locBtn);
    layout->addLayout(searchRow);

    m_listTitle = new QLabel(QStringLiteral("附近充电站(按距离排序)"), this);
    m_listTitle->setObjectName(QStringLiteral("hintText"));
    layout->addWidget(m_listTitle);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("stationList"));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSpacing(10);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_list, 1);

    m_nam = new QNetworkAccessManager(this);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, &StationPage::relocate);
    connect(locBtn, &QPushButton::clicked, this, &StationPage::relocate);

    rebuildList();
    locateByIp();
}

void StationPage::sortByDistance()
{
    std::sort(m_stations.begin(), m_stations.end(),
              [](const Station &a, const Station &b) {
                  return distanceKm(gUserLocation.lat, gUserLocation.lng, a.latitude, a.longitude)
                       < distanceKm(gUserLocation.lat, gUserLocation.lng, b.latitude, b.longitude);
              });
}

void StationPage::relocate()
{
    const QString addr = m_searchEdit->text().trimmed();
    if (addr.isEmpty()) {
        // 未输入地址：回到演示默认定位
        gUserLocation.lat = kUserLat;
        gUserLocation.lng = kUserLng;
        gUserLocation.label = QStringLiteral("沈阳市");
        m_locLabel->setText(QStringLiteral("当前定位：%1").arg(gUserLocation.label));
        sortByDistance();
        rebuildList();
        searchNearbyStations();
        return;
    }

    ++m_geoSeq;
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("address"), addr);
    q.addQueryItem(QStringLiteral("key"), QLatin1String(kTencentMapKey));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "UserClient/1.0");
    QNetworkReply *reply = m_nam->get(req);
    reply->setProperty("seq", m_geoSeq);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onGeocodeReply(reply); });
}

void StationPage::onGeocodeReply(QNetworkReply *reply)
{
    const bool stale = (reply->property("seq").toInt() != m_geoSeq);
    const bool ok = !stale && reply->error() == QNetworkReply::NoError;
    const QByteArray data = ok ? reply->readAll() : QByteArray();
    const QString typed = m_searchEdit->text().trimmed();
    reply->deleteLater();

    if (stale) return;

    double lat = 0.0, lng = 0.0;
    QString label = typed;
    bool resolved = false;

    if (ok) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            const int status = root.value(QStringLiteral("status")).toInt();
            if (status == 0) {
                const QJsonObject result = root.value(QStringLiteral("result")).toObject();
                const QJsonObject loc = result.value(QStringLiteral("location")).toObject();
                lat = loc.value(QStringLiteral("lat")).toDouble();
                lng = loc.value(QStringLiteral("lng")).toDouble();
                if (lat != 0.0 || lng != 0.0) {
                    const QJsonObject comps = result.value(QStringLiteral("address_components")).toObject();
                    const QString district = comps.value(QStringLiteral("district")).toString();
                    const QString title = result.value(QStringLiteral("title")).toString();
                    if (!title.isEmpty()) {
                        label = district.isEmpty() ? title : (district + QStringLiteral(" ") + title);
                    }
                    resolved = true;
                }
            }
        }
    }

    if (!resolved) {
        // 地址解析失败（配额用尽/网络/无结果）→ 回退到地点搜索再定位一次
        relocateByPlaceSearch(typed);
        return;
    }

    gUserLocation.lat = lat;
    gUserLocation.lng = lng;
    gUserLocation.label = label;
    m_locLabel->setText(QStringLiteral("当前定位：%1").arg(label));
    sortByDistance();
    rebuildList();
    searchNearbyStations();
}

void StationPage::relocateByPlaceSearch(const QString &addr)
{
    ++m_geoSeq;
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/place/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("keyword"), addr);
    q.addQueryItem(QStringLiteral("boundary"),
                   QStringLiteral("nearby(%1,%2,50000)").arg(gUserLocation.lat).arg(gUserLocation.lng));
    q.addQueryItem(QStringLiteral("page_size"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("key"), QLatin1String(kTencentMapKey));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "UserClient/1.0");
    QNetworkReply *reply = m_nam->get(req);
    reply->setProperty("seq", m_geoSeq);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onRelocatePlaceReply(reply); });
}

void StationPage::onRelocatePlaceReply(QNetworkReply *reply)
{
    const bool stale = (reply->property("seq").toInt() != m_geoSeq);
    const bool ok = !stale && reply->error() == QNetworkReply::NoError;
    const QByteArray data = ok ? reply->readAll() : QByteArray();
    reply->deleteLater();

    if (stale) return;

    double lat = 0.0, lng = 0.0;
    QString label = m_searchEdit->text().trimmed();
    bool resolved = false;

    if (ok) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && doc.object().value(QStringLiteral("status")).toInt() == 0) {
            const QJsonArray arr = doc.object().value(QStringLiteral("data")).toArray();
            if (!arr.isEmpty()) {
                const QJsonObject o = arr.first().toObject();
                const QJsonObject loc = o.value(QStringLiteral("location")).toObject();
                lat = loc.value(QStringLiteral("lat")).toDouble();
                lng = loc.value(QStringLiteral("lng")).toDouble();
                const QString title = o.value(QStringLiteral("title")).toString();
                if (lat != 0.0 || lng != 0.0) {
                    if (!title.isEmpty()) label = title;
                    resolved = true;
                }
            }
        }
    }

    if (!resolved) {
        QMessageBox::information(this, QStringLiteral("定位失败"),
                                 QStringLiteral("未找到该地址，请检查后重试（如：浑南科技园）"));
        return;
    }

    gUserLocation.lat = lat;
    gUserLocation.lng = lng;
    gUserLocation.label = label;
    m_locLabel->setText(QStringLiteral("当前定位：%1").arg(label));
    sortByDistance();
    rebuildList();
    searchNearbyStations();
}

void StationPage::rebuildList()
{
    m_list->clear();
    for (const Station &s : m_stations) {
        QWidget *card = makeStationCard(s);
        // QPushButton 的 sizeHint()/minimumSizeHint() 都不含内部布局，会算成很小的高度；
        // 直接用布局的 totalSizeHint()（子控件真实高度 + 边距），并保底 88px。
        const int h = qMax(88, card->layout()->totalSizeHint().height());
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, h));
        m_list->addItem(item);
        m_list->setItemWidget(item, card);
    }
}

QWidget *StationPage::makeStationCard(const Station &s)
{
    const double dist = distanceKm(gUserLocation.lat, gUserLocation.lng, s.latitude, s.longitude);
    const bool onSale = isOnSale(s);

    auto *card = new QPushButton;
    card->setObjectName(QStringLiteral("stationCard"));
    card->setCursor(Qt::PointingHandCursor);
    if (onSale) card->setProperty("onSale", true);

    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(6);

    // 第一行：名称 + 闲点 + 距离
    auto *row1 = new QHBoxLayout;
    auto *name = new QLabel(s.name, card);
    name->setStyleSheet(QStringLiteral("color:#ffffff; font-size:15px; font-weight:bold;"));
    auto *distLabel = new QLabel(QString::number(dist, 'f', 1) + QStringLiteral(" km"), card);
    distLabel->setStyleSheet(QStringLiteral("color:#0ea5e9; font-size:13px; font-weight:bold;"));
    row1->addWidget(name);
    if (onSale) {
        auto *badge = new QLabel(QStringLiteral("闲"), card);
        badge->setObjectName(QStringLiteral("saleBadge"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(18, 18);
        badge->setToolTip(QStringLiteral("闲时特惠 · 8折"));
        row1->addWidget(badge);
    }
    row1->addStretch();
    row1->addWidget(distLabel);
    v->addLayout(row1);

    // 第二行：地址
    auto *addr = new QLabel(s.address, card);
    addr->setObjectName(QStringLiteral("hintText"));
    v->addWidget(addr);

    // 第三行：类型 / 价格 / 空闲 / 在线率（分栏对齐）
    auto *info = new QHBoxLayout;
    info->setSpacing(8);

    auto *typeLabel = new QLabel(s.type, card);
    typeLabel->setObjectName(QStringLiteral("infoTag"));
    typeLabel->setFixedWidth(56);

    QString priceText;
    if (onSale) {
        priceText = QStringLiteral(
            "<span style='color:#22c55e;font-weight:bold;'>¥%1/度</span> "
            "<s style='color:#6b7a99;'>¥%2</s>")
            .arg(QString::number(effectivePrice(s), 'f', 2))
            .arg(QString::number(s.price, 'f', 2));
    } else {
        priceText = QStringLiteral("<span style='color:#22c55e;font-weight:bold;'>¥%1/度</span>")
            .arg(QString::number(s.price, 'f', 2));
    }
    auto *priceLabel = new QLabel(priceText, card);
    priceLabel->setTextFormat(Qt::RichText);
    priceLabel->setFixedWidth(108);

    auto *idleLabel = new QLabel(QStringLiteral("空闲 %1/%2").arg(s.idlePiles).arg(s.totalPiles), card);
    idleLabel->setObjectName(QStringLiteral("infoTag"));
    idleLabel->setFixedWidth(66);

    auto *onlineLabel = new QLabel(QStringLiteral("在线率 %1%").arg(QString::number(s.onlineRate, 'f', 0)), card);
    onlineLabel->setObjectName(QStringLiteral("infoTag"));
    onlineLabel->setFixedWidth(74);

    info->addWidget(typeLabel);
    info->addWidget(priceLabel);
    info->addWidget(idleLabel);
    info->addWidget(onlineLabel);
    info->addStretch();
    v->addLayout(info);

    connect(card, &QPushButton::clicked, this, [this, s] { emit stationSelected(s); });

    return card;
}

void StationPage::searchNearbyStations()
{
    ++m_placeSeq;
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/place/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("keyword"), QStringLiteral("充电站"));
    q.addQueryItem(QStringLiteral("boundary"),
                   QStringLiteral("nearby(%1,%2,5000)").arg(gUserLocation.lat).arg(gUserLocation.lng));
    q.addQueryItem(QStringLiteral("orderby"), QStringLiteral("_distance"));
    q.addQueryItem(QStringLiteral("page_size"), QStringLiteral("20"));
    q.addQueryItem(QStringLiteral("key"), QLatin1String(kTencentMapKey));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "UserClient/1.0");
    QNetworkReply *reply = m_nam->get(req);
    reply->setProperty("seq", m_placeSeq);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onPlaceSearchReply(reply); });
}

void StationPage::onPlaceSearchReply(QNetworkReply *reply)
{
    const bool stale = (reply->property("seq").toInt() != m_placeSeq);
    const bool ok = !stale && reply->error() == QNetworkReply::NoError;
    const QByteArray data = ok ? reply->readAll() : QByteArray();
    reply->deleteLater();

    if (stale) return;

    QVector<Station> found;
    if (ok) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && doc.object().value(QStringLiteral("status")).toInt() == 0) {
            const QJsonArray arr = doc.object().value(QStringLiteral("data")).toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                const QJsonObject loc = o.value(QStringLiteral("location")).toObject();
                const double lat = loc.value(QStringLiteral("lat")).toDouble();
                const double lng = loc.value(QStringLiteral("lng")).toDouble();
                const QString name = o.value(QStringLiteral("title")).toString();
                const QString addr = o.value(QStringLiteral("address")).toString();
                if (name.isEmpty() || (lat == 0.0 && lng == 0.0)) continue;
                found.append(makePoiStation(name, addr, lat, lng));
            }
        }
    }

    if (found.isEmpty()) {
        // 失败（配额/网络/无结果）→ 保留演示数据，标题透出具体原因便于排查
        QString why = QStringLiteral("真实查询失败");
        if (ok) {
            const QJsonObject root = QJsonDocument::fromJson(data).object();
            const int st = root.value(QStringLiteral("status")).toInt();
            const QString msg = root.value(QStringLiteral("message")).toString();
            if (st != 0 && !msg.isEmpty()) why = msg;
        }
        m_listTitle->setText(QStringLiteral("附近充电站(演示数据 · %1)").arg(why));
        return;
    }

    m_stations = found;
    sortByDistance();
    rebuildList();
    m_listTitle->setText(QStringLiteral("附近充电站(腾讯真实 · 共%1家)").arg(found.size()));
}

void StationPage::locateByIp()
{
    ++m_ipSeq;
    QUrl url(QStringLiteral("https://apis.map.qq.com/ws/location/v1/ip"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), QLatin1String(kTencentMapKey));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "UserClient/1.0");
    QNetworkReply *reply = m_nam->get(req);
    reply->setProperty("seq", m_ipSeq);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onIpLocationReply(reply); });
}

void StationPage::onIpLocationReply(QNetworkReply *reply)
{
    const bool stale = (reply->property("seq").toInt() != m_ipSeq);
    const bool ok = !stale && reply->error() == QNetworkReply::NoError;
    const QByteArray data = ok ? reply->readAll() : QByteArray();
    reply->deleteLater();

    if (stale) return;

    if (ok) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && doc.object().value(QStringLiteral("status")).toInt() == 0) {
            const QJsonObject result = doc.object().value(QStringLiteral("result")).toObject();
            const QJsonObject loc = result.value(QStringLiteral("location")).toObject();
            const double lat = loc.value(QStringLiteral("lat")).toDouble();
            const double lng = loc.value(QStringLiteral("lng")).toDouble();
            if (lat != 0.0 || lng != 0.0) {
                const QJsonObject ad = result.value(QStringLiteral("ad_info")).toObject();
                const QString city = ad.value(QStringLiteral("city")).toString();
                gUserLocation.lat = lat;
                gUserLocation.lng = lng;
                gUserLocation.label = city.isEmpty() ? QStringLiteral("当前位置") : city;
            }
        }
    }

    // 无论 IP 定位成败，都刷新一次周边电站（失败则沿用默认演示坐标）
    m_locLabel->setText(QStringLiteral("当前定位：%1").arg(gUserLocation.label));
    searchNearbyStations();
}
