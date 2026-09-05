# 错误处理公共组件单元测试（纯逻辑，无 GUI）
QT       -= gui
QT       += core testlib
CONFIG   += console c++17 testcase

TARGET   = error_utils_tests
TEMPLATE = app

INCLUDEPATH += $$PWD/..

SOURCES += tst_error_utils.cpp
