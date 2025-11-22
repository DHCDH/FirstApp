#pragma once

#include <QtWidgets/QMainWindow>
#include <QDialog>

#include <memory>

class VulkanWindow;
class QTimer;
class FirstApp;
class Simulation2DDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    void InitRenderWidget();
    void InitUI();

private:
    QWidget* m_renderWidget;
    QWidget* m_buttonWidget;
    QTimer* m_renderTimer;
    std::unique_ptr<FirstApp> m_vulkanApp;
    Simulation2DDialog* m_simulation2DDialog;

    /*窗口交互转台*/
    QPoint m_lastPos;
    bool m_leftDown = false;
    bool m_midDown = false;
    bool m_rightDown = false;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
};

