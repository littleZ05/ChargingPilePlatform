#ifndef USERCLIENT_LOGINPAGE_H
#define USERCLIENT_LOGINPAGE_H

#include <QWidget>

class QLineEdit;

/** 登录页（手机号免密登录，首次登录自动注册）。
 *  当前为本地演示（本地校验 11 位手机号），接入 Socket 后对接登录/注册接口。
 */
class LoginPage : public QWidget
{
    Q_OBJECT
public:
    explicit LoginPage(QWidget *parent = nullptr);

signals:
    void loginSucceeded(const QString &phone);

private slots:
    void tryLogin();

private:
    QLineEdit *m_phoneEdit = nullptr;
};

#endif // USERCLIENT_LOGINPAGE_H
