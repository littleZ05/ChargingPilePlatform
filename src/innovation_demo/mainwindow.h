#ifndef ID_MAINWINDOW_H
#define ID_MAINWINDOW_H

#include <QMainWindow>
#include "demo_store.h"

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStationChanged(int idx);
    void onPileChanged(int idx);
    void applyPricing();
    void reportPower();
    void reportNormalPower();

private:
    void reloadStations();
    void reloadPiles();
    void log(const QString &s);
    Ui::MainWindow *ui;
    DemoStore m_store;
    bool m_ready = false;
};

#endif
