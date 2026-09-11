#ifndef USERCLIENT_PROFILEPAGE_H
#define USERCLIENT_PROFILEPAGE_H

#include <QWidget>
#include "station.h"

class QLabel;
class QVBoxLayout;

namespace userclient {
class PcServerSession;
}

/** 个人中心（「我的」tab，NO.6，负责人：葛伊诺）。
 *  用户信息卡 + 充电订单 + 充值 + 退出登录。
 *  昵称/余额来自服务器登录回执，充值经 kRechargeRequest 真实落库。
 */
class ProfilePage : public QWidget
{
    Q_OBJECT
public:
    explicit ProfilePage(QWidget *parent = nullptr);
    void setPhone(const QString &phone);
    /** 注入应用级会话：充值走服务器并真实写入 users.balance */
    void setServerSession(userclient::PcServerSession *session);
    /** NO.6：服务器登录回执成功后刷新昵称/余额（离线时保持本地默认） */
    void setUserInfo(const QString &nickname, double balance);
    /** 追加一条充电订单到列表最上方（NO.7：结束充电后由 MainWindow 桥接调用） */
    void addOrder(const Order &order);

signals:
    void logoutRequested();

private slots:
    void editProfile();
    void recharge();
    void onRechargeResult(int code, const QString &message, double amount, double balance);

private:
    QWidget *makeOrderCard(const Order &order, QWidget *parent);

    QVBoxLayout *m_orderLayout = nullptr;
    QLabel      *m_orderEmpty  = nullptr;

    QLabel *m_nickLabel    = nullptr;
    QLabel *m_phoneLabel   = nullptr;
    QLabel *m_balanceLabel = nullptr;

    QString m_phone    = QStringLiteral("13800138001");
    QString m_nickname = QStringLiteral("用户#001");
    double  m_balance  = 92.50;
    userclient::PcServerSession *m_session = nullptr;
};

#endif // USERCLIENT_PROFILEPAGE_H
