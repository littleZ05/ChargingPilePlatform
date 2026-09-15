QT += core gui widgets network testlib
CONFIG += console c++17 testcase
TARGET = station_recommendation_tests
TEMPLATE = app
INCLUDEPATH += $$PWD/../../common
SOURCES += $$PWD/tst_station_recommendation.cpp $$PWD/../stationpage.cpp
HEADERS += $$PWD/../stationpage.h
