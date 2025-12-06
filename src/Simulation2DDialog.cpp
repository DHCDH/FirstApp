#include "Simulation2DDialog.h"

#include "SliceView.h"

#include <QHBoxLayout>
#include <QTimer>

Simulation2DDialog::Simulation2DDialog(lve::LveDevice& device, QWidget* parent)
	: QDialog(parent), m_renderWidget(new QWidget(this)), m_renderTimer(new QTimer(this))
{
	this->setWindowTitle("2D Simulation");
	this->resize(720, 480);

	QHBoxLayout* mainLayout = new QHBoxLayout(this);
	//mainLayout->addWidget(m_renderWidget);

	/*初始化显示label*/
	m_displayLabel = new QLabel(this);
	m_displayLabel->setAlignment(Qt::AlignCenter);
	m_displayLabel->setStyleSheet("background - color: black; border: 1px solid gray;");
	m_displayLabel->setMinimumSize(400, 400);
	m_displayLabel->setScaledContents(true);
	mainLayout->addWidget(m_displayLabel);

	/*初始化防抖定时器*/
	m_resizeTimer = new QTimer(this);
	m_resizeTimer->setSingleShot(true);	// 只触发一次
	m_resizeTimer->setInterval(100);	// 延迟100ms

	m_renderWidget->winId();
	void* hwnd = reinterpret_cast<void*>(m_renderWidget->winId());
	void* hinstance = GetModuleHandle(nullptr);

	InitSliceView(device, hwnd, hinstance);	

	connect(m_renderTimer, &QTimer::timeout, [this]() {
		m_sliceView->RunFrame();
	});
	m_renderTimer->start(16);

	connect(m_resizeTimer, &QTimer::timeout, [this]() {
		if (m_grndWheel && m_blank) {
			BuildContactMask();
		}
	});
}

void Simulation2DDialog::InitSliceView(lve::LveDevice& device, void* hwnd, void* hinstance)
{
	SliceViewConfig config = UpdateView();

	m_sliceView = std::make_unique<SliceView>(device, config, hwnd, hinstance, width(), height(), "2D Simulation");
}

void Simulation2DDialog::UpdateEntitiesData(const GrindingWheel& grndWheel, const Blank& blank,
	const std::vector<lve::InstanceData>& grndWheelInstances)
{
	std::cout << "-----Update entities data." << "\n";
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
	frameData.yM = 5.f;
	frameData.thickness = 10.f;
	frameData.blankModel = glm::mat4(1.f);
	frameData.wheelModels.reserve(m_grndWheelInstances.size());
	for (const auto& instance : m_grndWheelInstances) {
		frameData.wheelModels.push_back(instance.modelMatrix);
	}

	/*执行GPU计算*/
	m_sliceView->BuildContactMask(frameData);

	/*显示到界面*/
	QImage resultImg = m_sliceView->GetSerializedImage();
	if (!resultImg.isNull()) {
		m_displayLabel->setPixmap(QPixmap::fromImage(resultImg));
		std::cout << "Displaying image size: " << resultImg.width() << "x" << resultImg.height() << "\n";
	}
	else {
        std::cerr << "Failed to display image." << "\n";
        m_displayLabel->setText("Failed to display image.");
	}

}

SliceViewConfig Simulation2DDialog::UpdateView()
{
	double w = this->width();
	double h = this->height();
	/*计算宽高比*/
	float aspectRatio = static_cast<float>(this->width()) / static_cast<float>(this->height());

	/*配置视图*/
	SliceViewConfig config{};
	/*分辨率 pixels*/
	config.nX = static_cast<uint32_t>(w);
	config.nZ = static_cast<uint32_t>(h);

	/*设定视野基准*/
	float baseViewHalfSize = 10.f;

	/*根据比例修正视野范围*/
	if (aspectRatio > 1.f) {
		config.zMin = -baseViewHalfSize;
		config.zMax = baseViewHalfSize;
		config.xMin = -baseViewHalfSize * aspectRatio;
		config.xMax = baseViewHalfSize * aspectRatio;
	}
	else {
		config.xMin = -baseViewHalfSize;
		config.xMax = baseViewHalfSize;
		config.zMin = -baseViewHalfSize / aspectRatio;
		config.zMax = baseViewHalfSize / aspectRatio;
	}

	return config;
}

void Simulation2DDialog::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);

	m_resizeTimer->start();

	if (m_sliceView) {
		m_sliceView->GetWindow()->NotifyResized(this->width(), this->height());
	}
}

void Simulation2DDialog::closeEvent(QCloseEvent* e)
{
	if (m_renderTimer) m_renderTimer->stop();
	QDialog::closeEvent(e);
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