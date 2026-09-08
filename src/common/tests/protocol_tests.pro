# 公共通信协议数据结构测试（NO.15，维护人：陈庚泉）
QT       += core testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_protocol
TEMPLATE = app

INCLUDEPATH += $$PWD/..

SOURCES += \
    $$PWD/tst_protocol.cpp
