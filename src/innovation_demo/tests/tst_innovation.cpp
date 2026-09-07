#include <QtTest/QtTest>
#include "pricing_engine.h"
#include "selfheal_checker.h"

class TestInnovation : public QObject
{
    Q_OBJECT
private slots:
    void pricingThreshold();
    void selfHealStreak();
};

void TestInnovation::pricingThreshold()
{
    QVERIFY(!PricingEngine::shouldDiscount(30.0));  // 空闲率低：不打折
    QVERIFY(!PricingEngine::shouldDiscount(60.0));  // 临界 60%：不触发（>60%）
    QVERIFY(PricingEngine::shouldDiscount(61.0));   // >60%：触发
    QCOMPARE(PricingEngine::discountRate(), 0.8);   // 8 折与 common 常量一致
}

void TestInnovation::selfHealStreak()
{
    SelfHealLogic::State s;
    // 正常功率：计数清零
    SelfHealLogic::step(s, false);
    QCOMPARE(s.streak, 0);
    // 连续 3 次低功率触发告警
    SelfHealLogic::step(s, true);
    SelfHealLogic::step(s, true);
    QVERIFY(!SelfHealLogic::shouldAlert(s));
    SelfHealLogic::step(s, true);
    QVERIFY(SelfHealLogic::shouldAlert(s));
    // 恢复正常后计数清零
    SelfHealLogic::step(s, false);
    QVERIFY(!SelfHealLogic::shouldAlert(s));
    QCOMPARE(s.streak, 0);
}

QTEST_APPLESS_MAIN(TestInnovation)
#include "tst_innovation.moc"
