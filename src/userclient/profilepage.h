#ifndef USERCLIENT_PROFILEPAGE_H
#define USERCLIENT_PROFILEPAGE_H

#include <QWidget>
#include "station.h"

class QLabel;

/** 个人中心（「我的」tab，NO.6，负责人：葛伊诺）。
 *  用户信息卡 + 充电订单 + 充值 + 退出登录。
 *  当前为本地演示（mock 用户/订单），待接入 Socket 后替换数据来源。
 */
class ProfilePage : public QWidget
{
    Q_OBJECT
public:
    explicit ProfilePage(QWidget *parent = nullptr);
    void setPhone(const QString &phone);

signals:
    void logoutRequested();

private slots:
    void editProfile();
    void recharge();

private:
    QWidget *makeOrderCard(const Order &order, QWidget *parent);

    QLabel *m_nickLabel    = nullptr;
    QLabel *m_phoneLabel   = nullptr;
    QLabel *m_balanceLabel = nullptr;

    QString m_phone    = QStringLiteral("13800138001");
    QString m_nickname = QStringLiteral("用户#001");
    double  m_balance  = 92.50;
};

#endif // USERCLIENT_PROFILEPAGE_H
