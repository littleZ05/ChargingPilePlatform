# PC 服务器端 - 充电站管理界面测试（offscreen）
QT       += core gui widgets sql network charts testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_stationui
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_stationui.cpp \
    $$PWD/../addstationdialog.cpp \
    $$PWD/../dashboard_api.cpp \
    $$PWD/../mainwindow.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../uitheme.cpp \
    $$PWD/../../common/net_server.cpp \
    $$PWD/../../common/packet_assembler.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../addstationdialog.h \
    $$PWD/../dashboard_api.h \
    $$PWD/../mainwindow.h \
    $$PWD/../stationstore.h \
    $$PWD/../uitheme.h \
    $$PWD/../../common/net_server.h \
    $$PWD/../../common/packet_assembler.h \
    $$PWD/../../common/loadforecast.h

FORMS += \
    $$PWD/../mainwindow.ui

RESOURCES += \
    $$PWD/../pcserver.qrc

SOURCES += $$PWD/../charge_service.cpp $$PWD/../opsconsole.cpp $$PWD/../pricingservice.cpp $$PWD/../selfhealservice.cpp
HEADERS += $$PWD/../charge_service.h $$PWD/../opsconsole.h $$PWD/../pricingservice.h $$PWD/../selfhealservice.h

# Explicit dependency: qmake6 may omit qrc payload dependencies in Unicode paths.
schema_resource.target = qrc_pcserver.cpp
schema_resource.depends = $$PWD/../../database/schema.sql
QMAKE_EXTRA_TARGETS += schema_resource
