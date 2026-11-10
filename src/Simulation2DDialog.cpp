#include "Simulation2DDialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>

#include "Logger.h"
#include "RenderDocHelper.h"
#include "algorithm/DualNURBSCurveInterpolator.h"
#include "flute/FluteAnalyzer.h"
#include "optimize/OptimizeGlobalConfig.h"

const std::filesystem::path DEFAULT_TOOL_PATH =
    "D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\optimize_toolpath.txt";

namespace
{
// 控制面板样式：跟随 main.cpp 里的 Fusion 深色调色板（底色 #353535、强调色 #2a82da）
const char* kControlPanelStyle = R"(
QWidget#renderViewport {
    background: #1c1c1c;
    border-right: 1px solid #3c3c3c;
}
QScrollArea#controlScroll {
    background: #2e2e2e;
    border-left: 1px solid #3c3c3c;
}
QWidget#controlPanel {
    background: #2e2e2e;
}
QLabel {
    color: #d6d6d6;
}
QLabel#resChip {
    color: #9ecbff;
    background: #232323;
    border: 1px solid #3c3c3c;
    border-radius: 4px;
    padding: 4px 6px;
}
QLabel#hintLabel {
    color: #9a9a9a;
}
QGroupBox {
    background: #343434;
    border: 1px solid #454545;
    border-radius: 6px;
    margin-top: 16px;
    padding: 8px 8px 10px 8px;
    font-weight: bold;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    top: 2px;
    padding: 0 4px;
    color: #8fc2ff;
}
QLineEdit {
    background: #262626;
    border: 1px solid #454545;
    border-radius: 4px;
    padding: 4px 6px;
    color: #ededed;
    selection-background-color: #2a82da;
}
QLineEdit:focus {
    border: 1px solid #2a82da;
}
QPushButton {
    background: #3d3d3d;
    border: 1px solid #4d4d4d;
    border-radius: 4px;
    padding: 6px 10px;
    color: #ededed;
}
QPushButton:hover {
    background: #4a4a4a;
    border: 1px solid #5a5a5a;
}
QPushButton:pressed {
    background: #2a82da;
    border: 1px solid #2a82da;
}
QPushButton:disabled {
    color: #7a7a7a;
    border: 1px solid #3a3a3a;
}
QPushButton#primaryButton {
    background: #2a82da;
    border: 1px solid #3c92ea;
    color: #ffffff;
    font-weight: bold;
}
QPushButton#primaryButton:hover {
    background: #3a92ea;
}
QPushButton#primaryButton:pressed {
    background: #1f6fbf;
}
QCheckBox {
    color: #d6d6d6;
    spacing: 6px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    border: 1px solid #5a5a5a;
    border-radius: 3px;
    background: #262626;
}
QCheckBox::indicator:checked {
    background: #2a82da;
    border: 1px solid #3c92ea;
}
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #4c4c4c;
    border-radius: 5px;
    min-height: 26px;
}
QScrollBar::handle:vertical:hover {
    background: #5c5c5c;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: transparent;
}
)";
}  // namespace

