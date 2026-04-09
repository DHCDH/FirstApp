#include "Simulation2DDialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <fstream>

#include "algorithm/DualNURBSCurveInterpolator.h"
#include "optimize/OptimizeGlobalConfig.h"
#include "slice/SliceView.h"

const std::filesystem::path DEFAULT_TOOL_PATH =
    "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\optimize_toolpath.txt";

Simulation2DDialog::Simulation2DDialog(lve::LveDevice& device, QWidget* parent)
    : QDialog(parent),
      m_renderWidget(new QWidget(this)),
      m_renderTimer(new QTimer(this)),
      m_lveDevice(device)
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

    QPushButton* btnToolPath = new QPushButton("Tool Path", this);
    QCheckBox* checkDisplayWireframe = new QCheckBox("Display Wireframe", this);
    checkDisplayWireframe->setChecked(true);
    m_editDiameter = new QLineEdit("10.", this);
    m_editSliceNum = new QLineEdit("1", this);
    m_editPoint = new QLineEdit("0.0, 0.0, 0.0", this);
    m_editNormal = new QLineEdit("1.0, 0.0, 0.0", this);
    QPushButton* btnDisplayOnly = new QPushButton("Display", this);
    QPushButton* btnDisplayAndAnalysis = new QPushButton("Display&&Calculate", this);
    QPushButton* btnOptimize = new QPushButton("Optimize", this);

    controlLayout->addWidget(btnToolPath);
    controlLayout->addWidget(checkDisplayWireframe);
    controlLayout->addWidget(new QLabel("Diameter", this));
    controlLayout->addWidget(m_editDiameter);
    controlLayout->addWidget(new QLabel("Number of Slices", this));
    controlLayout->addWidget(m_editSliceNum);
    controlLayout->addWidget(new QLabel("Point", this));
    controlLayout->addWidget(m_editPoint);
    controlLayout->addWidget(new QLabel("Normal", this));
    controlLayout->addWidget(m_editNormal);
    controlLayout->addWidget(btnDisplayOnly);
    controlLayout->addWidget(btnDisplayAndAnalysis);
    controlLayout->addWidget(btnOptimize);
    controlLayout->addStretch();

    /*初始化防抖定时器*/
    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);  // 只触发一次
    m_resizeTimer->setInterval(100);     // 延迟100ms

    // m_renderWidget->resize(720, 480);
    m_renderWidget->setAttribute(Qt::WA_PaintOnScreen);
    m_renderWidget->setAttribute(Qt::WA_NativeWindow);
    // 禁止Qt自动绘制背景，防止失去焦点时画面被刷黑
    m_renderWidget->setAttribute(Qt::WA_NoSystemBackground);
    m_renderWidget->setAttribute(Qt::WA_OpaquePaintEvent);
    m_renderWidget->winId();
    void* hwnd = reinterpret_cast<void*>(m_renderWidget->winId());
    void* hinstance = GetModuleHandle(nullptr);

    InitSliceView(device, hwnd, hinstance);

    // connect(m_renderTimer, &QTimer::timeout, [this]() { BuildContactMask(); });
    // m_renderTimer->start(16);

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

    connect(btnDisplayOnly, &QPushButton::clicked, this, [this]() {
        m_sliceView->SetRunningMode(RunningMode::DISPLAY_ONLY);
        m_runningMode = RunningMode::DISPLAY_ONLY;
        BuildContactMask();
    });

    connect(btnDisplayAndAnalysis, &QPushButton::clicked, this, [this]() {
        this->setProperty("isFullAnalysis", true);
        m_sliceView->SetRunningMode(RunningMode::DISPLAY_AND_ANALYZE);
        m_runningMode = RunningMode::DISPLAY_AND_ANALYZE;
        BuildContactMask();
    });

    // --- 为输入框创建一个400毫秒的防抖定时器 ---
    QTimer* inputTimer = new QTimer(this);
    inputTimer->setSingleShot(true);
    inputTimer->setInterval(400);

    connect(m_editPoint, &QLineEdit::textChanged, [inputTimer]() {
        inputTimer->start();
    });
    connect(m_editNormal, &QLineEdit::textChanged, [inputTimer]() {
        inputTimer->start();
    });
    connect(inputTimer, &QTimer::timeout, [this]() {
        std::cout << "plane change"
                  << "\n";
        this->setProperty("isFullAnalysis", false);  // 标记为单截面
        m_sliceView->SetRunningMode(RunningMode::DISPLAY_AND_ANALYZE);
        m_runningMode = RunningMode::DISPLAY_AND_ANALYZE;
        BuildContactMask();
    });

    connect(btnOptimize, &QPushButton::clicked, this, [this]() {
        OptimizeGrindingWheelPose();
    });

    connect(btnToolPath, &QPushButton::clicked, this, [this]() {
        const QString qPath = QFileDialog::getOpenFileName(
            this,
            tr("选择 toolpath 文件"),
            QString("D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff"),
            tr("Toolpath Files (*.txt);;All Files (*.*)"));
        if (qPath.isEmpty())
            QMessageBox::critical(this, tr("警告"), tr("输入正确刀轨文件"));

        std::filesystem::path path = std::filesystem::u8path(qPath.toUtf8().constData());
        UpdateToolPath(path);
        UpdateGrindingWheelInstances();
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
                                              m_renderWidget->width(),
                                              m_renderWidget->height(),
                                              "2D Simulation");
}

