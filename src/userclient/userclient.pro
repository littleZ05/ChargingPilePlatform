# 充电用户端（Linux + Qt）
QT       += core gui widgets network sql
CONFIG   += c++17

TARGET   = UserClient
TEMPLATE = app

# 引入共享代码目录
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
