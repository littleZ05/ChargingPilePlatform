# PC 服务器端（Linux + Qt）
QT       += core gui widgets network sql charts
CONFIG   += c++17

TARGET   = PcServer
TEMPLATE = app

INCLUDEPATH += $$PWD/../common

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h \
    ../common/common.h

FORMS += \
    mainwindow.ui

QMAKE_CXXFLAGS += -Wall
