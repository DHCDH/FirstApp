#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

// 砂轮型面类型：
//   1A1 - 两端为圆角的平形砂轮
//   1V1 - 带斜角的砂轮
enum class WheelProfileType {
    A1A1,
    A1V1,
};

// 砂轮几何参数（单位：mm / deg）
struct WheelParamValues {
    WheelProfileType profile{WheelProfileType::A1V1};
    double gR{50.0};          // 大端圆半径
    double gr1{0.5};          // 大端圆角
    double gr2{0.5};          // 小端圆角
    double width{10.0};       // 砂轮宽度
    double bevelAngleDeg{60.0};  // 1V1 斜角，1A1 不使用
};

// 刀具参数（单位：mm / deg）
struct CutterParamValues {
    double cuttingEdgeLength{30.0};
    double helixAngleDeg{30.0};
    double radius{4.0};
    double coreRadius{2.4};
    double slotAngleDeg{60.0};
    double radialRakeAngleDeg{16.5};
};

class WheelParamDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WheelParamDialog(const WheelParamValues& wheelDefaults,
                              const CutterParamValues& cutterDefaults,
                              QWidget* parent = nullptr);

    WheelParamValues GetWheelParams() const;
    CutterParamValues GetCutterParams() const;

private:
    void UpdateProfileControlState();

    QComboBox* m_comboProfile = nullptr;
    QDoubleSpinBox* m_spinGR = nullptr;
    QDoubleSpinBox* m_spinGr1 = nullptr;
    QDoubleSpinBox* m_spinGr2 = nullptr;
    QDoubleSpinBox* m_spinWidth = nullptr;
    QDoubleSpinBox* m_spinBevelAngle = nullptr;

    QDoubleSpinBox* m_spinCuttingEdgeLength = nullptr;
    QDoubleSpinBox* m_spinHelixAngle = nullptr;
    QDoubleSpinBox* m_spinCutterRadius = nullptr;
    QDoubleSpinBox* m_spinCoreRadius = nullptr;
    QDoubleSpinBox* m_spinSlotAngle = nullptr;
    QDoubleSpinBox* m_spinRadialRakeAngle = nullptr;
};
