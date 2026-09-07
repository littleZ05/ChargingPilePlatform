# 创新点 1/2 模拟触发演示（组长分支，中期评审用）
# 说明：以“手动输入”代替 ML 预测输出，演示价格策略与自愈检查机制闭环；
#       答辩前由吴羽桐 ML 接口替换手动输入。
QT       += core gui widgets sql
CONFIG   += c++17

TARGET   = InnovationDemo
TEMPLATE = app

INCLUDEPATH += $$PWD/../common

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    demo_store.cpp

HEADERS += \
    mainwindow.h \
    demo_store.h \
    pricing_engine.h \
    selfheal_checker.h \
    ../common/common.h

FORMS += mainwindow.ui

QMAKE_CXXFLAGS += -Wall
