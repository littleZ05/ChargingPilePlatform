# PC 服务器端（Linux + Qt）
# 说明：基础工程不强制依赖 Qt Charts（当前源码未使用，避免无 charts 开发包环境编译失败）。
# 归属约定：NO.9 销售业绩（毛悦琮，feat/maoyuecong682）使用 QChart 时，
# 应在其分支的 pcserver.pro 增加 “QT += charts”，再随 PR 合入 main。
QT       += core gui widgets network sql charts
CONFIG   += c++17

TARGET   = PcServer
TEMPLATE = app

INCLUDEPATH += $$PWD/../common

SOURCES += \
    addstationdialog.cpp \
    main.cpp \
    mainwindow.cpp \
    stationstore.cpp

HEADERS += \
    addstationdialog.h \
    mainwindow.h \
    stationstore.h \
    ../common/common.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    pcserver.qrc

QMAKE_CXXFLAGS += -Wall
