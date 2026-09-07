#ifndef ID_DEMOSTORE_H
#define ID_DEMOSTORE_H

/**
 * 创新点演示用的本地 SQLite 存储（表结构与 src/database/schema.sql 保持一致，子集）
 */
#include <QString>
#include <QVector>

struct DemoStation { int id; QString name; double basePrice; };
struct DemoPile   { int id; int stationId; QString code; double powerKw; };

class DemoStore
{
public:
    bool open(const QString &dbPath, QString *err);   // 建库+建表+演示数据
    QVector<DemoStation> stations() const;
    QVector<DemoPile> piles() const;
    double lowThreshold(int pileId) const;            // 自愈低异常阈值
    QString strategyText(int stationId) const;        // 当前营销策略文案
    bool applyStrategy(int stationId, double idleRate, QString *out); // 动态计费
    QString checkAndHeal(int pileId, double powerKw); // 自愈检查一次上报
    void resetStreak(int pileId);

private:
    QString m_conn;
    void seed();
    QString connectionName() const { return m_conn; }
};

#endif // ID_DEMOSTORE_H
