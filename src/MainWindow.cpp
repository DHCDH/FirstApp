#include "MainWindow.h"

#include <windows.h>

#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <iostream>
#include <QMessageBox>
#include <QLineEdit>
#include <QLabel>
#include <QStringList>

#include "FirstApp.h"
#include "Simulation2DDialog.h"
#include "slice/SliceView.h"
#include "lve/LveWindow.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_renderWidget(new QWidget(this)),
      m_renderTimer(new QTimer(this)),
      m_buttonWidget(new QWidget(this))
{
    setWindowTitle("FirstApp");

    this->resize(1080, 800);

    QWidget* centralWidget = new QWidget(this);
    QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    setCentralWidget(centralWidget);

    InitRenderWidget();
    InitUI();

    mainLayout->addWidget(m_renderWidget, 1);
    mainLayout->addWidget(m_buttonWidget);
}

void MainWindow::InitRenderWidget()
{
    m_renderWidget->setMinimumSize(720, 720);

    /*获取原生窗口句柄HWND*/
    m_renderWidget->winId();  // 确保窗口创建
    void* hwnd = reinterpret_cast<void*>(m_renderWidget->winId());
    void* hinstance = GetModuleHandle(nullptr);

    m_renderWidget->setMouseTracking(true);
    m_renderWidget->installEventFilter(this);

    /*创建vulkanApp，传入Qt窗口句柄*/
    m_vulkanApp = std::make_unique<FirstApp>(hwnd, hinstance, 800, 600, "Vulkan App");
    m_vulkanApp->ReadToolPath("D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff\\optimize_toolpath.txt");

    /*启动渲染循环*/
    connect(m_renderTimer, &QTimer::timeout, [this]() {
#if 0
        m_vulkanApp->RunFrame();
#else
            m_vulkanApp->RunFrameForThicknessMap();
#endif
    });
    m_renderTimer->start(16);  // 60 FPS
}