void Simulation2DDialog::UpdateEntitiesData(
    const GrindingWheel& grndWheel, const Blank& blank,
    const std::vector<glm::mat4>& grndWheelInstances)
{
    m_grndWheel = &grndWheel;
    m_blank = &blank;
    m_grndWheelInstances = grndWheelInstances;
}

void Simulation2DDialog::BuildContactMask()
{
    bool wasAnalysisRequested = (m_runningMode == RunningMode::DISPLAY_AND_ANALYZE);

    if (wasAnalysisRequested) {
        // 不管是全量还是单截面分析，先把雷达拉回到刚好能包住整根棒料的范围！
        // 这样既不会让新截面跑出视野，也不会因为视野太大(400)导致小特征丢失。
        m_viewCenter = glm::vec2(0.0f, 0.0f);
        m_viewHalfSize =
            m_editDiameter->text().toDouble() *
            1.5f;  // <--- 请根据你实际棒料的半径调整，20.0f 是个非常安全的推荐值
    }

    m_sliceView->UpdateSliceViewConfig(UpdateView());

    m_sliceView->UpdateSliceViewConfig(UpdateView());
    m_sliceView->SetModel(m_blank->GetModel(), m_grndWheel->GetModel());

    // 将分辨率和点数实时显示在标题栏
    SliceViewConfig currentConfig = UpdateView();
    QString title = QString("2D Simulation | Res: %1x%2")
                        .arg(currentConfig.nX)
                        .arg(currentConfig.nZ);  // 假设你给 SliceView 加了获取点数的接口
    this->setWindowTitle(title);

    if (m_grndWheelInstances.empty()) {
        emit OpenToolPathSignal();
    }

    // ---准备帧数据 ---
    SliceFrameData frameData{};
    frameData.displayPlane = FetchDisplayPlane();
    if (m_runningMode == RunningMode::DISPLAY_AND_ANALYZE) {
        bool isFullAnalysis = this->property("isFullAnalysis").toBool();
        if (isFullAnalysis) {
            int n = m_editSliceNum->text().toDouble();
            float step = 30. / (float)n;
            for (int i = 0; i <= n; i++) {
                Plane pln{{1.f, 0.f, 0.f}, {step * i, 0.f, 0.f}};
                frameData.planes.emplace_back(pln);
            }
        }
        m_runningMode = RunningMode::DISPLAY_ONLY;
    }
    // 需要显示的截面永远放在数组最后一位
    frameData.planes.push_back(frameData.displayPlane);
    frameData.blankMatrix = glm::mat4(1.f);
    frameData.wheelMatrixes.reserve(m_grndWheelInstances.size());
    for (const auto& instance : m_grndWheelInstances) {
        frameData.wheelMatrixes.push_back(instance);
    }

    /*执行GPU计算*/
    m_sliceView->BuildContactMask(frameData);

    // --- 把 GPU 的微观视野反向同步给 UI 的鼠标控制器 ---
    if (wasAnalysisRequested) {
        auto microConfigs = m_sliceView->GetLastMicroConfigs();
        if (!microConfigs.empty()) {
            // --- 避免对焦到可能没有交集的displayPlane

            auto micro = microConfigs.back();

            // 同步相机的物理中心点到切削交集处
            m_viewCenter.x = (micro.xMin + micro.xMax) * 0.5f;
            m_viewCenter.y = (micro.zMin + micro.zMax) * 0.5f;

            // 同步鼠标的缩放倍率 (m_viewHalfSize) 到显微镜级别
            float aspectRatio =
                static_cast<float>(width()) / static_cast<float>(height());
            if (aspectRatio > 1.0f) {
                m_viewHalfSize = std::abs(micro.zMax - micro.zMin) * 0.5f;
            } else {
                m_viewHalfSize = std::abs(micro.xMax - micro.xMin) * 0.5f;
            }
        }
    }
}

