QT -= gui
QT += core concurrent testlib
CONFIG += console c++17 testcase
TARGET = forecast_async_tests
TEMPLATE = app
SOURCES += $$PWD/tst_forecast_async.cpp $$PWD/../forecast_async.cpp $$PWD/../loadforecast.cpp
HEADERS += $$PWD/../forecast_async.h $$PWD/../loadforecast.h
