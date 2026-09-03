#ifndef USERCLIENT_MAINWINDOW_H
#define USERCLIENT_MAINWINDOW_H

#include <QMainWindow>

namespace Ui { class MainWindow; }

/** 充电用户端主窗口（骨架，负责人：葛伊诺） */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
};

#endif // USERCLIENT_MAINWINDOW_H
