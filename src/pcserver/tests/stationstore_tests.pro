# PC 服务器端 - 充电站数据访问层单元测试
QT       += core sql testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_stationstore
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_stationstore.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../stationstore.h \
    $$PWD/../../common/loadforecast.h

RESOURCES += \
    $$PWD/../pcserver.qrc
