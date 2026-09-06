#include <QtTest/QtTest>
#include "error_utils.h"

/** 需求 NO.20 错误处理公共组件 单元测试（负责人：张芮萌） */
class TestErrorUtils : public QObject
{
    Q_OBJECT
private slots:
    void phone();
    void amount();
    void errorText();
};

void TestErrorUtils::phone()
{
    QVERIFY(cp::isValidPhone(QStringLiteral("13800138000")));
    QVERIFY(cp::isValidPhone(QStringLiteral(" 13900139000 ")));  // 允许首尾空格
    QVERIFY(!cp::isValidPhone(QStringLiteral("1234567890")));     // 10 位
    QVERIFY(!cp::isValidPhone(QStringLiteral("123456789012")));   // 12 位
    QVERIFY(!cp::isValidPhone(QStringLiteral("23800138000")));    // 非 1 开头
    QVERIFY(!cp::isValidPhone(QStringLiteral("1380013800a")));    // 含字母
    QVERIFY(!cp::isValidPhone(QString()));
}

void TestErrorUtils::amount()
{
    double out = -1;
    QVERIFY(cp::isValidAmount(QStringLiteral("100"), &out));
    QCOMPARE(out, 100.0);
    QVERIFY(cp::isValidAmount(QStringLiteral("12.34"), &out));
    QVERIFY(cp::isValidAmount(QStringLiteral("0"), &out));
    QVERIFY(!cp::isValidAmount(QStringLiteral("12.345")));  // 三位小数
    QVERIFY(!cp::isValidAmount(QStringLiteral("-1")));      // 负数
    QVERIFY(!cp::isValidAmount(QStringLiteral("abc")));
    QVERIFY(!cp::isValidAmount(QString()));
}

void TestErrorUtils::errorText()
{
    QVERIFY(cp::appErrorText(cp::AppError::PhoneInvalid).contains(QStringLiteral("手机号")));
    QVERIFY(cp::appErrorText(cp::AppError::DivideByZero).contains(QStringLiteral("0")));
    QCOMPARE(cp::appErrorText(cp::AppError::Ok, QStringLiteral("done")),
             QStringLiteral("成功：done"));
}

QTEST_APPLESS_MAIN(TestErrorUtils)
#include "tst_error_utils.moc"