SliceViewConfig Simulation2DDialog::UpdateView()
{
    double w = m_renderWidget->width();
    double h = m_renderWidget->height();
    /*计算宽高比*/
    float aspectRatio = static_cast<float>(w) / static_cast<float>(h);

    /*配置视图*/
    SliceViewConfig config{};
#if 1
    // 采样倍率，被率越高，Solid边缘越平滑，图形越精确，显存和性能开销越大
    constexpr float renderScale = 1.f;
    /*分辨率 pixels*/
    config.nX = static_cast<uint32_t>(w * renderScale);
    config.nZ = static_cast<uint32_t>(h * renderScale);
#else
    // 固定分辨率
    const uint32_t FIXED_RES = 2048u;
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

Plane Simulation2DDialog::FetchDisplayPlane()
{
    QString inputP = m_editPoint->text();
    QString inputN = m_editNormal->text();

    auto fetch_value = [](QString input) {
        static QRegularExpression re(
            R"((-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?))");
        QRegularExpressionMatch match = re.match(input);

        if (match.hasMatch()) {
            bool okX, okY, okZ;
            double x = match.captured(1).toDouble(&okX);
            double y = match.captured(2).toDouble(&okY);
            double z = match.captured(3).toDouble(&okZ);

            if (okX && okY && okZ) {
            } else {
                std::cerr << "bu shi ge men"
                          << "\n";
            }

            return glm::vec3{x, y, z};
        }
        return glm::vec3{0, 0, 0};
    };

    return {fetch_value(inputN), fetch_value(inputP)};
}

void Simulation2DDialog::OptimizeGrindingWheelPose()
{
    std::cout << "Enter function: " << __FUNCTION__ << "\n";

    this->setProperty("isOptimization", true);
    m_sliceView->SetRunningMode(RunningMode::OPTIMIZE);
    m_runningMode = RunningMode::OPTIMIZE;

    if (!m_blank || !m_grndWheel) {
        throw std::runtime_error("Grinding wheel or blank not set");
    }

    if (!m_optContext) {
        std::cout << "[System] Initializing Optimizer Core for the first time... (May "
                     "cause a slight stutter)"
                  << "\n";

        uint32_t texWidth = 1024;
        uint32_t texHeight = 1024;

        m_optContext = std::make_unique<optimize::OptimizeResourceContext>(m_lveDevice,
                                                                           texWidth,
                                                                           texHeight);

        m_optContext->SetGrindingWheelParameters(
            {.gR{50.},
             .gr1{0.1},
             .gr2{0.1},
             .width{10.},
             .radius{[r1 = 50., gr1 = 0.1, width = 10., gr2 = 0.1](double u0) {
                  //std::cout << "[GrindingWheel radius] r1: " << r1 << " gr1: " << gr1
                  //         << "\n";
                 if (u0 < gr1) {
                     return r1 - gr1 + std::sqrt(gr1 * gr1 - (gr1 - u0) * (gr1 - u0));
                 } else if (u0 > (width - gr1) && u0 <= width) {
                     return r1 - gr2 +
                            std::sqrt(gr2 * gr2 -
                                      (u0 - (width - gr2)) * (u0 - (width - gr2)));
                 }
                 return r1;
             }},
             .radiusDeriv{[r1 = 50., gr1 = 0.1, width = 10., gr2 = 0.1](double u0) {
                 if (u0 < gr1) {
                     return (gr1 - u0) / std::sqrt(gr1 * gr1 - (gr1 - u0) * (gr1 - u0));
                 } else if (u0 > (width - gr1) && u0 <= width) {
                     return (width - gr2 - u0) /
                            std::sqrt(gr2 * gr2 -
                                      (u0 - width + gr2) * (u0 - width + gr2));
                 }
                 return 0.;
             }}});
        m_optContext->SetCutterParameters(
            {.cuttingEdgeLength{30.},
             .helixAngle{[](double u1) { return 30.; }},
             .radius{[](double u1) { return 6.; }},
             .radiusDeriv{[](double u1) { return 0.; }},
             .coreRadius{[](double u1) { return 3.; }},
             .slotAngle{[](double u1) { return 50.; }},
             .radialRakeAngle{[](double u1) { return 10.; }}});

        m_optMaskSystem = std::make_unique<optimize::OptimizeMaskRenderSystem>(
            m_lveDevice,
            m_optContext->GetContourComputeSetLayout());
    }

    optimize::GrindingWheelPoseOptimizer optimizer(m_lveDevice,
                                                   *m_blank->GetModel(),
                                                   *m_grndWheel->GetModel());

    SliceViewConfig macroConfig{};
    float radius = m_optContext->GetCutterParameters().radius(0.);
    macroConfig.xMin = -radius * 1.1f;
    macroConfig.xMax = radius * 1.1f;
    macroConfig.zMin = -radius * 1.1f;
    macroConfig.zMax = radius * 1.1f;
    macroConfig.nX = 1024;
    macroConfig.nZ = 1024;

    Plane plane{{1.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};

    // 调整砂轮实例数量
    optimize::BatchedWheelPushConstants pushData{};
    pushData.normal = plane.normal;
    pushData.point = plane.point;
    pushData.stepX = 0.4f;
    double helixAngle =
        glm::radians<double>(m_optContext->GetCutterParameters().helixAngle(0.));
    pushData.tanHelixAngle = static_cast<float>(tan(helixAngle));
    pushData.radius = m_optContext->GetCutterParameters().radius(0.);
    pushData.stepsPerPose = 13;

    optimizer.InitializeDataForPSO(*m_optContext);
    optimizer.RunOptimization(*m_optContext,
                              *m_optMaskSystem,
                              macroConfig,
                              plane,
                              pushData);
    optimizer.ReadBackBestResult(*m_optContext);
    optimizer.WriteToolPath(*m_optContext);

    glm::mat4 bestPose = optimizer.GetBestPose();
    float bestScore = optimizer.GetBestScore();

    UpdateToolPath(DEFAULT_TOOL_PATH);
    UpdateGrindingWheelInstances();
}

int Simulation2DDialog::ReadToolPath(std::filesystem::path path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open file: " << path.string() << "\n";
        return -1;
    }

    m_toolpaths.clear();

    ToolPath current;
    bool inSeg = false;  // 读取到new seg才开始收集

    auto flushSeg = [&]() {
        if (!current.points.empty() || !current.normals.empty()) {
            if (current.points.size() != current.normals.size()) {
                std::cerr << "points/normals size mismatch in a segment"
                          << "\n";
                current = ToolPath{};
                return;
            }

            current.size = current.points.size();
            m_toolpaths.emplace_back(current);
            current = ToolPath();
        }
        return;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) continue;

        if (line == "new seg") {
            flushSeg();
            inSeg = true;
            continue;
        }

        if (!inSeg) continue;

        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);

        float x, y, z, nx, ny, nz;
        if (!(iss >> x >> y >> z >> nx >> ny >> nz)) {
            std::cerr << "Warning: bad data line (skip): " << line << "\n";
            continue;  // 或者 return -2;
        }

        current.points.push_back({x, y, z});
        current.normals.push_back({nx, ny, nz});
    }

    flushSeg();

