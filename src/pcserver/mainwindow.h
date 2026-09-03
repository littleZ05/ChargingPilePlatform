#ifndef PCSERVER_MAINWINDOW_H
#define PCSERVER_MAINWINDOW_H

#include <QMainWindow>

namespace Ui { class MainWindow; }

/** PC 服务器端主窗口（骨架，负责人：毛悦琮 / 陈庚泉） */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
};

#endif // PCSERVER_MAINWINDOW_H
