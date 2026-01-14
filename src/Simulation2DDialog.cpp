#include "Simulation2DDialog.h"

#include <QHBoxLayout>
#include <QTimer>
#include <QWheelEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "SliceView.h"

Simulation2DDialog::Simulation2DDialog(lve::LveDevice& device, QWidget* parent)
    : QDialog(parent), m_renderWidget(new QWidget(this)), m_renderTimer(new QTimer(this))
{
    this->setWindowTitle("2D Simulation");
    this->resize(720, 480);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_renderWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_renderWidget, 1);

    QVBoxLayout* controlLayout = new QVBoxLayout();
    controlLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->addLayout(controlLayout);
    QPushButton* btnDisplayMode = new QPushButton("WireFrame/Shaded");
    controlLayout->addWidget(btnDisplayMode);

    /*初始化防抖定时器*/
    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);  // 只触发一次
    m_resizeTimer->setInterval(100);     // 延迟100ms

    //m_renderWidget->resize(720, 480);
    m_renderWidget->setAttribute(Qt::WA_PaintOnScreen);
    m_renderWidget->setAttribute(Qt::WA_NativeWindow);
    m_renderWidget->winId();
    void* hwnd = reinterpret_cast<void*>(m_renderWidget->winId());
    void* hinstance = GetModuleHandle(nullptr);

    InitSliceView(device, hwnd, hinstance);

    connect(m_renderTimer, &QTimer::timeout, [this]() { BuildContactMask(); });
    m_renderTimer->start(16);

    connect(m_resizeTimer, &QTimer::timeout, [this]() {
        if (m_grndWheel && m_blank) {
            BuildContactMask();
        }
    });
}

void Simulation2DDialog::InitSliceView(lve::LveDevice& device, void* hwnd,
                                       void* hinstance)
{
    SliceViewConfig config = UpdateView();

    m_sliceView = std::make_unique<SliceView>(device,
                                              config,
                                              hwnd,
                                              hinstance,
                                              width(),
                                              height(),
                                              "2D Simulation");
}

void Simulation2DDialog::UpdateEntitiesData(
    const GrindingWheel& grndWheel, const Blank& blank,
    const std::vector<lve::InstanceData>& grndWheelInstances)
{
    std::cout << "-----Update entities data."
              << "\n";
    m_grndWheel = &grndWheel;
    m_blank = &blank;
    m_grndWheelInstances = grndWheelInstances;
}

void Simulation2DDialog::BuildContactMask()
{
    m_sliceView->UpdateSliceViewConfig(UpdateView());
    m_sliceView->SetBlankModel(m_blank->GetModel());
    m_sliceView->SetGrindingWheelModel(m_grndWheel->GetModel());

    if (m_grndWheelInstances.empty()) {
        throw std::runtime_error("Grinding wheel instances are empty.");
    }

    /*准备帧数据*/
    SliceFrameData frameData{};
    frameData.yM = 1.f;
    frameData.thickness = 1.f;
    frameData.blankModel = glm::mat4(1.f);
    frameData.wheelModels.reserve(m_grndWheelInstances.size());
    for (const auto& instance : m_grndWheelInstances) {
        frameData.wheelModels.push_back(instance.modelMatrix);
    }

    /*执行GPU计算*/
    m_sliceView->BuildContactMask(frameData);
}

SliceViewConfig Simulation2DDialog::UpdateView()
{
    double w = this->width();
    double h = this->height();
    /*计算宽高比*/
    float aspectRatio =
        static_cast<float>(this->width()) / static_cast<float>(this->height());

    /*配置视图*/
    SliceViewConfig config{};
    // 采样倍率，被率越高，Solid边缘越平滑，图形越精确，显存和性能开销越大
    constexpr float renderScale = 2.f;
    /*分辨率 pixels*/
    config.nX = static_cast<uint32_t>(w * renderScale);
    config.nZ = static_cast<uint32_t>(h * renderScale);

    /*根据比例修正视野范围*/
    if (aspectRatio > 1.f) {
        config.zMin = -m_viewHalfSize;
        config.zMax = m_viewHalfSize;
        config.xMin = -m_viewHalfSize * aspectRatio;
        config.xMax = m_viewHalfSize * aspectRatio;
    } else {
        config.xMin = -m_viewHalfSize;
        config.xMax = m_viewHalfSize;
        config.zMin = -m_viewHalfSize / aspectRatio;
        config.zMax = m_viewHalfSize / aspectRatio;
    }

    return config;
}

void Simulation2DDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);

    m_resizeTimer->start();

    if (m_sliceView) {
        m_sliceView->GetWindow()->NotifyResized(m_renderWidget->width(),
                                                m_renderWidget->height());
    }
}

void Simulation2DDialog::closeEvent(QCloseEvent* e)
{
    if (m_renderTimer) m_renderTimer->stop();
    QDialog::closeEvent(e);
}

void Simulation2DDialog::wheelEvent(QWheelEvent* event)
{
    QPoint numPixels = event->pixelDelta();
    QPoint numDegrees = event->angleDelta() / 8;

    float steps = 0.f;
    if (!numPixels.isNull()) {
        steps = numPixels.y() / 15.f;
    } else if (!numDegrees.isNull()) {
        steps = numDegrees.y() / 15.f;
    }

    if (steps == 0.f) return;

    float zoomFactor = 1.1f;

    if (steps > 0) {
        m_viewHalfSize /= zoomFactor;
    } else {
        m_viewHalfSize *= zoomFactor;
    }

    if (m_viewHalfSize < 0.1f) m_viewHalfSize = 0.1f;
    if (m_viewHalfSize > 500.0f) m_viewHalfSize = 500.0f;

    if (m_grndWheel && m_blank && !m_grndWheelInstances.empty()) {
        m_resizeTimer->start();
    }
}

Simulation2DDialog::~Simulation2DDialog()
{
    if (m_renderTimer) {
        m_renderTimer->stop();
        disconnect(m_renderTimer, nullptr, this, nullptr);
    }

    if (m_sliceView) {
        /*等待GPU完成所有命令*/
        m_sliceView->WaitIdle();
        m_sliceView.reset();
    }
}