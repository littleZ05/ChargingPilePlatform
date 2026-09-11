#ifndef USERCLIENT_STATION_H
#define USERCLIENT_STATION_H

#include <QString>
#include <QVector>
#include <cmath>

#include "common.h"

/** 充电桩信息（对应数据库 piles 表，供用户端展示） */
struct Pile {
    QString code;      // 桩编号，如 "DS-01"
    QString type;      // 快充 / 慢充
    double  powerKw = 0.0;
    QString state;     // 空闲 / 故障 / 使用中
};

/** 充电站信息（对应数据库 stations 表，供用户端展示） */
struct Station {
    int     id         = 0;      // 服务器 station id（0 = 本地占位站，无后端数据）
    QString name;
    QString address;
    double  latitude   = 0.0;
    double  longitude  = 0.0;
    double  price      = 1.0;    // 基础电价（元/度）
    int     totalPiles = 0;      // 总桩数
    int     idlePiles  = 0;      // 空闲桩数
    double  onlineRate = 100.0;  // 在线率 0~100
    QString type;                // 快充 / 慢充 / 快慢兼有
    QString pilePrefix;          // 桩号前缀，如 "DS"
    QVector<Pile> piles;         // 站内电桩（buildPiles 生成）
    bool serverSale = false;     // 服务器标记的闲时特惠（联调数据源）
};

/** 充电订单（对应数据库 orders 表，供「我的」页展示） */
struct Order {
    QString orderNo;
    QString pileCode;
    QString time;
    double  kwh    = 0.0;
    double  amount = 0.0;
    QString state;               // 已结算 / 进行中
};

/** 用户当前定位（演示用，后续接入设备 GPS / 腾讯定位） */
inline constexpr double kUserLat = 39.990;
inline constexpr double kUserLng = 116.380;

/** 当前定位状态（初始为演示坐标；「首页」搜索经腾讯地理编码可更新为真实地址坐标） */
struct UserLocation {
    double  lat   = kUserLat;
    double  lng   = kUserLng;
    QString label = QStringLiteral("北京市");
};
inline UserLocation gUserLocation;

/** 生成站内电桩：前 idle 个「空闲」、下一个「故障」、其余「使用中」。 */
inline QVector<Pile> buildPiles(const QString &prefix, int total, int idle, const QString &type)
{
    QVector<Pile> piles;
    for (int i = 1; i <= total; ++i) {
        Pile p;
        p.code = QStringLiteral("%1-P%2").arg(prefix).arg(i, 2, 10, QLatin1Char('0'));
        const bool slow = (type == QStringLiteral("慢充"))
                       || (type == QStringLiteral("快慢兼有") && (i % 2 == 0));
        p.type = slow ? QStringLiteral("慢充") : QStringLiteral("快充");
        p.powerKw = slow ? 7.0 : 120.0;
        if (i <= idle)          p.state = QStringLiteral("空闲");
        else if (i == idle + 1) p.state = QStringLiteral("故障");
        else                    p.state = QStringLiteral("使用中");
        piles.append(p);
    }
    return piles;
}

/** 演示用电站数据（后续经 Socket 从服务器 stations 表获取） */
inline QVector<Station> mockStations()
{
    const struct Raw {
        QString name, address, type, prefix;
        double lat, lng, price, online;
        int total, idle;
    } raw[] = {
        { QStringLiteral("中关村软件园充电站"), QStringLiteral("海淀区东北旺西路8号"), QStringLiteral("快充"), QStringLiteral("ZG"), 40.047, 116.297, 1.00, 92.0, 12,  8 },
        { QStringLiteral("五道口充电站"), QStringLiteral("海淀区成府路28号"), QStringLiteral("快充"), QStringLiteral("WD"), 39.992, 116.338, 1.10, 88.0, 16, 11 },
        { QStringLiteral("望京SOHO充电站"), QStringLiteral("朝阳区望京街10号"), QStringLiteral("快充"), QStringLiteral("WJ"), 39.996, 116.481, 1.10, 88.0, 16, 11 },
        { QStringLiteral("国贸CBD充电站"), QStringLiteral("朝阳区建国门外大街1号"), QStringLiteral("慢充"), QStringLiteral("GM"), 39.908, 116.461, 0.95, 75.0,  8,  2 },
        { QStringLiteral("上地信息产业基地站"), QStringLiteral("海淀区上地十街10号"), QStringLiteral("快慢兼有"), QStringLiteral("SD"), 40.050, 116.304, 1.05, 90.0, 10,  7 },
    };

    QVector<Station> list;
    for (const Raw &r : raw) {
        Station s;
        s.name = r.name; s.address = r.address;
        s.latitude = r.lat; s.longitude = r.lng;
        s.price = r.price; s.totalPiles = r.total; s.idlePiles = r.idle;
        s.onlineRate = r.online; s.type = r.type; s.pilePrefix = r.prefix;
        s.piles = buildPiles(r.prefix, r.total, r.idle, r.type);
        list.append(s);
    }
    return list;
}

