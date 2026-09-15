QT -= gui
QT += core testlib
CONFIG += console c++17 testcase
TARGET = map_routes_tests
TEMPLATE = app
SOURCES += $$PWD/tst_map_routes.cpp $$PWD/../map_routes.cpp
HEADERS += $$PWD/../map_routes.h
