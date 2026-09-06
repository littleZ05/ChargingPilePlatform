# PC 服务器端 - 数据库核心表契约/一致性/索引测试
QT       += core sql testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_schema
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_schema.cpp \
    $$PWD/../stationstore.cpp

HEADERS += \
    $$PWD/../stationstore.h

RESOURCES += \
    $$PWD/../pcserver.qrc
