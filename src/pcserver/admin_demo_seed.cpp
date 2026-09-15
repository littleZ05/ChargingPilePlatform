#include "admin_repository.h"
#include "stationstore.h"
#include "common.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QDateTime>
#include <algorithm>
namespace pcserver_admin {
bool DatabaseManager::seedDemoData(QString *error)
{
    struct StationSeed {
        QString name;
        QString address;
        double longitude;
        double latitude;
        double basePrice;
    };
    const QVector<StationSeed> stationSeeds = {
        { QStringLiteral("中关村软件园A站"), QStringLiteral("北京市海淀区东北旺西路8号"), 116.30, 40.05, 1.20 },
        { QStringLiteral("望京SOHO快充站"), QStringLiteral("北京市朝阳区望京街10号"), 116.48, 40.00, 1.08 },
        { QStringLiteral("上地园区慢充站"), QStringLiteral("北京市海淀区上地十街10号"), 116.30, 40.05, 0.98 }
    };

    QVector<StationRow> stationRows = stations(error);
    if (stationRows.isEmpty() && scalarInt(QStringLiteral("SELECT COUNT(*) FROM stations"), {}, error) == 0) {
        for (const auto &seed : stationSeeds) {
            const int id = insertRowId(QStringLiteral(
                "INSERT INTO stations(name, address, longitude, latitude, total_piles, online_rate, base_price) "
                "VALUES(?, ?, ?, ?, 0, 0, ?)"),
                { seed.name, seed.address, seed.longitude, seed.latitude, seed.basePrice }, error);
            if (id < 0) {
                return false;
            }
            stationRows.push_back({ id, seed.name, seed.address, seed.longitude, seed.latitude, 0, 0.0, seed.basePrice });
        }
    }
    if (stationRows.isEmpty()) {
        stationRows = stations(error);
    }

    if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM admins"), {}, error) == 0) {
        if (insertRowId(QStringLiteral("INSERT INTO admins(username, password) VALUES(?, ?)"),
            { QStringLiteral("admin"), QStringLiteral("123456") }, error) < 0) {
            return false;
        }
    }

