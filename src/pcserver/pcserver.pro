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
