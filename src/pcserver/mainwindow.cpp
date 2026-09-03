#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("PC 服务器端 - 东软电动汽车充电桩应用管理平台"));
    statusBar()->showMessage(QStringLiteral("骨架工程：登录/业绩/桩/站/用户管理待开发"), 5000);
}

MainWindow::~MainWindow()
{
    delete ui;
}
