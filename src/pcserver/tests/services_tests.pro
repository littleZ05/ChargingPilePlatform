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
