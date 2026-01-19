#pragma once

#include <QDialog>
#include <QLabel>

#include <memory>

#include "entities\GrindingWheel.h"
#include "entities\Blank.h"

class SliceView;
class QTimer;
class GrindingWheel;
class Blank;

class Simulation2DDialog : public QDialog
{
    Q_OBJECT

public:
    Simulation2DDialog(lve::LveDevice& device, QWidget* parent = nullptr);
    ~Simulation2DDialog();

    void UpdateEntitiesData(const GrindingWheel& grndWheel, const Blank& blank,
        const std::vector<lve::InstanceData>& grndWheelInstances);
    void BuildContactMask();

private:
    std::unique_ptr<SliceView> m_sliceView = nullptr;
    QTimer* m_renderTimer = nullptr;
    QWidget* m_renderWidget = nullptr;
    QTimer* m_resizeTimer = nullptr;    // 防抖定时器

    const Blank* m_blank = nullptr;
    const GrindingWheel* m_grndWheel = nullptr;
    std::vector<lve::InstanceData> m_grndWheelInstances;

    double m_viewHalfSize{ 70. };

    glm::vec3 m_normal{ 1., 0., 0. };   // 截平面的法向

private:
    void InitSliceView(lve::LveDevice& device, void* hwnd, void* hinstance);
    SliceViewConfig UpdateView();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* e) override;
    void wheelEvent(QWheelEvent* event) override;

signals:
    void OpenToolPathSignal();
};