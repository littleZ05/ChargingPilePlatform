# 充电用户端（Linux + Qt）
QT       += core gui widgets network sql
CONFIG   += c++17

TARGET   = charging_ui_tests
TEMPLATE = app

# 引入共享代码目录
INCLUDEPATH += $$PWD/../../common $$PWD/..

SOURCES += \
    $$PWD/tst_charging_ui.cpp \
    $$PWD/../mainwindow.cpp \
    $$PWD/../loginpage.cpp \
    $$PWD/../stationpage.cpp \
    $$PWD/../stationdetailpage.cpp \
    $$PWD/../profilepage.cpp \
    $$PWD/../mappage.cpp \
    $$PWD/../pcserver_session.cpp \
    $$PWD/../../common/net_client.cpp \
    $$PWD/../../common/packet_assembler.cpp

HEADERS += \
    $$PWD/../mainwindow.h \
    $$PWD/../loginpage.h \
    $$PWD/../stationpage.h \
    $$PWD/../stationdetailpage.h \
    $$PWD/../profilepage.h \
    $$PWD/../mappage.h \
    $$PWD/../station.h \
    $$PWD/../tencentkey.h \
    $$PWD/../pcserver_session.h \
    $$PWD/../../common/common.h \
    $$PWD/../../common/net_client.h \
    $$PWD/../../common/packet_assembler.h

QMAKE_CXXFLAGS += -Wall

SOURCES += $$PWD/../session_business.cpp

QT += webenginewidgets
SOURCES += $$PWD/../map_routes.cpp
HEADERS += $$PWD/../map_routes.h

QT += testlib
CONFIG += testcase console

SOURCES += $$PWD/../user_theme.cpp
HEADERS += $$PWD/../user_theme.h
