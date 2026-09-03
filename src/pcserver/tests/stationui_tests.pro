# PC 服务器端 - 充电站管理界面测试（offscreen）
QT       += core gui widgets sql testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_stationui
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_stationui.cpp \
    $$PWD/../mainwindow.cpp \
    $$PWD/../stationstore.cpp

HEADERS += \
    $$PWD/../mainwindow.h \
    $$PWD/../stationstore.h

FORMS += \
    $$PWD/../mainwindow.ui

RESOURCES += \
    $$PWD/../pcserver.qrc