void MainWindow::InitUI()
{
    QVBoxLayout* buttonLayout = new QVBoxLayout(m_buttonWidget);
    QPushButton* btnToolPath = new QPushButton("Tool Path", m_buttonWidget);
    QPushButton* btn2DSimulation = new QPushButton("2D Simulation", m_buttonWidget);
    QPushButton* btnStart = new QPushButton("Start", m_buttonWidget);
    QPushButton* btnPause = new QPushButton("Pause", m_buttonWidget);
    QPushButton* btnReset = new QPushButton("Reset", m_buttonWidget);
    QPushButton* btnInstanced = new QPushButton("Instanced", m_buttonWidget);
    QPushButton* btnQuit = new QPushButton("Quit", m_buttonWidget);
    QLabel* labelPos = new QLabel("Camera Position", m_buttonWidget);
    QLabel* labelTarget = new QLabel("Camera Target", m_buttonWidget);
    QLabel* labelUp = new QLabel("Camera Up", m_buttonWidget);
    m_camPos = new QLineEdit("224., -124.22, 224", m_buttonWidget);
    //m_camPos = new QLineEdit("-200, 0, ", m_buttonWidget);
    m_camTarget = new QLineEdit("11.20, 33.38, 7.56", m_buttonWidget);
    m_camUp = new QLineEdit("-0.54, -0.64, -0.54", m_buttonWidget);
    //m_camUp = new QLineEdit("0, -1, 0", m_buttonWidget);
    QPushButton* btnUpdateCam = new QPushButton("Update Camera", m_buttonWidget);
    buttonLayout->addWidget(btnToolPath);
    buttonLayout->addWidget(btn2DSimulation);
    buttonLayout->addWidget(btnStart);
    buttonLayout->addWidget(btnPause);
    buttonLayout->addWidget(btnReset);
    buttonLayout->addWidget(btnInstanced);
    buttonLayout->addWidget(labelPos);
    buttonLayout->addWidget(m_camPos);
    buttonLayout->addWidget(labelTarget);
    buttonLayout->addWidget(m_camTarget);
    buttonLayout->addWidget(labelUp);
    buttonLayout->addWidget(m_camUp);
    buttonLayout->addWidget(btnUpdateCam);
    buttonLayout->addStretch();  // 让按钮靠上排列
    buttonLayout->addWidget(btnQuit);

    connect(btnToolPath, &QPushButton::clicked, this, [this]() {
        const QString qPath = QFileDialog::getOpenFileName(
            this,
            tr("选择 toolpath 文件"),
            QString("D:\\Data\\Study\\vulkan\\FirstApp\\output_stuff"),
            tr("Toolpath Files (*.txt);;All Files (*.*)"));
        if (qPath.isEmpty())
            QMessageBox::critical(this, tr("警告"), tr("输入正确刀轨文件"));

        std::filesystem::path path = std::filesystem::u8path(qPath.toUtf8().constData());
        m_vulkanApp->ReadToolPath(path);
    });
    connect(btnReset, &QPushButton::clicked, [this]() { m_vulkanApp->ResetView(); });
    connect(btnStart, &QPushButton::clicked, this, [this]() {
        m_vulkanApp->SetGrindingWheelMotionEnable(true);
    });
    connect(btnPause, &QPushButton::clicked, this, [this]() {
        m_vulkanApp->SetGrindingWheelMotionEnable(false);
    });
    connect(btn2DSimulation, &QPushButton::clicked, this, [this]() {
        if (m_2DSimDialog == nullptr)
            m_2DSimDialog = new Simulation2DDialog(m_vulkanApp->GetDevice(), this);
        m_2DSimDialog->show();
        m_is2DSimulationActive = true;
        m_2DSimDialog->UpdateEntitiesData(m_vulkanApp->GetGrindingWheel(),
                                          m_vulkanApp->GetBlank(),
                                          m_vulkanApp->GetGrindingWheelInstances());
        m_2DSimDialog->BuildContactMask();
    });
    connect(btnInstanced, &QPushButton::clicked, [this]() {
        m_vulkanApp->SetInstancesShown(!m_instancedShown);
        m_instancedShown = !m_instancedShown;
    });
    connect(btnUpdateCam, &QPushButton::clicked, this, [this]() {
        auto parseVec3 = [](const QString& str) -> glm::vec3 {
            // 支持逗号或空格分隔
            QStringList parts =
                str.contains(',') ? str.split(',') : str.split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 3) return glm::vec3(0.f);
            return glm::vec3(parts[0].trimmed().toFloat(),
                             parts[1].trimmed().toFloat(),
                             parts[2].trimmed().toFloat());
        };

        glm::vec3 pos = parseVec3(m_camPos->text());
        glm::vec3 target = parseVec3(m_camTarget->text());
        glm::vec3 up = parseVec3(m_camUp->text());

        if (m_vulkanApp) {
            m_vulkanApp->SetCameraPose(pos, target, up);
        }
    });
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_renderWidget) {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto* e = static_cast<QMouseEvent*>(event);
                m_lastPos = e->pos();
                if (e->button() == Qt::LeftButton) m_leftDown = true;
                if (e->button() == Qt::MiddleButton) m_midDown = true;
                if (e->button() == Qt::RightButton) m_rightDown = true;

                // 交互时抓鼠，避免拖到窗口外中断
                m_renderWidget->grabMouse();
                return true;
            }
            case QEvent::MouseMove: {
                auto* e = static_cast<QMouseEvent*>(event);
                QPoint d = e->pos() - m_lastPos;
                m_lastPos = e->pos();
                if (!m_vulkanApp) return true;
                // === [新增] HiDPI 校正：把像素位移乘以设备像素比 ===
                const float dpr = m_renderWidget->devicePixelRatioF();
                const float dx = d.x() * dpr;
                const float dy = d.y() * dpr;
                if (m_leftDown) {
                    m_vulkanApp->Orbit(dx, dy);
                } else if (m_midDown || m_rightDown) {
                    m_vulkanApp->Pan(-dx, dy);  // 保持屏幕坐标系方向习惯
                }
                return true;
            }
            case QEvent::MouseButtonRelease: {
                auto* e = static_cast<QMouseEvent*>(event);
                if (e->button() == Qt::LeftButton) m_leftDown = false;
                if (e->button() == Qt::MiddleButton) m_midDown = false;
                if (e->button() == Qt::RightButton) m_rightDown = false;

                // 释放鼠标（可选）
                if (!m_leftDown && !m_midDown && !m_rightDown) {
                    m_renderWidget->releaseMouse();

                    // --- 在拖拽结束时输出相机坐标 ---
                    if (m_vulkanApp) {
                        glm::vec3 pos = m_vulkanApp->GetCameraPosition();
                        glm::vec3 up = m_vulkanApp->GetCameraUp();
                        glm::vec3 target = m_vulkanApp->GetCameraTarget();

                        // 自动回填UI
                        m_camPos->setText(QString("%1, %2, %3")
                                              .arg(pos.x, 0, 'f', 2)
                                              .arg(pos.y, 0, 'f', 2)
                                              .arg(pos.z, 0, 'f', 2));
                        m_camTarget->setText(QString("%1, %2, %3")
                                                 .arg(target.x, 0, 'f', 2)
                                                 .arg(target.y, 0, 'f', 2)
                                                 .arg(target.z, 0, 'f', 2));
                        m_camUp->setText(QString("%1, %2, %3")
                                             .arg(up.x, 0, 'f', 2)
                                             .arg(up.y, 0, 'f', 2)
                                             .arg(up.z, 0, 'f', 2));
                    }
                }
                return true;
            }
            case QEvent::Wheel: {
                auto* e = static_cast<QWheelEvent*>(event);
                // 120 对应 1 step；如需触控板平滑滚动可优先用 pixelDelta
                float steps = 0.f;
                if (e->pixelDelta().y() != 0) {
                    steps = float(e->pixelDelta().y()) / 120.f;
                } else {
                    steps = float(e->angleDelta().y()) / 120.f;
                }

                if (m_vulkanApp && steps != 0.f) {
                    m_vulkanApp->Dolly(steps);
                }
                return true;
            }
            default:
                break;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (m_vulkanApp) {
        m_vulkanApp->GetLveWindow()->NotifyResized(centralWidget()->width(),
                                                   centralWidget()->height());
    }
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (m_renderTimer) m_renderTimer->stop();
    QMainWindow::closeEvent(e);
}

MainWindow::~MainWindow()
{
    if (m_renderTimer) {
        m_renderTimer->stop();
        disconnect(m_renderTimer, nullptr, this, nullptr);
    }

    /*必须保证2DSimDialog在vulkanApp前被销毁*/
    if (m_2DSimDialog) {
        m_2DSimDialog->close();
        delete m_2DSimDialog;
        m_2DSimDialog = nullptr;
    }

    if (m_vulkanApp) {
        /*等待GPU完成所有命令*/
        m_vulkanApp->WaitIdle();
        m_vulkanApp.reset();
    }
}
