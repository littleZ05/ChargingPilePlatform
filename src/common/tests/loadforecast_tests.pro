# NO.17 充电负荷智能预测：简化时序模型单元测试（纯 QtCore，无 GUI）
QT       -= gui
QT       += core testlib
CONFIG   += console c++17 testcase

TARGET   = tst_loadforecast
TEMPLATE = app

INCLUDEPATH += $$PWD/..

SOURCES += \
    tst_loadforecast.cpp \
    ../loadforecast.cpp

HEADERS += \
    ../loadforecast.h
