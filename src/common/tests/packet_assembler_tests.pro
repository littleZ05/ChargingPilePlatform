# PacketAssembler 通信协议组包/解包单元测试（纯逻辑，无 GUI）
QT       -= gui
QT       += core testlib
CONFIG   += console c++17 testcase
CONFIG   -= app_bundle

TARGET   = packet_assembler_tests
TEMPLATE = app

INCLUDEPATH += $$PWD/..

SOURCES += \
    $$PWD/tst_packet_assembler.cpp \
    $$PWD/../packet_assembler.cpp

HEADERS += \
    $$PWD/../packet_assembler.h