    struct UserSeed {
        QString phone;
        QString nickname;
        double balance;
    };
    const QVector<UserSeed> userSeeds = {
        { QStringLiteral("13800000001"), QStringLiteral("张晨"), 218.20 },
        { QStringLiteral("13800000002"), QStringLiteral("李婷"), 126.50 },
        { QStringLiteral("13800000003"), QStringLiteral("王磊"), 326.80 },
        { QStringLiteral("13800000004"), QStringLiteral("赵敏"), 95.40 },
        { QStringLiteral("13800000005"), QStringLiteral("周扬"), 452.60 }
    };
    QVector<int> userIds;
    if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM users"), {}, error) == 0) {
        for (const auto &seed : userSeeds) {
            const int id = insertRowId(QStringLiteral(
                "INSERT INTO users(phone, nickname, balance, status) VALUES(?, ?, ?, 0)"),
                { seed.phone, seed.nickname, seed.balance }, error);
            if (id < 0) {
                return false;
            }
            userIds.push_back(id);
        }
    }
    if (userIds.isEmpty()) {
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral("SELECT id FROM users ORDER BY id ASC"))) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
        while (query.next()) {
            userIds.push_back(query.value(0).toInt());
        }
    }

    struct PileSeed {
        int stationIndex;
        QString code;
        QString type;
        double powerKw;
        int state;
    };
    const QVector<PileSeed> pileSeeds = {
        { 0, QStringLiteral("A-01"), QStringLiteral("快充"), 120.0, 1 },
        { 0, QStringLiteral("A-02"), QStringLiteral("快充"), 90.0, 0 },
        { 0, QStringLiteral("A-03"), QStringLiteral("慢充"), 7.0, 0 },
        { 1, QStringLiteral("B-01"), QStringLiteral("快充"), 80.0, 0 },
        { 1, QStringLiteral("B-02"), QStringLiteral("快充"), 60.0, 2 },
        { 1, QStringLiteral("B-03"), QStringLiteral("慢充"), 11.0, 0 },
        { 2, QStringLiteral("C-01"), QStringLiteral("慢充"), 7.0, 0 },
        { 2, QStringLiteral("C-02"), QStringLiteral("慢充"), 7.0, 1 }
    };
    if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM piles"), {}, error) == 0) {
        for (const auto &seed : pileSeeds) {
            const int stationId = stationRows.value(seed.stationIndex).id;
            if (insertRowId(QStringLiteral(
                "INSERT INTO piles(station_id, code, type, power_kw, state, charge_count, charge_seconds) "
                "VALUES(?, ?, ?, ?, ?, 0, 0)"),
                { stationId, seed.code, seed.type, seed.powerKw, seed.state }, error) < 0) {
                return false;
            }
        }
    }

    stationRows = stations(error);
    QVector<PileInfo> pileInfos = currentPileInfos(error);
    if (pileInfos.isEmpty()) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("未能读取电桩基础数据");
        }
        return false;
    }

    if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM marketing_strategy"), {}, error) == 0) {
        for (int i = 0; i < std::min(2, static_cast<int>(stationRows.size())); ++i) {
            const auto &station = stationRows.at(i);
            const double discount = i == 0 ? 0.88 : 0.92;
            const QString rule = i == 0
                ? QStringLiteral("工作日早晚高峰外 8.8 折")
                : QStringLiteral("夜间 22:00 后 9.2 折");
            if (insertRowId(QStringLiteral(
                "INSERT INTO marketing_strategy(station_id, base_price, discount, rule_desc, is_active, valid_from, valid_to) "
                "VALUES(?, ?, ?, ?, 1, datetime('now','localtime'), NULL)"),
                { station.id, station.basePrice, discount, rule }, error) < 0) {
                return false;
            }
        }
    }

    if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM pile_health_metrics"), {}, error) == 0) {
        for (const auto &pile : pileInfos) {
            const double avg = pile.powerKw * 0.72;
            if (insertRowId(QStringLiteral(
                "INSERT INTO pile_health_metrics(pile_id, avg_power, low_threshold, high_threshold, sample_count) "
                "VALUES(?, ?, ?, ?, 12)"),
                { pile.id, avg, avg * 0.65, avg * 1.35 }, error) < 0) {
                return false;
            }
        }
    }

    if (scalarInt(QStringLiteral("SELECT COUNT(*) FROM orders"), {}, error) == 0) {
        QHash<int, int> chargeCountByPile;
        QHash<int, int> chargeSecondsByPile;
        QRandomGenerator rng(42);
        const QDate today = QDate::currentDate();

        for (int dayOffset = 29; dayOffset >= 0; --dayOffset) {
            const int orderCount = (dayOffset % 3 == 0) ? 2 : 1;
            const QDate date = today.addDays(-dayOffset);
            for (int orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
                const int pileIndex = rng.bounded(pileInfos.size());
                const auto pile = pileInfos.at(pileIndex);
                const auto station = std::find_if(stationRows.begin(), stationRows.end(), [&](const StationRow &row) {
                    return row.id == pile.stationId;
                });
                const int userId = userIds.at(rng.bounded(userIds.size()));
                const int state = (dayOffset % 8 == 0) ? 2 : ((dayOffset % 7 == 0 && orderIndex == 0) ? 0 : 1);
                const QTime startTime(8 + ((dayOffset + orderIndex) % 10), (dayOffset * 7 + orderIndex * 11) % 60);
                const QDateTime start(date, startTime);
                QString endText;
                double kwh = 0.0;
                double price = 0.0;
                double amount = 0.0;
                int durationSec = 0;

                if (state == static_cast<int>(cp::OrderState::Finished)) {
                    kwh = 12.0 + rng.bounded(36) / 2.0;
                    const double stationBasePrice = (station != stationRows.end()) ? station->basePrice : 1.0;
                    price = stationBasePrice * (pile.type == QStringLiteral("快充") ? 1.18 : 0.92);
                    amount = kwh * price;
                    durationSec = 1800 + rng.bounded(3600);
                    endText = start.addSecs(durationSec).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    chargeCountByPile[pile.id] += 1;
                    chargeSecondsByPile[pile.id] += durationSec;
                } else if (state == static_cast<int>(cp::OrderState::Charging)) {
                    price = 0.0;
                } else {
                    price = 1.0;
                    endText = start.addSecs(300).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                }

                const int orderId = insertRowId(QStringLiteral(
                    "INSERT INTO orders(user_id, pile_id, station_id, start_time, end_time, kwh, price, amount, state) "
                    "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                    { userId, pile.id, pile.stationId, start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                      endText.isEmpty() ? QVariant() : QVariant(endText), kwh, price, amount, state }, error);
                if (orderId < 0) {
                    return false;
                }

                if (state == static_cast<int>(cp::OrderState::Finished) && durationSec > 0) {
                    const double realPower = kwh / (durationSec / 3600.0);
                    if (insertRowId(QStringLiteral(
                        "INSERT INTO pile_power_logs(pile_id, order_id, real_power, logged_at) "
                        "VALUES(?, ?, ?, ?)"),
                        { pile.id, orderId, realPower, endText }, error) < 0) {
                        return false;
                    }
                }
            }
        }

        for (const auto &pile : pileInfos) {
            QSqlQuery query(m_db);
            query.prepare(QStringLiteral("UPDATE piles SET charge_count = ?, charge_seconds = ? WHERE id = ?"));
            query.addBindValue(chargeCountByPile.value(pile.id, 0));
            query.addBindValue(chargeSecondsByPile.value(pile.id, 0));
            query.addBindValue(pile.id);
            if (!query.exec()) {
                if (error) {
                    *error = query.lastError().text();
                }
                return false;
            }
        }
    }

    return recalculateAllStationStats(error);
}
}
