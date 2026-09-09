# 充电用户端（Linux + Qt）
QT       += core gui widgets network sql
CONFIG   += c++17

TARGET   = UserClient
TEMPLATE = app

# 引入共享代码目录
INCLUDEPATH += $$PWD/../common

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    servergateway.cpp \
    loginpage.cpp \
    stationpage.cpp \
    stationdetailpage.cpp \
    profilepage.cpp \
    mappage.cpp \
    ../common/net_client.cpp \
    ../common/packet_assembler.cpp

HEADERS += \
    mainwindow.h \
    servergateway.h \
    loginpage.h \
    stationpage.h \
    stationdetailpage.h \
    profilepage.h \
    mappage.h \
    station.h \
    tencentkey.h \
    ../common/common.h \
    ../common/net_client.h \
    ../common/packet_assembler.h

QMAKE_CXXFLAGS += -Wall