Simulation2DDialog::Simulation2DDialog(lve::LveDevice& device, QWidget* parent)
    : QDialog(parent),
      m_renderWidget(new QWidget(this)),
      m_renderTimer(new QTimer(this)),
      m_lveDevice(device)
{
    DEBUG("Construct Simulation2DDialog");

    this->setWindowTitle("2D Simulation");
    this->resize(1350, 1080);

    // ================= 布局骨架：左侧渲染区 + 右侧功能分块面板 =================
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左：Vulkan 渲染区（外包一层容器，方便给画面加底色与分隔线）
    m_renderWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_renderWidget->setFixedSize(1024, 1024);
    QWidget* renderViewport = new QWidget(this);
    renderViewport->setObjectName("renderViewport");
    QVBoxLayout* viewportLayout = new QVBoxLayout(renderViewport);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->addWidget(m_renderWidget, 1, Qt::AlignCenter);
    mainLayout->addWidget(renderViewport, 1);

    // 右：可滚动的控制面板（窗口变矮时按钮不会被挤出视野）
    QScrollArea* controlScroll = new QScrollArea(this);
    controlScroll->setObjectName("controlScroll");
    controlScroll->setWidgetResizable(true);
    controlScroll->setFrameShape(QFrame::NoFrame);
    controlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlScroll->setFixedWidth(324);
    QWidget* controlPanel = new QWidget(controlScroll);
    controlPanel->setObjectName("controlPanel");
    QVBoxLayout* controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(10, 10, 10, 10);
    controlLayout->setSpacing(10);
    controlScroll->setWidget(controlPanel);
    mainLayout->addWidget(controlScroll);

    this->setStyleSheet(kControlPanelStyle);

    m_labelRes = new QLabel("Res: 0 x 0", m_renderWidget);
    m_labelRes->setAttribute(Qt::WA_TransparentForMouseEvents);
    QPushButton* btnToolPath = new QPushButton("Tool Path", this);
    QCheckBox* checkDisplayWireframe = new QCheckBox("Display Wireframe", this);
    checkDisplayWireframe->setChecked(true);
    QCheckBox* checkDisplaySubpixelContour =
        new QCheckBox("Display Subpixel Contour", this);
    checkDisplaySubpixelContour->setChecked(true);
    QCheckBox* checkDisplayRawContour = new QCheckBox("Display Raw Contour", this);
    checkDisplayRawContour->setChecked(true);
    QCheckBox* checkDisplayPixelGrid = new QCheckBox("Display Pixel Grid", this);
    checkDisplayPixelGrid->setChecked(false);
    m_editDiameter = new QLineEdit("10.", this);
    m_editSliceNum = new QLineEdit("1", this);
    m_editPoint = new QLineEdit("0.0, 0.0, 0.0", this);
    m_editNormal = new QLineEdit("1.0, 0.0, 0.0", this);
    QPushButton* btnDisplayOnly = new QPushButton("Display", this);
    QPushButton* btnDisplayAndAnalysis = new QPushButton("Display&&Calculate", this);
    QPushButton* btnOptimize = new QPushButton("Optimize", this);
    QPushButton* btnAnalyzeFlute = new QPushButton("Analyze Flute", this);
    // GPU 砂轮磨损反演：一键完成「选容屑槽点集 -> 反演圆角半径 -> 导出轮廓 -> 回显」
    QPushButton* btnWearEstimation = new QPushButton("Wear Estimation", this);
    m_labelViewInfo = new QLabel("Center: (0.00, 0.00) | Zoom: 8.00", this);
    m_editViewCenter = new QLineEdit("0.0, 0.0", this);
    m_editViewZoom = new QLineEdit("8.0", this);
    QPushButton* btnApplyView = new QPushButton("Apply View", this);

    // ================= 功能分块 =================
    // 每块一个 QGroupBox，块内用 QFormLayout 对齐「标签 - 输入框」；按钮按用途成组

    // ---- 1. Toolpath：加载刀轨 / 统计 / 插值 ----
    m_labelToolPathStats = new QLabel("No toolpath loaded", this);
    m_labelToolPathStats->setObjectName("hintLabel");
    m_labelToolPathStats->setWordWrap(true);
    m_editInterpStep = new QLineEdit("0.5", this);
    QPushButton* btnInterpolate = new QPushButton("Interpolate", this);

    QGroupBox* groupToolPath = new QGroupBox("Toolpath", controlPanel);
    QVBoxLayout* toolPathLayout = new QVBoxLayout(groupToolPath);
    toolPathLayout->setSpacing(8);
    btnToolPath->setMinimumHeight(30);
    toolPathLayout->addWidget(btnToolPath);
    toolPathLayout->addWidget(m_labelToolPathStats);
    QFormLayout* interpForm = new QFormLayout();
    interpForm->setSpacing(6);
    interpForm->addRow("Interp X step (mm)", m_editInterpStep);
    toolPathLayout->addLayout(interpForm);
    toolPathLayout->addWidget(btnInterpolate);
    controlLayout->addWidget(groupToolPath);

    // ---- 2. Slice：截面参数 + 显示/计算 ----
    QGroupBox* groupSlice = new QGroupBox("Slice", controlPanel);
    QVBoxLayout* sliceLayout = new QVBoxLayout(groupSlice);
    sliceLayout->setSpacing(8);
    QFormLayout* sliceForm = new QFormLayout();
    sliceForm->setSpacing(6);
    sliceForm->addRow("Diameter", m_editDiameter);
    sliceForm->addRow("Slices", m_editSliceNum);
    sliceForm->addRow("Point", m_editPoint);
    sliceForm->addRow("Normal", m_editNormal);
    sliceLayout->addLayout(sliceForm);
    QHBoxLayout* sliceButtons = new QHBoxLayout();
    sliceButtons->setSpacing(6);
    sliceButtons->addWidget(btnDisplayOnly);
    sliceButtons->addWidget(btnDisplayAndAnalysis);
    sliceLayout->addLayout(sliceButtons);
    controlLayout->addWidget(groupSlice);

    // ---- 3. Display：屏显开关 ----
    QGroupBox* groupDisplay = new QGroupBox("Display", controlPanel);
    QVBoxLayout* displayLayout = new QVBoxLayout(groupDisplay);
    displayLayout->setSpacing(6);
    displayLayout->addWidget(checkDisplayWireframe);
    displayLayout->addWidget(checkDisplaySubpixelContour);
    displayLayout->addWidget(checkDisplayRawContour);
    displayLayout->addWidget(checkDisplayPixelGrid);
    controlLayout->addWidget(groupDisplay);

    // ---- 4. Analysis：磨损反演 / 槽形分析 / 位姿优化 ----
    QGroupBox* groupAnalysis = new QGroupBox("Analysis", controlPanel);
    QVBoxLayout* analysisLayout = new QVBoxLayout(groupAnalysis);
    analysisLayout->setSpacing(8);
    btnWearEstimation->setObjectName("primaryButton");  // 主操作，高亮显示
    btnWearEstimation->setMinimumHeight(34);
    btnWearEstimation->setToolTip(
        "一次完成：选容屑槽点集 -> 反演砂轮圆角半径 -> 导出轮廓 -> 回显");
    analysisLayout->addWidget(btnWearEstimation);
    QHBoxLayout* analysisButtons = new QHBoxLayout();
    analysisButtons->setSpacing(6);
    analysisButtons->addWidget(btnAnalyzeFlute);
    analysisButtons->addWidget(btnOptimize);
    analysisLayout->addLayout(analysisButtons);
    controlLayout->addWidget(groupAnalysis);

    // ---- 5. View：镜头中心与缩放 ----
    QGroupBox* groupView = new QGroupBox("View", controlPanel);
    QVBoxLayout* viewLayout = new QVBoxLayout(groupView);
    viewLayout->setSpacing(8);
    m_labelViewInfo->setObjectName("hintLabel");
    viewLayout->addWidget(m_labelViewInfo);
    QFormLayout* viewForm = new QFormLayout();
    viewForm->setSpacing(6);
    viewForm->addRow("Center (x, y)", m_editViewCenter);
    viewForm->addRow("Zoom (half size)", m_editViewZoom);
    viewLayout->addLayout(viewForm);
    viewLayout->addWidget(btnApplyView);
    controlLayout->addWidget(groupView);

    connect(btnApplyView, &QPushButton::clicked, this, [this]() { ApplyViewInput(); });

    // ---- 6. Helix：螺旋线刀轨生成 ----
    m_editHelixAngle = new QLineEdit("30.0", this);
    m_editHelixLength = new QLineEdit("10.0", this);
    m_editHelixXStep = new QLineEdit("0.5", this);
    QPushButton* btnGenerateHelix = new QPushButton("Generate Helix", this);

    QGroupBox* groupHelix = new QGroupBox("Helix", controlPanel);
    QVBoxLayout* helixLayout = new QVBoxLayout(groupHelix);
    helixLayout->setSpacing(8);
    QFormLayout* helixForm = new QFormLayout();
    helixForm->setSpacing(6);
    helixForm->addRow("Angle (deg)", m_editHelixAngle);
    helixForm->addRow("Length (mm)", m_editHelixLength);
    helixForm->addRow("X step (mm)", m_editHelixXStep);
    helixLayout->addLayout(helixForm);
    helixLayout->addWidget(btnGenerateHelix);
    controlLayout->addWidget(groupHelix);

    // ---- 顶部：分辨率状态条（UpdateView 会实时刷新）----
    m_labelRes->setObjectName("resChip");
    m_labelRes->setAlignment(Qt::AlignCenter);
    controlLayout->insertWidget(0, m_labelRes);

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

    // 把界面的初始勾选状态同步给渲染端。
    // 注意：setChecked() 都在 connect() 之前调用，不会触发 stateChanged，
    // 所以必须在这里显式推一次，否则渲染端会用自己内部的默认值（曾导致
    // Pixel Grid 复选框未勾选、画面却仍画着像素网格）。
    m_sliceView->SetDisplayWireframe(checkDisplayWireframe->isChecked());
    m_sliceView->SetDisplayContour(checkDisplaySubpixelContour->isChecked());
    m_sliceView->SetDisplayRawContour(checkDisplayRawContour->isChecked());
    m_sliceView->SetDisplayGrid(checkDisplayPixelGrid->isChecked());

    // 持续轻量重绘：只重新 present 已算好的离屏遮罩，不重跑实例化求交。
    // 用于窗口失去焦点/被遮挡后画面被 Qt 刷黑时自动恢复。
    connect(m_renderTimer, &QTimer::timeout, this, [this]() {
        if (m_sliceView) {
            m_sliceView->RePresent();
        }
    });
    m_renderTimer->start(16);  // 60 FPS

    connect(m_resizeTimer, &QTimer::timeout, [this]() {
        std::cout << "[Real Widget Size] W: " << m_renderWidget->width()
                  << ", H: " << m_renderWidget->height() << std::endl;

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

    connect(checkDisplaySubpixelContour,
            &QCheckBox::stateChanged,
            this,
            [this](int state) { m_sliceView->SetDisplayContour(state == Qt::Checked); });

    connect(checkDisplayRawContour, &QCheckBox::stateChanged, this, [this](int state) {
        m_sliceView->SetDisplayRawContour(state == Qt::Checked);
    });

    connect(checkDisplayPixelGrid, &QCheckBox::stateChanged, this, [this](int state) {
        m_sliceView->SetDisplayGrid(state == Qt::Checked);
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

    connect(btnAnalyzeFlute, &QPushButton::clicked, this, [this]() {
        AnalyzeFluteFromFile();
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

    connect(btnWearEstimation, &QPushButton::clicked, this, [this]() {
        RunWearEstimation();
    });

    connect(btnInterpolate, &QPushButton::clicked, this, [this]() {
        ApplyInterpolation();
    });

    connect(btnGenerateHelix, &QPushButton::clicked, this, [this]() { GenerateHelix(); });
}

void Simulation2DDialog::InitSliceView(lve::LveDevice& device, void* hwnd,
                                       void* hinstance)
{
    SliceViewConfig config = UpdateView();

    m_sliceView = std::make_unique<slice::SliceView>(device,
                                                     config,
                                                     hwnd,
                                                     hinstance,
                                                     m_renderWidget->width(),
                                                     m_renderWidget->height(),
                                                     "2D Simulation");
}

void Simulation2DDialog::UpdateEntitiesData(
    const entity::GrindingWheel& grndWheel, const entity::Blank& blank,
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
            1.5f;  // <--- 请根据实际棒料的半径调整，20.0f 是个非常安全的推荐值
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
    // 只有 Wear Estimation 按钮会把 m_wearEstimationRequested 置 true，
    // 普通显示/拖拽/缩放都传 false，反演不会混在显示流程里被误触发。
    m_sliceView->BuildContactMask(frameData,
                                  m_wearEstimationRequested,
                                  m_targetFlutePath);

    // 反演只执行一次：清空路径并复位标志，避免拖拽/缩放时反复重跑反演并污染显示相机
    m_wearEstimationRequested = false;
    m_targetFlutePath.clear();

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

    // --- 分辨率随视野缩放, 让每个 texel 的世界尺寸固定 ---
    // 基准: 视野半宽 BASE_HALF_SIZE 时用 FIXED_RES 个 texel。
    // 放大(视野变小) -> texel 数变少 -> 每个方块在屏幕上变大。
    const uint32_t FIXED_RES = 1024;
    const double BASE_HALF_SIZE = 8.0;
    const double texelWorld = (2.0 * BASE_HALF_SIZE) / static_cast<double>(FIXED_RES);

    int nx = static_cast<int>((config.xMax - config.xMin) / texelWorld + 0.5);
    int nz = static_cast<int>((config.zMax - config.zMin) / texelWorld + 0.5);

    // 下限 1 texel; 上限 FIXED_RES(缩小视野时保持用户设定的 256, 不再变密)
    if (nx < 1) nx = 1;
    if (nz < 1) nz = 1;
    if (nx > static_cast<int>(FIXED_RES)) nx = static_cast<int>(FIXED_RES);
    if (nz > static_cast<int>(FIXED_RES)) nz = static_cast<int>(FIXED_RES);

    config.nX = static_cast<uint32_t>(nx);
    config.nZ = static_cast<uint32_t>(nz);

    QString resText = QString("Res: %1 x %2").arg(config.nX).arg(config.nZ);
    m_labelRes->setText(resText);
    m_labelRes->adjustSize();

    UpdateViewInfoLabel();

    return config;
}

void Simulation2DDialog::UpdateViewInfoLabel()
{
    m_labelViewInfo->setText(QString("Center: (%1, %2) | Zoom: %3")
                                 .arg(m_viewCenter.x, 0, 'f', 2)
                                 .arg(m_viewCenter.y, 0, 'f', 2)
                                 .arg(m_viewHalfSize, 0, 'f', 4));
}

void Simulation2DDialog::ApplyViewInput()
{
    static QRegularExpression re(R"((-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?))");
    QRegularExpressionMatch match = re.match(m_editViewCenter->text());
    if (match.hasMatch()) {
        m_viewCenter.x = match.captured(1).toDouble();
        m_viewCenter.y = match.captured(2).toDouble();
    }

    bool ok = false;
    double halfSize = m_editViewZoom->text().toDouble(&ok);
    if (ok && halfSize > 0.0) {
        m_viewHalfSize = halfSize;
        if (m_viewHalfSize < 0.1) m_viewHalfSize = 0.1;
        if (m_viewHalfSize > 500.0) m_viewHalfSize = 500.0;
    }

    if (m_grndWheel && m_blank) {
        BuildContactMask();
    }
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

    if (!m_blank || !m_grndWheel) {
        throw std::runtime_error("Grinding wheel or blank not set");
    }

    // 每次点击 Optimize 都先确认参数；取消时保持当前仿真状态不变。
    WheelParamDialog paramDialog(m_wheelParams, m_cutterParams, this);
    if (paramDialog.exec() != QDialog::Accepted) {
        std::cout << "[Optimize] Cancelled by user.\n";
        return;
    }
    m_wheelParams = paramDialog.GetWheelParams();
    m_cutterParams = paramDialog.GetCutterParams();

    this->setProperty("isOptimization", true);
    m_sliceView->SetRunningMode(RunningMode::OPTIMIZE);
    m_runningMode = RunningMode::OPTIMIZE;

    if (!m_optContext) {
        std::cout << "[System] Initializing Optimizer Core for the first time... (May "
                     "cause a slight stutter)"
                  << "\n";

        uint32_t texWidth = 1024;
        uint32_t texHeight = 1024;

        m_optContext = std::make_unique<optimize::OptimizeResourceContext>(m_lveDevice,
                                                                           texWidth,
                                                                           texHeight);

        m_optMaskSystem = std::make_unique<optimize::OptimizeMaskRenderSystem>(
            m_lveDevice,
            m_optContext->GetContourComputeSetLayout());
    }

    // 参数写入必须放在 m_optContext 创建分支之外，
    // 这样第二次及以后点击 Optimize 时也会使用对话框中的最新值。
    m_optContext->SetGrindingWheelParameters(
        BuildGrindingWheelParameters(m_wheelParams));
    m_optContext->SetCutterParameters(BuildCutterParameters(m_cutterParams));

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
    pushData.stepX = 0.02f;
    double helixAngle =
        glm::radians<double>(m_optContext->GetCutterParameters().helixAngle(0.));
    pushData.tanHelixAngle = static_cast<float>(tan(helixAngle));
    pushData.radius = m_optContext->GetCutterParameters().radius(0.);
    pushData.stepsPerPose = 312;

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

optimize::GrindingWheelParameters Simulation2DDialog::BuildGrindingWheelParameters(
    const WheelParamValues& values) const
{
    optimize::GrindingWheelParameters params;
    params.gR = values.gR;
    params.gr1 = values.gr1;
    params.gr2 = values.gr2;
    params.width = values.width;

    const double r1 = values.gR;
    const double gr1 = values.gr1;
    const double gr2 = values.gr2;
    const double width = values.width;

    if (values.profile == WheelProfileType::A1A1) {
        params.radius = [r1, gr1, gr2, width](double u0) {
            if (u0 < gr1) {
                return r1 - gr1 +
                       std::sqrt(gr1 * gr1 - (gr1 - u0) * (gr1 - u0));
            }
            if (u0 > (width - gr1) && u0 <= width) {
                return r1 - gr2 +
                       std::sqrt(gr2 * gr2 -
                                 (u0 - (width - gr2)) * (u0 - (width - gr2)));
            }
            return r1;
        };

        params.radiusDeriv = [gr1, gr2, width](double u0) {
            if (u0 < gr1) {
                return (gr1 - u0) /
                       std::sqrt(std::max(
                           gr1 * gr1 - (gr1 - u0) * (gr1 - u0), 1e-12));
            }
            if (u0 > (width - gr1) && u0 <= width) {
                return (width - gr2 - u0) /
                       std::sqrt(std::max(
                           gr2 * gr2 -
                               (u0 - width + gr2) * (u0 - width + gr2),
                           1e-12));
            }
            return 0.0;
        };
    } else {
        const double ga = glm::radians(values.bevelAngleDeg);
        const double cosGa = std::cos(ga);
        const double sinGa = std::sin(ga);
        const double tanGa = std::tan(ga);
        const double tanHalfGa = std::tan(ga / 2.0);
        const double largeCornerEnd = gr1 + gr1 * cosGa;
        const double smallCornerStart = width - (gr2 - gr2 * cosGa);

        params.radius = [r1, gr1, gr2, width, sinGa, tanGa, tanHalfGa,
                         largeCornerEnd, smallCornerStart](double u0) {
            if (u0 < largeCornerEnd) {
                return r1 - gr1 +
                       std::sqrt(std::max(
                           gr1 * gr1 - (gr1 - u0) * (gr1 - u0), 0.0));
            }
            if (u0 >= largeCornerEnd && u0 < smallCornerStart) {
                return r1 - gr1 + gr1 * sinGa -
                       (u0 - largeCornerEnd) * (1.0 / tanGa);
            }
            if (u0 >= smallCornerStart && u0 <= width) {
                return r1 - gr1 * (1.0 - 1.0 / tanHalfGa) - width / tanGa -
                       gr2 * tanHalfGa +
                       std::sqrt(std::max(
                           gr2 * gr2 - (gr2 - width + u0) * (gr2 - width + u0),
                           0.0));
            }
            return r1;
        };

        params.radiusDeriv = [gr1, gr2, width, tanGa, largeCornerEnd,
                              smallCornerStart](double u0) {
            if (u0 < largeCornerEnd) {
                return (gr1 - u0) /
                       std::sqrt(std::max(
                           gr1 * gr1 - (gr1 - u0) * (gr1 - u0), 1e-12));
            }
            if (u0 >= largeCornerEnd && u0 < smallCornerStart) {
                return -1.0 / tanGa;
            }
            if (u0 >= smallCornerStart && u0 <= width) {
                return (width - gr2 - u0) /
                       std::sqrt(std::max(
                           gr2 * gr2 -
                               (u0 - width + gr2) * (u0 - width + gr2),
                           1e-12));
            }
            return 0.0;
        };
    }

    return params;
}

optimize::CutterParameters Simulation2DDialog::BuildCutterParameters(
    const CutterParamValues& values) const
{
    optimize::CutterParameters params;
    params.cuttingEdgeLength = values.cuttingEdgeLength;
    params.helixAngle = [helix = values.helixAngleDeg](double) { return helix; };
    params.radius = [radius = values.radius](double) { return radius; };
    params.radiusDeriv = [](double) { return 0.0; };
    params.coreRadius = [core = values.coreRadius](double) { return core; };
    params.slotAngle = [slot = values.slotAngleDeg](double) { return slot; };
    params.radialRakeAngle = [rake = values.radialRakeAngleDeg](double) {
        return rake;
    };
    return params;
}

void Simulation2DDialog::RunWearEstimation()
{
    std::cout << "Enter function: " << __FUNCTION__ << "\n";

    if (!m_blank || !m_grndWheel) {
        QMessageBox::warning(this, tr("警告"), tr("毛坯或砂轮模型尚未初始化"));
        return;
    }

    // 反演需要砂轮实例（刀位点）才能求交，没有刀轨时先引导用户加载
    if (m_grndWheelInstances.empty()) {
        QMessageBox::warning(this,
                             tr("警告"),
                             tr("尚未加载刀轨，请先用 Tool Path 加载刀轨文件"));
        emit OpenToolPathSignal();
        return;
    }

    // 第一步：选容屑槽点集（磨损反演的输入）
    const QString qPath = QFileDialog::getOpenFileName(
        this,
        tr("选择容屑槽点集文件（磨损反演输入）"),
        QString("D:/Data/Study/vulkan/FirstApp/output_stuff/wear"),
        tr("Flute Point Files (*.txt);;All Files (*.*)"));
    if (qPath.isEmpty()) {
        // 用户取消：什么都不做，保持当前画面与状态
        return;
    }

    m_targetFlutePath = std::filesystem::u8path(qPath.toUtf8().constData()).string();

    // 第二步：只让紧接着的这一次 BuildContactMask 走反演分支
    m_wearEstimationRequested = true;

    // 反演内部会切到拟合相机，结束后把视野同步回微观区域，
    // 因此沿用「分析入场」的流程；结束后 runningMode 会被 BuildContactMask 自动复位。
    m_sliceView->SetRunningMode(RunningMode::DISPLAY_AND_ANALYZE);
    m_runningMode = RunningMode::DISPLAY_AND_ANALYZE;

    // 第三步：渲染 + 反演 + 回显（ExecuteWearAnalysis 同步执行，返回后画面已恢复）
    BuildContactMask();
}

void Simulation2DDialog::AnalyzeFluteFromFile()
{
    const QString qPath = QFileDialog::getOpenFileName(
        this,
        tr("选择容屑槽点集文件"),
        QString("D:/Data/Study/vulkan/FirstApp/output_stuff/wear"),
        tr("Flute Point Files (*.txt);;All Files (*.*)"));
    if (qPath.isEmpty()) {
        return;
    }

    std::filesystem::path path = std::filesystem::u8path(qPath.toUtf8().constData());

    if (!m_fluteAnalyzer) {
        m_fluteAnalyzer = std::make_unique<flute::FluteAnalyzer>(m_lveDevice);
    }

    if (!m_fluteAnalyzer->ParseFlutePointSet(path.string())) {
        QMessageBox::critical(this, tr("错误"), tr("读取容屑槽点集失败"));
        return;
    }

    flute::FluteGeometryResult result;
    if (!m_fluteAnalyzer->Analyze(result)) {
        QMessageBox::warning(this,
                             tr("警告"),
                             tr("分析失败（点数过少或未得到有效结果）"));
        return;
    }

    float radius = std::sqrt(std::bit_cast<float>(result.coreRadiusSqBits));
    QString msg = QString(
                      "Core Radius: %1 mm\n"
                      "Core Point: (%2, %3)\n"
                      "Rake Angle: %4 deg\n"
                      "Slot Angle: %5 deg\n"
                      "Tip Point: (%6, %7)")
                      .arg(radius, 0, 'f', 6)
                      .arg(result.coreRadiusPoint.x, 0, 'f', 6)
                      .arg(result.coreRadiusPoint.y, 0, 'f', 6)
                      .arg(result.rakeAngle, 0, 'f', 6)
                      .arg(result.slotAngle, 0, 'f', 6)
                      .arg(result.tipPoint.x, 0, 'f', 6)
                      .arg(result.tipPoint.y, 0, 'f', 6);

    QMessageBox::information(this, tr("Flute Geometry"), msg);
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

    // 保存原始刀轨（插值前）
    m_rawToolpaths = m_toolpaths;

    // 计算并显示统计信息
    int totalPts = 0;
    double totalXStep = 0.0;
    for (const auto& tp : m_rawToolpaths) {
        totalPts += tp.size;
        for (size_t j = 1; j < tp.size; j++) {
            totalXStep += std::abs(tp.points[j].x - tp.points[j - 1].x);
        }
    }
    int numSegs = totalPts > 0 ? totalPts - (int)m_rawToolpaths.size() : 0;
    double avgXStep = numSegs > 0 ? totalXStep / numSegs : 0.0;

    m_labelToolPathStats->setText(QString("Segments: %1 | Points: %2\nAvg X step: %3 mm")
                                      .arg(m_rawToolpaths.size())
                                      .arg(totalPts)
                                      .arg(avgXStep, 0, 'f', 3));

    DEBUG("Parse tool path succeed: ", path.string());

    return 0;
}

void Simulation2DDialog::UpdateToolPath(std::filesystem::path path)
{
    m_toolpaths.clear();
    m_rawToolpaths.clear();
    ReadToolPath(path);
}

void Simulation2DDialog::ApplyInterpolation()
{
    if (m_rawToolpaths.empty()) {
        DEBUG("No toolpath loaded, skip interpolation");
        return;
    }

    bool ok = false;
    float stepLength = m_editInterpStep->text().toFloat(&ok);
    if (!ok || stepLength <= 0.f) {
        DEBUG("Invalid interpolation step length: ",
              m_editInterpStep->text().toStdString());
        return;
    }

    // 计算原始刀轨的平均X步长
    double totalXSpan = 0.0;
    int totalSegs = 0;
    for (const auto& tp : m_rawToolpaths) {
        for (size_t j = 1; j < tp.size; j++) {
            totalXSpan += std::abs(tp.points[j].x - tp.points[j - 1].x);
        }
        totalSegs += static_cast<int>(tp.size) - 1;
    }
    double origAvgXStep = totalSegs > 0 ? totalXSpan / totalSegs : 0.0;

    if (stepLength > origAvgXStep) {
        QMessageBox::warning(this,
                             tr("警告"),
                             QString(tr("插值间距 (%1 mm) 大于原始刀轨平均间距 (%2 mm)，"
                                        "将导致刀位点变稀疏，可能丢失几何特征。"))
                                 .arg(stepLength, 0, 'f', 3)
                                 .arg(origAvgXStep, 0, 'f', 3));
    }

    m_toolpaths.clear();
    for (const auto& raw : m_rawToolpaths) {
        m_toolpaths.push_back(DualNURBSCurveInterpolator::Interpolate(raw, stepLength));
    }

    // 更新统计显示
    int totalPts = 0;
    for (const auto& tp : m_toolpaths) {
        totalPts += tp.size;
    }
    m_labelToolPathStats->setText(
        QString("Segments: %1 | Interpolated: %2 pts\nX Step: %3 mm")
            .arg(m_toolpaths.size())
            .arg(totalPts)
            .arg(stepLength, 0, 'f', 3));

    UpdateGrindingWheelInstances();

    if (m_grndWheel && m_blank) {
        BuildContactMask();
    }
}

void Simulation2DDialog::GenerateHelix()
{
    if (m_rawToolpaths.empty() || m_rawToolpaths[0].points.empty()) {
        DEBUG("No toolpath loaded, skip helix generation");
        return;
    }

    bool okA = false, okL = false, okDx = false;
    float angleDeg = m_editHelixAngle->text().toFloat(&okA);
    float length = m_editHelixLength->text().toFloat(&okL);
    float dx = m_editHelixXStep->text().toFloat(&okDx);

    if (!okA || !okL || !okDx || dx <= 0.f || length <= 0.f) {
        DEBUG("Invalid helix parameters");
        return;
    }

    glm::vec3 p0 = m_rawToolpaths[0].points[0];
    glm::vec3 n0 = m_rawToolpaths[0].normals[0];
    float r = std::sqrt(p0.y * p0.y + p0.z * p0.z);
    if (r <= 0.f) {
        DEBUG("Invalid helix radius from first toolpath point");
        return;
    }

    float angleRad = glm::radians(angleDeg);
    float dTheta = dx * std::tan(angleRad) / r;
    float theta0 = std::atan2(p0.z, p0.y);

    int numPoints = static_cast<int>(std::floor(length / dx)) + 1;

    ToolPath helix;
    for (int i = 0; i < numPoints; i++) {
        float rotAngle = i * dTheta;
        float theta = theta0 + rotAngle;
        float x = p0.x + i * dx;
        float y = r * std::cos(theta);
        float z = r * std::sin(theta);

        glm::mat4 rotX =
            glm::rotate(glm::mat4(1.0f), rotAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 n = glm::vec3(rotX * glm::vec4(n0, 0.0f));

        helix.points.push_back({x, y, z});
        helix.normals.push_back(n);
    }
    helix.size = helix.points.size();

    m_toolpaths.clear();
    m_toolpaths.push_back(helix);

    m_labelToolPathStats->setText(
        QString("Helix: %1 pts | Radius: %2 mm\nAngle: %3° | Length: %4 mm | Step: %5 mm")
            .arg(numPoints)
            .arg(r, 0, 'f', 2)
            .arg(angleDeg, 0, 'f', 1)
            .arg(length, 0, 'f', 2)
            .arg(dx, 0, 'f', 3));

    UpdateGrindingWheelInstances();

    if (m_grndWheel && m_blank) {
        BuildContactMask();
    }
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
