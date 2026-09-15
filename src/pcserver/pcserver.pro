# PC 服务器端（Linux + Qt）
# 说明：NO.9 销售业绩（毛悦琮）与 NO.17 负荷预测（吴羽桐）均使用 QChart，
#       因此主工程已启用 charts 模块；公共算法 loadforecast 仅供 QtCore 使用。
QT       += core gui widgets network sql charts
CONFIG   += c++17

TARGET   = PcServer
TEMPLATE = app

INCLUDEPATH += $$PWD/../common

SOURCES += \
    addstationdialog.cpp \
    dashboard_api.cpp \
    main.cpp \
    mainwindow.cpp \
    stationstore.cpp \
    pricingservice.cpp \
    selfhealservice.cpp \
    opsconsole.cpp \
    uitheme.cpp \
    ../common/net_server.cpp \
    ../common/packet_assembler.cpp \
    ../common/loadforecast.cpp

HEADERS += \
    addstationdialog.h \
    dashboard_api.h \
    mainwindow.h \
    stationstore.h \
    pricingservice.h \
    selfhealservice.h \
    opsconsole.h \
    uitheme.h \
    ../common/common.h \
    ../common/net_server.h \
    ../common/packet_assembler.h \
    ../common/loadforecast.h

FORMS += \
    mainwindow.ui

RESOURCES += \
    pcserver.qrc

# NO.18 主题源文件随 qrc 编译（:/styles/theme.qss）；此处登记便于 IDE/打包定位
DISTFILES += \
    styles/dark_theme.qss

QMAKE_CXXFLAGS += -Wall

SOURCES += $$PWD/charge_service.cpp
HEADERS += $$PWD/charge_service.h

# Explicit dependency: qmake6 may omit qrc payload dependencies in Unicode paths.
schema_resource.target = qrc_pcserver.cpp
schema_resource.depends = $$PWD/../database/schema.sql
QMAKE_EXTRA_TARGETS += schema_resource

QT += concurrent
SOURCES += $$PWD/../common/forecast_async.cpp
HEADERS += $$PWD/../common/forecast_async.h

SOURCES += $$PWD/admin_repository.cpp $$PWD/admin_demo_seed.cpp
HEADERS += $$PWD/admin_repository.h

SOURCES += $$PWD/mainwindow_support.cpp $$PWD/mainwindow_pages.cpp $$PWD/mainwindow_socket.cpp
HEADERS += $$PWD/mainwindow_support.h $$PWD/mainwindow_p.h

SOURCES += $$PWD/stationstore_orders.cpp
