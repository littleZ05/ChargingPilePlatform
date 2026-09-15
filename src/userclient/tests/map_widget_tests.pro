QT += core gui widgets network webenginewidgets testlib
CONFIG += console c++17 testcase
TARGET = map_widget_tests
TEMPLATE = app
SOURCES += $$PWD/tst_map_widget.cpp $$PWD/../mappage.cpp $$PWD/../map_routes.cpp
HEADERS += $$PWD/../mappage.h $$PWD/../map_routes.h

INCLUDEPATH += $$PWD/../../common