// 插值
#if 1
    for (int i = 0; i < m_toolpaths.size(); i++) {
        m_toolpaths[i] = DualNURBSCurveInterpolator::Interpolate(m_toolpaths[i], 0.1);

        std::cout << "toolpath[" << i << "].size: " << m_toolpaths[i].size << " \n";
    }
#endif

    std::cout << "Parse tool path succeed: " << path.string() << "\n";

    return 0;
}

void Simulation2DDialog::UpdateToolPath(std::filesystem::path path)
{
    m_toolpaths.clear();
    ReadToolPath(path);
}

void Simulation2DDialog::UpdateGrindingWheelInstances()
{
    m_grndWheelInstances.clear();
    m_grndWheel->CalculateGrindingWheelInstances(m_grndWheelInstances, m_toolpaths);
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
    float aspectRatio = static_cast<float>(m_renderWidget->width()) /
                        static_cast<float>(m_renderWidget->height());
    float pixelToWorldScale = 0.0f;

    // 根据 UpdateView 的逻辑：
    // 如果宽 > 高 (aspect > 1)，m_viewHalfSize 对应高度的一半 (Z轴)
    // 如果宽 < 高 (aspect < 1)，m_viewHalfSize 对应宽度的一半 (X轴)
    if (aspectRatio > 1.0f) {
        // 高度对应 2 * m_viewHalfSize
        pixelToWorldScale =
            (m_viewHalfSize * 2.0f) / static_cast<float>(m_renderWidget->height());
    } else {
        // 宽度对应 2 * m_viewHalfSize
        pixelToWorldScale =
            (m_viewHalfSize * 2.0f) / static_cast<float>(m_renderWidget->width());
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