#ifndef PCSERVER_CHARGE_SERVICE_H
#define PCSERVER_CHARGE_SERVICE_H

#include <QJsonObject>
#include <QString>

namespace pcserver {
class StationStore;

/** Authenticated business commands. Socket identity is supplied by the router,
 * never inferred from a user-controlled phone in a charging request. */
class ChargeService
{
public:
    explicit ChargeService(StationStore *store) : m_store(store) {}
    QJsonObject execute(int type, const QString &phone, const QJsonObject &request);
private:
    StationStore *m_store;
};
}
#endif
