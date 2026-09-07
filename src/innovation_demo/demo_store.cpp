#include "demo_store.h"
#include "../common/common.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QTimer>

namespace {
const char *kDdl =
    "CREATE TABLE IF NOT EXISTS stations("
    " id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL,"
    " longitude REAL DEFAULT 0, latitude REAL DEFAULT 0,"
    " base_price REAL NOT NULL DEFAULT 1.0);"
    "CREATE TABLE IF NOT EXISTS piles("
    " id INTEGER PRIMARY KEY AUTOINCREMENT, station_id INTEGER NOT NULL,"
    " code TEXT NOT NULL, power_kw REAL DEFAULT 0, state INTEGER DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS marketing_strategy("
    " id INTEGER PRIMARY KEY AUTOINCREMENT, station_id INTEGER NOT NULL UNIQUE,"
    " base_price REAL DEFAULT 1.0, discount REAL DEFAULT 1.0,"
    " rule_desc TEXT, is_active INTEGER DEFAULT 1);"
    "CREATE TABLE IF NOT EXISTS pile_health_metrics("
    " id INTEGER PRIMARY KEY AUTOINCREMENT, pile_id INTEGER NOT NULL UNIQUE,"
    " avg_power REAL DEFAULT 0, low_threshold REAL DEFAULT 0,"
    " high_threshold REAL DEFAULT 0, sample_count INTEGER DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS pile_power_logs("
    " id INTEGER PRIMARY KEY AUTOINCREMENT, pile_id INTEGER NOT NULL,"
    " real_power REAL DEFAULT 0, logged_at TEXT DEFAULT (datetime('now','localtime')));";
}

bool DemoStore::open(const QString &dbPath, QString *err)
{
    m_conn = QStringLiteral("id_demo_%1").arg(quintptr(this));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        if (err) *err = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    const QStringList stmts = QString::fromLatin1(kDdl)
                                  .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &s : stmts) {
        if (!q.exec(s)) {
            if (err) *err = q.lastError().text();
            return false;
        }
    }
    q.exec(QStringLiteral("SELECT COUNT(*) FROM stations"));
    bool empty = q.next() && q.value(0).toInt() == 0;
    if (empty)
        seed();
    return true;
}

void DemoStore::seed()
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.exec(QStringLiteral("INSERT INTO stations(name,base_price) VALUES('城东科技园站',1.00)"));
    q.exec(QStringLiteral("INSERT INTO stations(name,base_price) VALUES('城南枢纽充电站',1.20)"));
    q.exec(QStringLiteral("INSERT INTO piles(station_id,code,power_kw) VALUES(1,'S1-P01',60)"));
    q.exec(QStringLiteral("INSERT INTO piles(station_id,code,power_kw) VALUES(1,'S1-P02',60)"));
    q.exec(QStringLiteral("INSERT INTO piles(station_id,code,power_kw) VALUES(2,'S2-P01',120)"));
    q.exec(QStringLiteral("INSERT INTO pile_health_metrics(pile_id,avg_power,low_threshold,high_threshold)"
                          " VALUES(1,60,48,72),(2,60,48,72),(3,120,96,144)"));
}

QVector<DemoStation> DemoStore::stations() const
{
    QVector<DemoStation> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.exec(QStringLiteral("SELECT id,name,base_price FROM stations ORDER BY id"));
    while (q.next())
        out.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toDouble()});
    return out;
}

QVector<DemoPile> DemoStore::piles() const
{
    QVector<DemoPile> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.exec(QStringLiteral("SELECT id,station_id,code,power_kw FROM piles ORDER BY id"));
    while (q.next())
        out.append({q.value(0).toInt(), q.value(1).toInt(),
                    q.value(2).toString(), q.value(3).toDouble()});
    return out;
}

double DemoStore::lowThreshold(int pileId) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT low_threshold FROM pile_health_metrics WHERE pile_id=?"));
    q.addBindValue(pileId);
    return (q.exec() && q.next()) ? q.value(0).toDouble() : 0.0;
}

