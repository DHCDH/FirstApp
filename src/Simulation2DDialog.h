#pragma once

#include <QDialog>
#include <QLabel>
#include <memory>
#include <filesystem>

#include "entities\Blank.h"
#include "entities\GrindingWheel.h"
#include "optimize/GrindingWheelPoseOptimizer.h"
#include "optimize/OptimizeResourceContext.h"
#include "optimize/OptimizeMaskRenderSystem.h"
#include "optimize/OptimizePoseRenderSystem.h"

class SliceView;
class QTimer;
class QLineEdit;
class QLabel;

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
    std::unique_ptr<SliceView> m_sliceView = nullptr;
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

    std::unique_ptr<optimize::OptimizeResourceContext> m_optContext;
    std::unique_ptr<optimize::OptimizeMaskRenderSystem> m_optMaskSystem;

    /*刀轨*/
    std::vector<ToolPath> m_toolpaths;

private:
    void InitSliceView(lve::LveDevice& device, void* hwnd, void* hinstance);
    SliceViewConfig UpdateView();

    Plane FetchDisplayPlane();

    void OptimizeGrindingWheelPose();

    int ReadToolPath(std::filesystem::path path);
    void UpdateToolPath(std::filesystem::path path);
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