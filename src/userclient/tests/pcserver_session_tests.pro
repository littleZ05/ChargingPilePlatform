# 用户端 PcServer 会话测试：心跳/ACK、失联检测、退避重连、电站查询反序列化
QT       -= gui
QT       += core network testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = pcserver_session_tests
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_pcserver_session.cpp \
    $$PWD/../pcserver_session.cpp \
    $$PWD/../../common/net_client.cpp \
    $$PWD/../../common/net_server.cpp \
    $$PWD/../../common/packet_assembler.cpp

HEADERS += \
    $$PWD/../pcserver_session.h \
    $$PWD/../../common/common.h \
    $$PWD/../../common/net_client.h \
    $$PWD/../../common/net_server.h \
    $$PWD/../../common/packet_assembler.h

QMAKE_CXXFLAGS += -Wall
