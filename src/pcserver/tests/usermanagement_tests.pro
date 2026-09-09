# PC 服务器端 - 用户管理界面测试（offscreen）
QT       += core gui widgets sql charts network testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_usermanagement
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_usermanagement.cpp \
    $$PWD/../addstationdialog.cpp \
    $$PWD/../dashboard_api.cpp \
    $$PWD/../mainwindow.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../../common/net_server.cpp \
    $$PWD/../../common/packet_assembler.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../addstationdialog.h \
    $$PWD/../dashboard_api.h \
    $$PWD/../mainwindow.h \
    $$PWD/../stationstore.h \
    $$PWD/../../common/net_server.h \
    $$PWD/../../common/packet_assembler.h \
    $$PWD/../../common/loadforecast.h

FORMS += \
    $$PWD/../mainwindow.ui

RESOURCES += \
    $$PWD/../pcserver.qrc
