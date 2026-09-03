#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("充电用户端 - 东软电动汽车充电桩应用管理平台"));
    statusBar()->showMessage(QStringLiteral("骨架工程：附近电站/导航/用户/充电功能待开发"), 5000);
}

MainWindow::~MainWindow()
{
    delete ui;
}
