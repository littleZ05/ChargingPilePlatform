#include "profilepage.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QScrollArea>
#include <QInputDialog>
#include <QMessageBox>

ProfilePage::ProfilePage(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(16, 16, 16, 16);
    v->setSpacing(12);
    scroll->setWidget(content);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto *title = new QLabel(QStringLiteral("我的"), content);
    title->setObjectName(QStringLiteral("pageTitle"));
    v->addWidget(title);

    // ---- 用户信息卡 ----
    auto *infoCard = new QFrame(content);
    infoCard->setObjectName(QStringLiteral("card"));
    auto *iv = new QVBoxLayout(infoCard);
    iv->setContentsMargins(16, 16, 16, 16);
    iv->setSpacing(10);

    auto *row = new QHBoxLayout;
    auto *avatar = new QLabel(QStringLiteral("用"), infoCard);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(56, 56);
    avatar->setStyleSheet(QStringLiteral(
        "background:#0ea5e9; color:#ffffff; border-radius:28px; font-size:22px; font-weight:bold;"));

    auto *infoV = new QVBoxLayout;
    infoV->setSpacing(2);
    m_nickLabel = new QLabel(m_nickname, infoCard);
    m_nickLabel->setStyleSheet(QStringLiteral("color:#ffffff; font-size:16px; font-weight:bold;"));
    m_phoneLabel = new QLabel(m_phone, infoCard);
    m_phoneLabel->setObjectName(QStringLiteral("hintText"));
    auto *regLabel = new QLabel(QStringLiteral("注册时间：2025-08-26 10:24"), infoCard);
    regLabel->setObjectName(QStringLiteral("hintText"));
    infoV->addWidget(m_nickLabel);
    infoV->addWidget(m_phoneLabel);
    infoV->addWidget(regLabel);

    auto *saveBtn = new QPushButton(QStringLiteral("保存"), infoCard);
    saveBtn->setObjectName(QStringLiteral("ghostButton"));
    saveBtn->setFixedWidth(64);
    connect(saveBtn, &QPushButton::clicked, this, &ProfilePage::editProfile);

    row->addWidget(avatar);
    row->addSpacing(12);
    row->addLayout(infoV, 1);
    row->addWidget(saveBtn, 0, Qt::AlignTop);
    iv->addLayout(row);

    auto *balRow = new QHBoxLayout;
    m_balanceLabel = new QLabel(QStringLiteral("余额 ¥%1").arg(QString::number(m_balance, 'f', 2)), infoCard);
    m_balanceLabel->setStyleSheet(QStringLiteral("color:#22c55e; font-size:15px; font-weight:bold;"));
    auto *rechargeBtn = new QPushButton(QStringLiteral("充值"), infoCard);
    rechargeBtn->setObjectName(QStringLiteral("primaryButton"));
    rechargeBtn->setFixedWidth(96);
    connect(rechargeBtn, &QPushButton::clicked, this, &ProfilePage::recharge);
    balRow->addWidget(m_balanceLabel);
    balRow->addStretch();
    balRow->addWidget(rechargeBtn);
    iv->addLayout(balRow);
    v->addWidget(infoCard);

    // ---- 充电订单 ----
    auto *orderTitle = new QLabel(QStringLiteral("充电订单"), content);
    orderTitle->setStyleSheet(QStringLiteral("color:#ffffff; font-size:15px; font-weight:bold;"));
    v->addWidget(orderTitle);

    m_orderLayout = new QVBoxLayout;
    m_orderLayout->setSpacing(8);
    v->addLayout(m_orderLayout);

    // 起始为空，仅展示真实充电操作产生的订单（不再显示 mock 假数据）
    m_orderEmpty = new QLabel(QStringLiteral("暂无充电订单"), content);
    m_orderEmpty->setObjectName(QStringLiteral("hintText"));
    m_orderEmpty->setAlignment(Qt::AlignCenter);
    m_orderLayout->addWidget(m_orderEmpty);

    // ---- 退出登录 ----
    auto *logoutBtn = new QPushButton(QStringLiteral("退出登录"), content);
    logoutBtn->setObjectName(QStringLiteral("logoutButton"));
    logoutBtn->setFixedHeight(44);
    connect(logoutBtn, &QPushButton::clicked, this, &ProfilePage::logoutRequested);
    v->addWidget(logoutBtn);

    v->addStretch();
}

