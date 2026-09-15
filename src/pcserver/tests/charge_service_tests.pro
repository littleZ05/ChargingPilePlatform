QT += core gui sql testlib
CONFIG += console c++17 testcase
TARGET = tst_charge_service
TEMPLATE = app
INCLUDEPATH += $$PWD/.. $$PWD/../../common
SOURCES += $$PWD/tst_charge_service.cpp $$PWD/../charge_service.cpp $$PWD/../stationstore.cpp $$PWD/../../common/loadforecast.cpp
HEADERS += $$PWD/../charge_service.h $$PWD/../stationstore.h
RESOURCES += $$PWD/../pcserver.qrc

# Explicit dependency: qmake6 may omit qrc payload dependencies in Unicode paths.
schema_resource.target = qrc_pcserver.cpp
schema_resource.depends = $$PWD/../../database/schema.sql
QMAKE_EXTRA_TARGETS += schema_resource

SOURCES += $$PWD/../stationstore_orders.cpp
