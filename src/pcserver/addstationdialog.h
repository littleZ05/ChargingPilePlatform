#ifndef PCSERVER_ADDSTATIONDIALOG_H
#define PCSERVER_ADDSTATIONDIALOG_H

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

/**
 * 新增充电站对话框（模拟新增）。
 * 仅负责收集与校验输入；真正的建站 + 批量生成模拟电桩由 StationStore::addStation 完成。
 */
class AddStationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddStationDialog(QWidget *parent = nullptr);

    QString stationName() const;
    QString address() const;
    double  longitude() const;
    double  latitude() const;
    int     pileCount() const;

    /** 校验输入并通过 accept() 关闭；失败时在对话框内提示并返回 false（供按钮与测试共用） */
    bool tryAccept(QString *error = nullptr);

private:
    QLineEdit     *m_nameEdit = nullptr;
    QLineEdit     *m_addressEdit = nullptr;
    QDoubleSpinBox *m_longitudeSpin = nullptr;
    QDoubleSpinBox *m_latitudeSpin = nullptr;
    QSpinBox      *m_pileCountSpin = nullptr;
    QLabel        *m_errorLabel = nullptr;
};

#endif // PCSERVER_ADDSTATIONDIALOG_H
