#pragma once

#include <QtWidgets/QMainWindow>

#include "FirstApp.h"

#include <memory>

class VulkanWindow;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QWidget* m_renderWidget;
    QWidget* m_buttonWidget;
    QTimer* m_renderTimer;
    std::unique_ptr<lve::FirstApp> m_vulkanApp;

    /*窗口交互转台*/
    QPoint m_lastPos;
    bool m_leftDown = false;
    bool m_midDown = false;
    bool m_rightDown = false;

    void InitRenderWidget();
    void InitUI();
};

