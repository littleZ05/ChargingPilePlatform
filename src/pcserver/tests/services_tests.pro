QT       += core sql testlib
CONFIG   += console c++17 testcase
TARGET   = tst_services
TEMPLATE = app
INCLUDEPATH += $$PWD/.. $$PWD/../../common
SOURCES += \
    tst_services.cpp \
    ../pricingservice.cpp \
    ../selfhealservice.cpp
HEADERS += \
    ../pricingservice.h \
    ../selfhealservice.h

SOURCES += $$PWD/../stationstore.cpp $$PWD/../../common/loadforecast.cpp
RESOURCES += $$PWD/../pcserver.qrc

# Explicit dependency: qmake6 may omit qrc payload dependencies in Unicode paths.
schema_resource.target = qrc_pcserver.cpp
schema_resource.depends = $$PWD/../../database/schema.sql
QMAKE_EXTRA_TARGETS += schema_resource