QWidget *ProfilePage::makeOrderCard(const Order &o, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(6);

    auto *row1 = new QHBoxLayout;
    auto *no = new QLabel(o.orderNo, card);
    no->setStyleSheet(QStringLiteral("color:#ffffff; font-size:14px; font-weight:bold;"));
    auto *state = new QLabel(o.state, card);
    state->setStyleSheet(QStringLiteral("color:#22c55e; font-size:12px;"));
    row1->addWidget(no);
    row1->addStretch();
    row1->addWidget(state);

    auto *row2 = new QHBoxLayout;
    auto *info = new QLabel(QStringLiteral("电桩 %1 · %2").arg(o.pileCode, o.time), card);
    info->setObjectName(QStringLiteral("hintText"));
    auto *amt = new QLabel(QStringLiteral("%1度  ¥%2")
                               .arg(QString::number(o.kwh, 'f', 2))
                               .arg(QString::number(o.amount, 'f', 2)), card);
    amt->setStyleSheet(QStringLiteral("color:#22c55e; font-size:13px; font-weight:bold;"));
    row2->addWidget(info);
    row2->addStretch();
    row2->addWidget(amt);

    v->addLayout(row1);
    v->addLayout(row2);
    return card;
}

void ProfilePage::setPhone(const QString &phone)
{
    if (phone.isEmpty()) return;
    m_phone = phone;
    m_phoneLabel->setText(m_phone);
}

void ProfilePage::setUserInfo(const QString &nickname, double balance)
{
    if (!nickname.isEmpty()) {
        m_nickname = nickname;
        m_nickLabel->setText(m_nickname);
    }
    m_balance = balance;
    m_balanceLabel->setText(
        QStringLiteral("余额 ¥%1").arg(QString::number(m_balance, 'f', 2)));
}

void ProfilePage::addOrder(const Order &order)
{
    // 有真实订单后隐藏空提示，新订单插到列表最上方
    if (m_orderEmpty)
        m_orderEmpty->hide();
    m_orderLayout->insertWidget(0, makeOrderCard(order, m_orderLayout->parentWidget()));
}

void ProfilePage::editProfile()
{
    bool ok = false;
    const QString newNick = QInputDialog::getText(this, QStringLiteral("编辑资料"),
                                                  QStringLiteral("昵称："), QLineEdit::Normal,
                                                  m_nickname, &ok);
    if (ok && !newNick.trimmed().isEmpty()) {
        m_nickname = newNick.trimmed();
        m_nickLabel->setText(m_nickname);
        QMessageBox::information(this, QStringLiteral("已保存"),
                                 QStringLiteral("昵称已更新为：%1（当前为本地演示）").arg(m_nickname));
    }
}

void ProfilePage::recharge()
{
    bool ok = false;
    const double amount = QInputDialog::getDouble(this, QStringLiteral("充值"),
                                                  QStringLiteral("充值金额（元）："), 50.0, 1.0, 10000.0, 2, &ok);
    if (ok && amount > 0) {
        m_balance += amount;
        m_balanceLabel->setText(QStringLiteral("余额 ¥%1").arg(QString::number(m_balance, 'f', 2)));
        QMessageBox::information(this, QStringLiteral("充值成功"),
                                 QStringLiteral("已充值 ¥%1，当前余额 ¥%2")
                                     .arg(QString::number(amount, 'f', 2))
                                     .arg(QString::number(m_balance, 'f', 2)));
    }
}
