#include "Simulation2DDialog.h"

#include <QHBoxLayout>
#include <QTimer>
#include <QWheelEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QCheckBox>

#include "SliceView.h"

Simulation2DDialog::Simulation2DDialog(lve::LveDevice& device, QWidget* parent)
    : QDialog(parent), m_renderWidget(new QWidget(this)), m_renderTimer(new QTimer(this))
{
    this->setWindowTitle("2D Simulation");
    this->resize(1920, 1080);

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_renderWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_renderWidget, 1);

    QVBoxLayout* controlLayout = new QVBoxLayout();
    controlLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->addLayout(controlLayout);
    QCheckBox* checkDisplayWireframe = new QCheckBox("Display Wireframe", this);
    checkDisplayWireframe->setChecked(true);
    QPushButton* btnFetchContour = new QPushButton("Fetch Contour", this);
    controlLayout->addWidget(checkDisplayWireframe);
    controlLayout->addWidget(btnFetchContour);

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

    connect(checkDisplayWireframe, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::Checked) {
            m_sliceView->SetDisplayWireframe(true);
        } else {
            m_sliceView->SetDisplayWireframe(false);
        }
    });

    connect(btnFetchContour, &QPushButton::clicked, this, [this]() {
        m_sliceView->SetFetchContour();
        BuildContactMask();
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
    m_grndWheel = &grndWheel;
    m_blank = &blank;
    m_grndWheelInstances = grndWheelInstances;
}

void Simulation2DDialog::BuildContactMask()
{
    m_sliceView->UpdateSliceViewConfig(UpdateView());
    m_sliceView->SetBlankModel(m_blank->GetModel());
    m_sliceView->SetGrindingWheelModel(m_grndWheel->GetModel());

    // 将分辨率和点数实时显示在标题栏
    SliceViewConfig currentConfig = UpdateView();
    QString title =
        QString("2D Simulation | Res: %1x%2")
            .arg(currentConfig.nX)
            .arg(currentConfig.nZ);  // 假设你给 SliceView 加了获取点数的接口
    this->setWindowTitle(title);

    if (m_grndWheelInstances.empty()) {
        emit OpenToolPathSignal();
    }

    /*准备帧数据*/
    SliceFrameData frameData{};
    frameData.normal = {1.f, 0.f, 0.f};
    frameData.point = {0.f, 0.f, 0.f};
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
    #if 0
    // 采样倍率，被率越高，Solid边缘越平滑，图形越精确，显存和性能开销越大
    constexpr float renderScale = 2.f;
    /*分辨率 pixels*/
    config.nX = static_cast<uint32_t>(w * renderScale);
    config.nZ = static_cast<uint32_t>(h * renderScale);
    #else
    // 固定分辨率
    const uint32_t FIXED_RES = 4096u;
    config.nX = FIXED_RES;
    config.nZ = FIXED_RES;
    #endif

    float xHalf, zHalf;

    /*根据比例修正视野范围*/
    if (aspectRatio > 1.f) {
        zHalf = m_viewHalfSize;
        xHalf = m_viewHalfSize * aspectRatio;
    } else {
        xHalf = m_viewHalfSize;
        zHalf = m_viewHalfSize / aspectRatio;
    }

    // 【修改点】应用 m_viewCenter 偏移
    config.xMin = m_viewCenter.x - xHalf;
    config.xMax = m_viewCenter.x + xHalf;
    config.zMin = m_viewCenter.y - zHalf;
    config.zMax = m_viewCenter.y + zHalf;

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
        // m_resizeTimer->start();
        BuildContactMask();
    }
}

void Simulation2DDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_isDragging = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);  // 改变光标形状提示用户
    }
    QDialog::mousePressEvent(event);
}

// 实现鼠标移动事件 (核心逻辑)
void Simulation2DDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_isDragging) {
        QDialog::mouseMoveEvent(event);
        return;
    }

    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    // --- 计算 像素 -> 世界坐标 的缩放比例 ---
    // 这必须与 UpdateView 中的逻辑一致
    float aspectRatio = static_cast<float>(width()) / static_cast<float>(height());
    float pixelToWorldScale = 0.0f;
    
    // 根据 UpdateView 的逻辑：
    // 如果宽 > 高 (aspect > 1)，m_viewHalfSize 对应高度的一半 (Z轴)
    // 如果宽 < 高 (aspect < 1)，m_viewHalfSize 对应宽度的一半 (X轴)
    if (aspectRatio > 1.0f) {
        // 高度对应 2 * m_viewHalfSize
        pixelToWorldScale = (m_viewHalfSize * 2.0f) / static_cast<float>(height());
    } else {
        // 宽度对应 2 * m_viewHalfSize
        pixelToWorldScale = (m_viewHalfSize * 2.0f) / static_cast<float>(width());
    }

    // --- 更新视图中心 ---
    // 注意方向：鼠标向右移(x+)，我们要看左边的物体，相当于摄像机向左移(center x-)
    // 或者理解为：拖动纸张。鼠标向右，视野中心向左。
    // 通常符合直觉的是：鼠标向右，画面向右平移 -> 摄像机向左移。
    // 这里的符号取决于你的坐标系定义。
    // 假设：X轴向右为正，Z轴(或Y)向上为正。
    // Qt屏幕坐标：X向右，Y向下。

    m_viewCenter.x -= delta.x() * pixelToWorldScale;

    // Qt Y向下，世界 Y(Z) 向上。
    // 鼠标向下(dy > 0)，希望画面往下移(看上面的物体)，摄像机向上移(center y+)
    m_viewCenter.y += delta.y() * pixelToWorldScale;

    // 触发更新
    if (m_grndWheel && m_blank) {
        // 使用防抖 timer 或者直接调用 BuildContactMask
        // 为了流畅度，拖拽时建议直接调用，或者使用极短的timer
        // 这里直接复用 resizeTimer 的逻辑，或者直接调用 BuildContactMask()
        BuildContactMask();
    }
}

// 实现鼠标释放事件
void Simulation2DDialog::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_isDragging = false;
        setCursor(Qt::ArrowCursor);  // 恢复光标
    }
    QDialog::mouseReleaseEvent(event);
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