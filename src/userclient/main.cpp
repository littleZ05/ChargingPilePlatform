#include <QApplication>
#include "mainwindow.h"

namespace {

/** 全局样式（QSS）：深蓝暗黑主题（#0a1025 + 亮蓝 #0ea5e9），对齐老师示例视觉。 */
const char *kAppStyle = R"(
* {
    font-family: "Noto Sans CJK SC", "Source Han Sans SC", "PingFang SC", "Microsoft YaHei", sans-serif;
}

QWidget {
    background-color: #0a1025;
    color: #a5b4cf;
    font-size: 13px;
}
QLabel {
    background: transparent;
}

QMainWindow { background-color: #0a1025; }

/* ---------- 文本 ---------- */
QLabel#pageTitle {
    color: #ffffff;
    font-size: 20px;
    font-weight: bold;
}
QLabel#hintText {
    color: #8a94a6;
    font-size: 12px;
}
QLabel#infoTag {
    color: #a5b4cf;
    font-size: 13px;
}

/* ---------- 卡片 ---------- */
QFrame#card {
    background-color: #141b38;
    border: 1px solid #1f2a4d;
    border-radius: 8px;
}

/* ---------- 按钮 ---------- */
QPushButton#primaryButton {
    background-color: #0ea5e9;
    color: #ffffff;
    border: none;
    border-radius: 6px;
    padding: 10px 0;
    font-size: 15px;
    font-weight: bold;
}
QPushButton#primaryButton:hover { background-color: #0b9cd1; }
QPushButton#primaryButton:pressed { background-color: #0990c2; }
QPushButton#primaryButton:disabled { background-color: #1e3a5f; color: #4a6a8f; }

QPushButton#ghostButton {
    background-color: #0e2a4d;
    color: #0ea5e9;
    border: none;
    border-radius: 6px;
    padding: 7px 0;
    font-size: 13px;
}
QPushButton#ghostButton:hover { background-color: #123a6b; }
QPushButton#ghostButton:disabled { background-color: #141b38; color: #4a5a7a; }

QPushButton#logoutButton {
    background-color: #ef4444;
    color: #ffffff;
    border: none;
    border-radius: 6px;
    font-size: 15px;
    font-weight: bold;
}
QPushButton#logoutButton:hover { background-color: #dc2626; }

/* ---------- 输入框 ---------- */
QLineEdit#input {
    background-color: #1a2340;
    border: 1px solid #1f2a4d;
    border-radius: 6px;
    color: #ffffff;
    padding: 0 12px;
    font-size: 14px;
}
QLineEdit#input:focus { border: 1px solid #0ea5e9; }
QLineEdit#input::placeholder { color: #6b7a99; }

/* ---------- 底部导航 ---------- */
QWidget#navBar {
    background-color: #0d1530;
    border-top: 1px solid #1f2a4d;
}
QPushButton#navTab {
    border: none;
    background: transparent;
    color: #6b7a99;
    font-size: 14px;
    padding: 10px 0;
}
QPushButton#navTab:checked {
    color: #0ea5e9;
    font-weight: bold;
}

/* ---------- 电站卡片（QPushButton） ---------- */
QPushButton#stationCard {
    background-color: #141b38;
    border: 1px solid #1f2a4d;
    border-radius: 8px;
}
QPushButton#stationCard:hover { border: 1px solid #0ea5e9; }
QPushButton#stationCard[onSale="true"] { border: 1px solid #ff6b00; }

QLabel#saleBadge {
    background-color: #ff6b00;
    color: #ffffff;
    border-radius: 9px;
    font-size: 11px;
    font-weight: bold;
}

/* ---------- 列表 ---------- */
QListWidget#stationList {
    background: transparent;
    border: none;
}
QListWidget#stationList::item { background: transparent; }

/* ---------- 滚动区 ---------- */
QScrollArea {
    background: transparent;
    border: none;
}

/* ---------- 滑块 ---------- */
QSlider::groove:horizontal {
    background: #1a2340;
    height: 4px;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: #0ea5e9;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #0ea5e9;
    width: 16px;
    height: 16px;
    margin: -6px 0;
    border-radius: 8px;
}

/* ---------- 地图页 ---------- */
QWidget#topBar {
    background-color: #0d1530;
    border-bottom: 1px solid #1f2a4d;
}
QPushButton#navButton {
    background-color: #1a2340;
    color: #ffffff;
    border: none;
    border-radius: 16px;
    min-width: 32px;
    min-height: 32px;
    font-size: 15px;
}
QLabel#routeText {
    color: #ffffff;
    font-size: 13px;
}
QFrame#mapArea {
    background-color: #0e1733;
    border: none;
}

/* ---------- 弹窗 ---------- */
QMessageBox, QInputDialog { background-color: #141b38; }
QMessageBox QLabel, QInputDialog QLabel { color: #a5b4cf; }
QInputDialog QLineEdit {
    background-color: #1a2340;
    border: 1px solid #1f2a4d;
    color: #ffffff;
    padding: 4px 8px;
}
QMessageBox QPushButton, QInputDialog QPushButton {
    background-color: #0ea5e9;
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 6px 16px;
    min-width: 64px;
}
)";

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyleSheet(QString::fromLatin1(kAppStyle));

    MainWindow w;
    w.show();
    return app.exec();
}
