#ifndef PCSERVER_ADMIN_REPOSITORY_H
#define PCSERVER_ADMIN_REPOSITORY_H
#include <QString>
#include <QVector>
#include <QHash>
#include <QVariant>
#include <QSqlDatabase>
#include <functional>
namespace pcserver { class StationStore; }
namespace pcserver_admin {
struct StationRow {
    int id = 0;
    QString name;
    QString address;
    double longitude = 0.0;
    double latitude = 0.0;
    int totalPiles = 0;
    double onlineRate = 0.0;
    double basePrice = 0.0;
};

struct PileInfo {
    int id = 0;
    int stationId = 0;
    QString code;
    QString type;
    double powerKw = 0.0;
};

struct PileRow {
    int id = 0;
    int stationId = 0;
    QString stationName;
    QString code;
    QString type;
    double powerKw = 0.0;
    int state = 0;
    int chargeCount = 0;
    int chargeSeconds = 0;
};

struct OrderRow {
    int id = 0;
    QString userName;
    QString stationName;
    QString pileCode;
    QString startTime;
    QString endTime;
    double kwh = 0.0;
    double price = 0.0;
    double amount = 0.0;
    int state = 0;
};

struct UserRow {
    int id = 0;
    QString phone;
    QString nickname;
    double balance = 0.0;
    QString gmtCreate;
    int status = 0; // 0 正常 / 1 冻结
};

struct RevenuePoint {
    QString label;
    double amount = 0.0;
};

struct SalesSummary {
    double today = 0.0;
    double month = 0.0;
    double total = 0.0;
};

QString &adminDbPathOverride();
class DatabaseManager
{
public:
    static DatabaseManager &instance()
    {
        static DatabaseManager manager;
        return manager;
    }

    bool initialize(QString *error = nullptr);
    bool attach(pcserver::StationStore &store, QString *error = nullptr);
    void detach(const QString &connectionName);

    bool verifyAdmin(const QString &username, const QString &password, QString *error = nullptr) const;

    SalesSummary salesSummary(QString *error = nullptr) const;

    QVector<RevenuePoint> revenueSeries(int days, QString *error = nullptr) const;

    QVector<OrderRow> recentOrders(int limit, QString *error = nullptr) const;

    QVector<PileRow> pileRows(int stateFilter = -1, QString *error = nullptr) const;

    QVector<StationRow> stations(QString *error = nullptr) const;

    QVector<UserRow> users(const QString &phoneKeyword = QString(),
                           QString *error = nullptr) const;

    /** 冻结(1)/解冻(0)：仅允许合法状态，成功后刷新 gmt_modified */
    bool setUserStatus(int userId, int status, QString *error = nullptr);

    bool addPile(int stationId, const QString &code, const QString &type, double powerKw, int state, int *newId = nullptr, QString *error = nullptr);

    bool updatePile(int pileId, int stationId, const QString &code, const QString &type, double powerKw, int state, QString *error = nullptr);

    bool deletePile(int pileId, QString *error = nullptr);

    bool setPileState(int pileId, int state, QString *error = nullptr);

    bool remoteRestartPile(int pileId, QString *message = nullptr, QString *error = nullptr);

private:
    DatabaseManager() = default;
    bool mutate(const std::function<bool()> &operation, QString *error);
    bool canModifyPile(int pileId, bool deleting, QString *error);

    QString dbPath() const;

    bool openDatabase(QString *error);


    bool ensureSchema(QString *error);

    int scalarInt(const QString &sql, const QVariantList &binds = {}, QString *error = nullptr) const;

    double scalarDouble(const QString &sql, const QVariantList &binds = {}, QString *error = nullptr) const;

    int insertRowId(const QString &sql, const QVariantList &binds, QString *error = nullptr);

    QVector<PileInfo> currentPileInfos(QString *error = nullptr) const;


    bool recalculateStationStats(int stationId, QString *error = nullptr);

    bool recalculateAllStationStats(QString *error = nullptr);

    bool seedDemoData(QString *error);

    QSqlDatabase m_db;
    bool m_initialized = false;
};
}
#endif
