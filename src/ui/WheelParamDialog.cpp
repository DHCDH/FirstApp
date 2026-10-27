#include "WheelParamDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace
{
QDoubleSpinBox* MakeSpinBox(double minimum,
                            double maximum,
                            double value,
                            const QString& suffix,
                            int decimals = 4,
                            double step = 0.01,
                            QWidget* parent = nullptr)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setValue(value);
    spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    return spin;
}
}  // namespace

WheelParamDialog::WheelParamDialog(const WheelParamValues& wheelDefaults,
                                   const CutterParamValues& cutterDefaults,
                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Grinding Wheel Parameters"));
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        tr("The values below must match the geometry of the loaded wheel .obj."),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    auto* wheelGroup = new QGroupBox(tr("Grinding Wheel"), this);
    auto* wheelForm = new QFormLayout(wheelGroup);

    m_comboProfile = new QComboBox(wheelGroup);
    m_comboProfile->addItem(tr("1A1 - corner radii"),
                            static_cast<int>(WheelProfileType::A1A1));
    m_comboProfile->addItem(tr("1V1 - beveled edge"),
                            static_cast<int>(WheelProfileType::A1V1));
    m_comboProfile->setCurrentIndex(
        wheelDefaults.profile == WheelProfileType::A1V1 ? 1 : 0);

    m_spinGR = MakeSpinBox(0.001, 5000.0, wheelDefaults.gR, tr(" mm"),
                           4, 0.1, wheelGroup);
    m_spinGr1 = MakeSpinBox(0.0, 1000.0, wheelDefaults.gr1, tr(" mm"),
                            4, 0.01, wheelGroup);
    m_spinGr2 = MakeSpinBox(0.0, 1000.0, wheelDefaults.gr2, tr(" mm"),
                            4, 0.01, wheelGroup);
    m_spinWidth = MakeSpinBox(0.001, 5000.0, wheelDefaults.width, tr(" mm"),
                              4, 0.1, wheelGroup);
    m_spinBevelAngle = MakeSpinBox(1.0, 89.0, wheelDefaults.bevelAngleDeg,
                                   tr(" deg"), 3, 1.0, wheelGroup);

    wheelForm->addRow(tr("Profile"), m_comboProfile);
    wheelForm->addRow(tr("gR (large-end radius)"), m_spinGR);
    wheelForm->addRow(tr("gr1 (large-end corner)"), m_spinGr1);
    wheelForm->addRow(tr("gr2 (small-end corner)"), m_spinGr2);
    wheelForm->addRow(tr("Width"), m_spinWidth);
    wheelForm->addRow(tr("Bevel angle (1V1)"), m_spinBevelAngle);
    mainLayout->addWidget(wheelGroup);

    auto* cutterGroup = new QGroupBox(tr("Cutter (optional)"), this);
    auto* cutterForm = new QFormLayout(cutterGroup);

    m_spinCuttingEdgeLength = MakeSpinBox(
        0.001, 10000.0, cutterDefaults.cuttingEdgeLength, tr(" mm"), 4, 0.1,
        cutterGroup);
    m_spinHelixAngle = MakeSpinBox(0.0, 89.0, cutterDefaults.helixAngleDeg,
                                   tr(" deg"), 3, 0.5, cutterGroup);
    m_spinCutterRadius = MakeSpinBox(0.001, 1000.0, cutterDefaults.radius,
                                     tr(" mm"), 4, 0.1, cutterGroup);
    m_spinCoreRadius = MakeSpinBox(0.0, 1000.0, cutterDefaults.coreRadius,
                                   tr(" mm"), 4, 0.01, cutterGroup);
    m_spinSlotAngle = MakeSpinBox(0.0, 180.0, cutterDefaults.slotAngleDeg,
                                  tr(" deg"), 3, 0.5, cutterGroup);
    m_spinRadialRakeAngle = MakeSpinBox(
        -89.0, 89.0, cutterDefaults.radialRakeAngleDeg, tr(" deg"), 3, 0.5,
        cutterGroup);

    cutterForm->addRow(tr("Cutting edge length"), m_spinCuttingEdgeLength);
    cutterForm->addRow(tr("Helix angle"), m_spinHelixAngle);
    cutterForm->addRow(tr("Cutter radius"), m_spinCutterRadius);
    cutterForm->addRow(tr("Core radius"), m_spinCoreRadius);
    cutterForm->addRow(tr("Slot angle"), m_spinSlotAngle);
    cutterForm->addRow(tr("Radial rake angle"), m_spinRadialRakeAngle);
    mainLayout->addWidget(cutterGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_comboProfile, &QComboBox::currentIndexChanged, this,
            [this](int) { UpdateProfileControlState(); });
    UpdateProfileControlState();
}

void WheelParamDialog::UpdateProfileControlState()
{
    const bool isBeveled =
        m_comboProfile->currentData().toInt() ==
        static_cast<int>(WheelProfileType::A1V1);
    m_spinBevelAngle->setEnabled(isBeveled);
}

WheelParamValues WheelParamDialog::GetWheelParams() const
{
    WheelParamValues values;
    values.profile = m_comboProfile->currentData().toInt() ==
                             static_cast<int>(WheelProfileType::A1V1)
                         ? WheelProfileType::A1V1
                         : WheelProfileType::A1A1;
    values.gR = m_spinGR->value();
    values.gr1 = m_spinGr1->value();
    values.gr2 = m_spinGr2->value();
    values.width = m_spinWidth->value();
    values.bevelAngleDeg = m_spinBevelAngle->value();
    return values;
}

CutterParamValues WheelParamDialog::GetCutterParams() const
{
    CutterParamValues values;
    values.cuttingEdgeLength = m_spinCuttingEdgeLength->value();
    values.helixAngleDeg = m_spinHelixAngle->value();
    values.radius = m_spinCutterRadius->value();
    values.coreRadius = m_spinCoreRadius->value();
    values.slotAngleDeg = m_spinSlotAngle->value();
    values.radialRakeAngleDeg = m_spinRadialRakeAngle->value();
    return values;
}
