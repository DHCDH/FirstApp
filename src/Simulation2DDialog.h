#pragma once

#include <QDialog>
#include <QLabel>
#include <memory>
#include <filesystem>

#include "entities\Blank.h"
#include "entities\GrindingWheel.h"
#include "slice/SliceView.h"
#include "optimize/GrindingWheelPoseOptimizer.h"
#include "optimize/OptimizeResourceContext.h"
#include "optimize/OptimizeMaskRenderSystem.h"
#include "optimize/OptimizePoseRenderSystem.h"
#include "ui/WheelParamDialog.h"

class QTimer;
class QLineEdit;
class QLabel;

namespace flute {
class FluteAnalyzer;
}

class Simulation2DDialog : public QDialog
{
    Q_OBJECT

public:
    Simulation2DDialog(lve::LveDevice& device, QWidget* parent = nullptr);
    ~Simulation2DDialog();

    void UpdateEntitiesData(const entity::GrindingWheel& grndWheel,
                            const entity::Blank& blank,
                            const std::vector<glm::mat4>& grndWheelInstances);
    void BuildContactMask();

private:
    lve::LveDevice& m_lveDevice;
    std::unique_ptr<slice::SliceView> m_sliceView = nullptr;
    QTimer* m_renderTimer = nullptr;
    QWidget* m_renderWidget = nullptr;
    QTimer* m_resizeTimer = nullptr;  // 防抖定时器
    QLabel* m_labelRes = nullptr;   // 显示分辨率

    const entity::Blank* m_blank = nullptr;
    const entity::GrindingWheel* m_grndWheel = nullptr;
    std::vector<glm::mat4> m_grndWheelInstances;

    double m_viewHalfSize{8.};

    glm::vec3 m_normal{1., 0., 0.};  // 截平面的法向

    RunningMode m_runningMode = RunningMode::DISPLAY_ONLY;

    // 视角控制变量
    glm::vec2 m_viewCenter = {0.0f, 0.0f};  // 当前视角的中心点 (World Space)
    QPoint m_lastMousePos;                  // 上一次鼠标位置 (Screen Space)
    bool m_isDragging = false;              // 是否正在拖拽

    // --- 用户输入控件 ---
    QLineEdit* m_editDiameter;
    QLineEdit* m_editSliceNum;
    QLineEdit* m_editPoint;
    QLineEdit* m_editNormal;

    // --- 镜头控制 ---
    QLabel* m_labelViewInfo;     // 实时显示镜头中心点 + 放大倍数
    QLineEdit* m_editViewCenter; // 输入镜头中心点 "x, y"
    QLineEdit* m_editViewZoom;   // 输入放大倍数

    std::unique_ptr<optimize::OptimizeResourceContext> m_optContext;
    std::unique_ptr<optimize::OptimizeMaskRenderSystem> m_optMaskSystem;

    // 最近一次在 Optimize 对话框里确认的砂轮/刀具参数
    WheelParamValues m_wheelParams;
    CutterParamValues m_cutterParams;

    std::vector<ToolPath> m_rawToolpaths;  // 原始刀轨（插值前，保留用于重新插值）
    std::vector<ToolPath> m_toolpaths;     // 当前使用的刀轨（原始或插值后）

    // --- 刀轨插值控制 ---
    QLabel* m_labelToolPathStats;    // 显示刀轨统计信息
    QLineEdit* m_editInterpStep;     // 插值步长输入

    // --- 螺旋线刀轨生成控制 ---
    QLineEdit* m_editHelixAngle;
    QLineEdit* m_editHelixLength;
    QLineEdit* m_editHelixXStep;

    std::string m_targetFlutePath;  // 磨损反演输入：容屑槽点集
    // 只在「Wear Estimation」按钮触发的那一次 BuildContactMask 里开启反演，
    // 之后拖拽/缩放/改截面都不会再重跑反演
    bool m_wearEstimationRequested = false;

    std::unique_ptr<flute::FluteAnalyzer> m_fluteAnalyzer = nullptr;

private:
    void InitSliceView(lve::LveDevice& device, void* hwnd, void* hinstance);
    SliceViewConfig UpdateView();

    void UpdateViewInfoLabel();
    void ApplyViewInput();

    Plane FetchDisplayPlane();

    optimize::GrindingWheelParameters BuildGrindingWheelParameters(
        const WheelParamValues& values) const;
    optimize::CutterParameters BuildCutterParameters(
        const CutterParamValues& values) const;

    void OptimizeGrindingWheelPose();

    int ReadToolPath(std::filesystem::path path);
    void UpdateToolPath(std::filesystem::path path);
    void ApplyInterpolation();
    void GenerateHelix();
    void AnalyzeFluteFromFile();
    void RunWearEstimation();  // GPU 砂轮磨损反演：选点集 -> 反演 -> 回显，一步完成
    void UpdateGrindingWheelInstances();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* e) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void OpenToolPathSignal();
};
