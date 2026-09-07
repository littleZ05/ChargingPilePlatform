# NetClient/NetServer 通用异步 Socket 通信组件端到端联调测试（需求 NO.19）
QT       -= gui
QT       += core network testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = net_socket_tests
TEMPLATE = app

INCLUDEPATH += $$PWD/..

SOURCES += \
    $$PWD/tst_net_socket.cpp \
    $$PWD/../net_client.cpp \
    $$PWD/../net_server.cpp \
    $$PWD/../packet_assembler.cpp

HEADERS += \
    $$PWD/../net_client.h \
    $$PWD/../net_server.h \
    $$PWD/../packet_assembler.h \
    $$PWD/../common.h
