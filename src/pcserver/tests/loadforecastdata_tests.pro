# NO.17 负荷预测数据源 + 简化模型 集成测试（StationStore + loadforecast）
QT       += core sql testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_loadforecastdata
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_loadforecastdata.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../stationstore.h \
    $$PWD/../../common/loadforecast.h

RESOURCES += \
    $$PWD/../pcserver.qrc
