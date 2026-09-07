QT       -= gui
QT       += core testlib
CONFIG   += console c++17 testcase
TARGET   = innovation_tests
TEMPLATE = app
INCLUDEPATH += $$PWD/.. $$PWD/../.. $$PWD/../../common
SOURCES += tst_innovation.cpp
