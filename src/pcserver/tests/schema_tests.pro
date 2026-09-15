# PC 服务器端 - 数据库核心表契约/一致性/索引测试
QT       += core sql testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = tst_schema
TEMPLATE = app

INCLUDEPATH += $$PWD/.. $$PWD/../../common

SOURCES += \
    $$PWD/tst_schema.cpp \
    $$PWD/../stationstore.cpp \
    $$PWD/../../common/loadforecast.cpp

HEADERS += \
    $$PWD/../stationstore.h \
    $$PWD/../../common/loadforecast.h

RESOURCES += \
    $$PWD/../pcserver.qrc

# Explicit dependency: qmake6 may omit qrc payload dependencies in Unicode paths.
schema_resource.target = qrc_pcserver.cpp
schema_resource.depends = $$PWD/../../database/schema.sql
QMAKE_EXTRA_TARGETS += schema_resource