/** 简单稳定哈希：让同一个真实充电站在不同运行中呈现一致的模拟状态。 */
inline int stableHash(const QString &s, int lo, int hi)
{
    int h = 0;
    for (const QChar c : s)
        h = h * 131 + c.unicode();
    if (h < 0) h = -h;
    return lo + (h % (hi - lo + 1));
}

/**
 * 由腾讯「周边搜索」返回的真实 POI 构造 Station。
 * 腾讯只提供名称/地址/经纬度（静态信息）；价格、桩数、空闲数、在线率等实时状态
 * 腾讯拿不到，这里用稳定哈希生成「模拟值」占位（与 mock 同理），
 * 后续接入服务器 / 运营商接口后可替换为真实值。
 */
inline Station makePoiStation(const QString &name, const QString &address,
                              double lat, double lng)
{
    Station s;
    s.name = name;
    s.address = address;
    s.latitude = lat;
    s.longitude = lng;
    s.price      = 1.00 + 0.05 * stableHash(name, 0, 6);   // 1.00 ~ 1.30 元/度
    s.totalPiles = stableHash(name, 4, 20);
    s.idlePiles  = stableHash(name, 0, s.totalPiles);
    s.onlineRate = 80.0 + stableHash(name, 0, 20);          // 80 ~ 100 %
    s.type       = QStringLiteral("快充");
    s.pilePrefix = QStringLiteral("TC");                    // 真实 POI 无桩号，用通用前缀
    s.piles      = buildPiles(s.pilePrefix, s.totalPiles, s.idlePiles, s.type);
    return s;
}

/**
 * 创新点1「闲时动态计费」的用户侧展示逻辑。
 * 阈值/折扣统一取 common.h 的 cp::Pricing（组长维护），不再本地重复定义。
 */

/** 空闲率 = 空闲桩 / 总桩数 */
inline double idleRateOf(const Station &s)
{
    return s.totalPiles > 0 ? double(s.idlePiles) / s.totalPiles : 0.0;
}

/** 是否命中「闲时特惠」（空闲率超过阈值） */
inline bool isOnSale(const Station &s)
{
    // 服务器数据优先：来自服务器的电站（id>0）一律以服务器特惠标记为准，
    // 避免客户端按本地空闲率自行打折、导致展示价与服务器实际结算价不一致。
    if (s.id > 0)
        return s.serverSale;
    return idleRateOf(s) > cp::Pricing::kIdleRateThreshold;
}

/** 计费单价（命中闲时特惠则打折） */
inline double effectivePrice(const Station &s)
{
    return isOnSale(s) ? s.price * cp::Pricing::kDiscount : s.price;
}

/** 计算两经纬度间距离（km，Haversine 近似） */
inline double distanceKm(double lat1, double lng1, double lat2, double lng2)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kR  = 6371.0; // 地球半径 km
    const double dLat = (lat2 - lat1) * kPi / 180.0;
    const double dLng = (lng2 - lng1) * kPi / 180.0;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                   + std::cos(lat1 * kPi / 180.0) * std::cos(lat2 * kPi / 180.0)
                     * std::sin(dLng / 2) * std::sin(dLng / 2);
    return kR * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

#endif // USERCLIENT_STATION_H
