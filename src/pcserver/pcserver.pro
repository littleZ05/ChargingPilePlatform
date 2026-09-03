# PC 服务器端（Linux + Qt）
# 说明：当前源码未使用 Qt Charts；销售业绩模块实现时再按需增加 charts，避免无谓的编译依赖。
QT       += core gui widgets network sql
CONFIG   += c++17

TARGET   = PcServer
TEMPLATE = app

INCLUDEPATH += $$PWD/../common

SOURCES += \
    addstationdialog.cpp \
    main.cpp \
    mainwindow.cpp \
    stationstore.cpp

HEADERS += \
    addstationdialog.h \
    mainwindow.h \
    stationstore.h \
    ../common/common.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    pcserver.qrc

QMAKE_CXXFLAGS += -Wall
