#include "loginpage.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch();

    // 图标（闪电）
    auto *icon = new QLabel(QStringLiteral("⚡"), this);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(72, 72);
    icon->setStyleSheet(QStringLiteral(
        "background-color:#0ea5e9; color:#ffffff; border-radius:16px; font-size:36px;"));
    outer->addWidget(icon, 0, Qt::AlignHCenter);

    outer->addSpacing(12);

    auto *title = new QLabel(QStringLiteral("充电用户端"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("手机号免密登录 · 首次登录自动注册"), this);
    subtitle->setObjectName(QStringLiteral("hintText"));
    subtitle->setAlignment(Qt::AlignCenter);
    outer->addWidget(subtitle);

    outer->addSpacing(20);

    m_phoneEdit = new QLineEdit(this);
    m_phoneEdit->setObjectName(QStringLiteral("input"));
    m_phoneEdit->setPlaceholderText(QStringLiteral("请输入11位手机号"));
    m_phoneEdit->setMaxLength(11);
    m_phoneEdit->setAlignment(Qt::AlignCenter);
    m_phoneEdit->setFixedHeight(46);
    outer->addWidget(m_phoneEdit);

    outer->addSpacing(12);

    auto *loginBtn = new QPushButton(QStringLiteral("登录"), this);
    loginBtn->setObjectName(QStringLiteral("primaryButton"));
    loginBtn->setFixedHeight(46);
    connect(loginBtn, &QPushButton::clicked, this, &LoginPage::tryLogin);
    outer->addWidget(loginBtn);

    outer->addStretch();

    auto *demo = new QLabel(QStringLiteral(
        "演示账号：\n13800138001（有余额）\n13800138002（无余额）"), this);
    demo->setObjectName(QStringLiteral("hintText"));
    demo->setAlignment(Qt::AlignCenter);
    outer->addWidget(demo);

    connect(m_phoneEdit, &QLineEdit::returnPressed, this, &LoginPage::tryLogin);
}

void LoginPage::tryLogin()
{
    const QString phone = m_phoneEdit->text().trimmed();
    bool allDigit = phone.length() == 11;
    for (const QChar c : phone) {
        if (!c.isDigit()) { allDigit = false; break; }
    }
    if (!allDigit) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入正确的11位手机号"));
        return;
    }
    // TODO(geyinuo): 接入 Socket 后调用登录/注册接口，落库 users 表。
    emit loginSucceeded(phone);
}