QString DemoStore::strategyText(int stationId) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral(
        "SELECT s.name, s.base_price, m.discount, m.rule_desc, m.is_active "
        "FROM stations s LEFT JOIN marketing_strategy m ON m.station_id=s.id AND m.is_active=1 "
        "WHERE s.id=?"));
    q.addBindValue(stationId);
    if (!q.exec() || !q.next())
        return QString();
    const double base = q.value(1).toDouble();
    const bool active = q.value(4).toInt() == 1 && q.value(2).toDouble() < 1.0;
    return active
        ? QStringLiteral("%1 当前【闲时特惠】%2 折，原价 %3 元/度")
              .arg(q.value(0).toString())
              .arg(QString::number(q.value(2).toDouble() * 10, 'f', 1))
              .arg(QString::number(base, 'f', 2))
        : QStringLiteral("%1 当前基础价 %2 元/度（无折扣）")
              .arg(q.value(0).toString())
              .arg(QString::number(base, 'f', 2));
}

bool DemoStore::applyStrategy(int stationId, double idleRate, QString *out)
{
    // 创新点1：预测空闲率 > 60%（cp::Pricing 常量）→ 自动 8 折并写营销策略表
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (idleRate > cp::Pricing::kIdleRateThreshold * 100.0) {
        q.prepare(QStringLiteral(
            "INSERT INTO marketing_strategy(station_id,base_price,discount,rule_desc,is_active) "
            "VALUES(?, (SELECT base_price FROM stations WHERE id=?), ?, ?, 1) "
            "ON CONFLICT(station_id) DO UPDATE SET discount=excluded.discount,"
            " rule_desc=excluded.rule_desc, is_active=1"));
        q.addBindValue(stationId);
        q.addBindValue(stationId);
        q.addBindValue(cp::Pricing::kDiscount);
        q.addBindValue(QStringLiteral("预测空闲率>60% 自动闲时特惠"));
        if (!q.exec()) { if (out) *out = q.lastError().text(); return false; }
        if (out) *out = QStringLiteral("【生效】预测空闲率 %1% > 60%，已自动下调为 8 折（闲时特惠）")
                            .arg(idleRate, 0, 'f', 1);
    } else {
        q.prepare(QStringLiteral("UPDATE marketing_strategy SET is_active=0 WHERE station_id=?"));
        q.addBindValue(stationId);
        q.exec();
        if (out) *out = QStringLiteral("【关闭】预测空闲率 %1% <= 60%，恢复基础价")
                            .arg(idleRate, 0, 'f', 1);
    }
    return true;
}

QString DemoStore::checkAndHeal(int pileId, double powerKw)
{
    // 创新点2：低于阈值连续 3 次（cp::SelfHeal）→ 状态“需检查”并自动模拟远程重启
    const double low = lowThreshold(pileId);
    if (low <= 0)
        return QStringLiteral("该桩缺少健康阈值，无法自愈检查");
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT COUNT(*), MAX(logged_at) FROM pile_power_logs "
                             "WHERE pile_id=? AND real_power<?"));
    q.addBindValue(pileId);
    q.addBindValue(low);
    q.exec();
    q.next();
    int streak = q.value(0).toInt();
    q.prepare(QStringLiteral("INSERT INTO pile_power_logs(pile_id,real_power) VALUES(?,?)"));
    q.addBindValue(pileId);
    q.addBindValue(powerKw);
    q.exec();
    if (powerKw >= low) {
        resetStreak(pileId);
        return QStringLiteral("功率 %1 kW 正常（阈值 %2 kW），状态 Normal")
                   .arg(powerKw, 0, 'f', 1).arg(low, 0, 'f', 1);
    }
    streak += 1;
    if (streak >= cp::SelfHeal::kConsecutiveCount) {
        resetStreak(pileId);
        // 自动触发一次模拟远程重启
        q.prepare(QStringLiteral("UPDATE piles SET state=2 WHERE id=?"));
        q.addBindValue(pileId);
        q.exec();
        QTimer::singleShot(0, [this, pileId]() {
            QSqlQuery u(QSqlDatabase::database(m_conn));
            u.prepare(QStringLiteral("UPDATE piles SET state=0 WHERE id=?"));
            u.addBindValue(pileId);
            u.exec();
        });
        return QStringLiteral("【自愈】连续 %1 次功率低于均值阈值 → 状态需检查，已自动触发模拟远程重启")
                   .arg(cp::SelfHeal::kConsecutiveCount);
    }
    return QStringLiteral("功率 %1 kW 低于阈值 %2 kW（第 %3/%4 次，接近告警）")
               .arg(powerKw, 0, 'f', 1).arg(low, 0, 'f', 1)
               .arg(streak).arg(cp::SelfHeal::kConsecutiveCount);
}

void DemoStore::resetStreak(int pileId)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("DELETE FROM pile_power_logs WHERE pile_id=?"));
    q.addBindValue(pileId);
    q.exec();
}
