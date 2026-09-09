# PC 服务器端 - Socket 业务协议集成测试（docs/socket-protocol.md 配套）
QT       += core gui widgets sql network charts testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_socketbiz
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_socketbiz.cpp \
    $$PWD/../addstationdialog.cpp \
    $$PWD/../mainwindow.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../../common/net_client.cpp \
    $$PWD/../../common/net_server.cpp \
    $$PWD/../../common/packet_assembler.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../addstationdialog.h \
    $$PWD/../mainwindow.h \
    $$PWD/../stationstore.h \
    $$PWD/../../common/common.h \
    $$PWD/../../common/net_client.h \
    $$PWD/../../common/net_server.h \
    $$PWD/../../common/packet_assembler.h \
    $$PWD/../../common/loadforecast.h

FORMS += \
    $$PWD/../mainwindow.ui

RESOURCES += \
    $$PWD/../pcserver.qrc

QMAKE_CXXFLAGS += -Wall
