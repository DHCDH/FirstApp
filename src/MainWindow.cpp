#include "MainWindow.h"

#include "FirstApp.h"
#include "Simulation2DDialog.h"
#include "lve/LveWindow.h"

#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QEvent>
#include <QMouseEvent>

#include <windows.h>
#include <iostream>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_renderWidget(new QWidget(this)), 
    m_renderTimer(new QTimer(this)), m_buttonWidget(new QWidget(this)), 
    m_simulation2DDialog(new Simulation2DDialog(this))
{
    setWindowTitle("FirstApp");

    this->resize(1080, 720);

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
    // m_renderWidget->setMinimumSize(800, 600);

    /*获取原生窗口句柄HWND*/
    m_renderWidget->winId(); // 确保窗口创建
    void* hwnd = reinterpret_cast<void*>(m_renderWidget->winId());
    void* hinstance = GetModuleHandle(nullptr);

    m_renderWidget->setMouseTracking(true);
    m_renderWidget->installEventFilter(this);

    /*创建vulkanApp，传入Qt窗口句柄*/
    m_vulkanApp = std::make_unique<FirstApp>(hwnd, hinstance, 800, 600, "Vulkan App");

    /*启动渲染循环*/
    connect(m_renderTimer, &QTimer::timeout, [this]() {
        m_vulkanApp->runFrame();
        });
    m_renderTimer->start(16); // 60 FPS

}

void MainWindow::InitUI()
{
    QVBoxLayout* buttonLayout = new QVBoxLayout(m_buttonWidget);
    QPushButton* btnStart = new QPushButton("Start", m_buttonWidget);
    QPushButton* btnPause = new QPushButton("Pause", m_buttonWidget);
    QPushButton* btnReset = new QPushButton("Reset", m_buttonWidget);
    QPushButton* btnTrack = new QPushButton("Generate Track", m_buttonWidget);
    QPushButton* btn2DSimulation = new QPushButton("2D Simulation", m_buttonWidget);
    QPushButton* btnQuit = new QPushButton("Quit", m_buttonWidget);
    buttonLayout->addWidget(btnStart);
    buttonLayout->addWidget(btnPause);
    buttonLayout->addWidget(btnReset);
    buttonLayout->addWidget(btnTrack);
    buttonLayout->addWidget(btn2DSimulation);
    buttonLayout->addStretch();  // 让按钮靠上排列
    buttonLayout->addWidget(btnQuit);

    connect(btnReset, &QPushButton::clicked, [this]() {
        m_vulkanApp->ResetView();
    });
    connect(btnStart, &QPushButton::clicked, this, [this]() {
        m_vulkanApp->SetGrindingWheelMotionEnable(true);
    });
    connect(btnPause, &QPushButton::clicked, this, [this]() {
        m_vulkanApp->SetGrindingWheelMotionEnable(false);
    });
    connect(btn2DSimulation, &QPushButton::clicked, this, [this]() {
        m_simulation2DDialog->show();
    });
    connect(btnTrack, &QPushButton::clicked, [this]() {
        // m_vulkanApp->BuildGrindingWheelTrackInstances(0.f, 5.f, 100);
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
                if (e->button() == Qt::RightButton)  m_rightDown = true;

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
                const float dx  = d.x() * dpr;
                const float dy  = d.y() * dpr;
                if (m_leftDown) {
                    m_vulkanApp->Orbit(dx, dy);
                } else if (m_midDown || m_rightDown) {
                    m_vulkanApp->Pan(-dx, dy); // 保持屏幕坐标系方向习惯
                }
                return true;
            }
            case QEvent::MouseButtonRelease: {
                auto* e = static_cast<QMouseEvent*>(event);
                if (e->button() == Qt::LeftButton)   m_leftDown = false;
                if (e->button() == Qt::MiddleButton) m_midDown = false;
                if (e->button() == Qt::RightButton)  m_rightDown = false;

                // 释放鼠标（可选）
                if (!m_leftDown && !m_midDown && !m_rightDown) {
                    m_renderWidget->releaseMouse();
                }
                return true;
            }
            case QEvent::Wheel: {
                auto* e = static_cast<QWheelEvent*>(event);
                // 120 对应 1 step；如需触控板平滑滚动可优先用 pixelDelta
                float steps = 0.f;
                if (e->pixelDelta().y() != 0) {
                    steps = float(e->pixelDelta().y()) / 120.f;
                }
                else {
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
        m_vulkanApp->GetLveWindow()->NotifyResized(centralWidget()->width(), centralWidget()->height());
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

    if (m_vulkanApp) {
        /*等待GPU完成所有命令*/
        m_vulkanApp->WaitIdle();
        m_vulkanApp.reset();
    }
}

