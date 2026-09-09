# PC 服务器端 - NO.16 Web 大屏数据聚合 + HTTP/JSON 服务测试
QT       += core sql network testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_dashboardapi
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_dashboardapi.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../dashboard_api.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../stationstore.h \
    $$PWD/../dashboard_api.h \
    $$PWD/../../common/loadforecast.h

RESOURCES += \
    $$PWD/../pcserver.qrc
