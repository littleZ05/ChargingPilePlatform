#include "addstationdialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "stationstore.h"

AddStationDialog::AddStationDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("新增充电站（模拟新增）"));
    setModal(true);
    resize(460, 260);

    auto *rootLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("stationNameEdit"));
    m_nameEdit->setPlaceholderText(QStringLiteral("如：浑南科技园充电站"));
    formLayout->addRow(QStringLiteral("站名 *"), m_nameEdit);

    m_addressEdit = new QLineEdit(this);
    m_addressEdit->setObjectName(QStringLiteral("stationAddressEdit"));
    m_addressEdit->setPlaceholderText(QStringLiteral("如：沈阳市浑南区智慧三街 88 号"));
    formLayout->addRow(QStringLiteral("地址 *"), m_addressEdit);

    m_longitudeSpin = new QDoubleSpinBox(this);
    m_longitudeSpin->setObjectName(QStringLiteral("stationLongitudeSpin"));
    m_longitudeSpin->setRange(-180.0, 180.0);
    m_longitudeSpin->setDecimals(6);
    m_longitudeSpin->setSingleStep(0.0001);
    m_longitudeSpin->setValue(123.450000);
    m_longitudeSpin->setSuffix(QStringLiteral(" °"));
    formLayout->addRow(QStringLiteral("经度 *"), m_longitudeSpin);

    m_latitudeSpin = new QDoubleSpinBox(this);
    m_latitudeSpin->setObjectName(QStringLiteral("stationLatitudeSpin"));
    m_latitudeSpin->setRange(-90.0, 90.0);
    m_latitudeSpin->setDecimals(6);
    m_latitudeSpin->setSingleStep(0.0001);
    m_latitudeSpin->setValue(41.700000);
    m_latitudeSpin->setSuffix(QStringLiteral(" °"));
    formLayout->addRow(QStringLiteral("纬度 *"), m_latitudeSpin);

    m_pileCountSpin = new QSpinBox(this);
    m_pileCountSpin->setObjectName(QStringLiteral("stationPileCountSpin"));
    m_pileCountSpin->setRange(1, 100);
    m_pileCountSpin->setValue(10);
    m_pileCountSpin->setSuffix(QStringLiteral(" 根"));
    formLayout->addRow(QStringLiteral("电桩数量 *"), m_pileCountSpin);
    rootLayout->addLayout(formLayout);

    auto *hintLabel = new QLabel(
        QStringLiteral("保存后将模拟新增该电站，并按设定数量自动生成演示电桩"
                       "（快慢充混合，初始含闲置/充电中/故障状态）。"),
        this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QStringLiteral("color: #666;"));
    rootLayout->addWidget(hintLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("addStationErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    rootLayout->addWidget(m_errorLabel);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定新增"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    rootLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() { tryAccept(); });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString AddStationDialog::stationName() const
{
    return m_nameEdit->text().trimmed();
}

QString AddStationDialog::address() const
{
    return m_addressEdit->text().trimmed();
}

double AddStationDialog::longitude() const
{
    return m_longitudeSpin->value();
}

double AddStationDialog::latitude() const
{
    return m_latitudeSpin->value();
}

int AddStationDialog::pileCount() const
{
    return m_pileCountSpin->value();
}

bool AddStationDialog::tryAccept(QString *error)
{
    QString internalError;
    const bool ok = pcserver::StationStore::validateInput(
        stationName(), address(), longitude(), latitude(), pileCount(), &internalError);
    if (!ok) {
        m_errorLabel->setText(internalError);
        if (error)
            *error = internalError;
        return false;
    }
    m_errorLabel->clear();
    accept();
    return true;
}
