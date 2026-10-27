#include "NcExportDialog.h"

#include "NcCommon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string Utf8Path(const QString& s)
{
    return std::filesystem::u8path(s.toUtf8().constData()).string();
}

}  // namespace

NcExportDialog::NcExportDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("生成 NC 代码");
    resize(560, 760);

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    // 文件选择
    m_inputPath = new QLineEdit(this);
    m_inputPath->setReadOnly(true);
    auto* browseInput = new QPushButton("浏览...", this);
    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(m_inputPath, 1);
    inputRow->addWidget(browseInput);
    form->addRow("刀轨文件", inputRow);

    m_outputPath = new QLineEdit(this);
    m_outputPath->setReadOnly(true);
    auto* browseOutput = new QPushButton("浏览...", this);
    auto* outputRow = new QHBoxLayout();
    outputRow->addWidget(m_outputPath, 1);
    outputRow->addWidget(browseOutput);
    form->addRow("输出 .nc 文件", outputRow);

    // 工序
    m_operation = new QComboBox(this);
    for (nc::OperationType t : nc::AllOperationTypes()) {
        m_operation->addItem(nc::OperationTypeName(t));
    }
    for (int i = 0; i < m_operation->count(); ++i) {
        if (m_operation->itemText(i) == QString("Flute")) {
            m_operation->setCurrentIndex(i);
            break;
        }
    }
    form->addRow("工序类型 OperationType", m_operation);

    // 加工参数
    m_toolNum = new QLineEdit("10", this);
    form->addRow("刀具号 ToolNum", m_toolNum);
    m_cylinderRate = new QLineEdit("200", this);
    form->addRow("切削进给 CylinderRate (F)", m_cylinderRate);
    m_engamentRate = new QLineEdit("30", this);
    form->addRow("进刀进给 EngamentRate (F)", m_engamentRate);
    m_disengagementRate = new QLineEdit("1000", this);
    form->addRow("退刀进给 DisengagementRate (F)", m_disengagementRate);

    // 刀具/机床结构
    m_toolLength = new QLineEdit("0", this);
    form->addRow("刀具长度 ToolLength", m_toolLength);
    m_fLangeDist = new QLineEdit("0", this);
    form->addRow("法兰长度 FLangeDist", m_fLangeDist);
    m_wheelPackageLength = new QLineEdit("0", this);
    form->addRow("砂轮组总长 WheelPackageLength", m_wheelPackageLength);
    m_wheelClampOut = new QCheckBox("反向砂轮法向（内部安装）", this);
    m_wheelClampOut->setChecked(false);
    form->addRow("砂轮安装方向 WheelClampOut", m_wheelClampOut);
    m_clampingLength = new QLineEdit("130", this);
    form->addRow("夹紧外露长度 ClampingLength", m_clampingLength);
    m_rs = new QLineEdit("60", this);
    form->addRow("砂轮半径 Rs", m_rs);
    m_lp = new QLineEdit("0", this);
    form->addRow("砂轮端结构 Lp", m_lp);
    m_mountingShaftIndex = new QComboBox(this);
    m_mountingShaftIndex->addItem("0 - 默认轴");
    m_mountingShaftIndex->addItem("1 - 180° 轴");
    form->addRow("安装轴序号 MountingShaftIndex", m_mountingShaftIndex);

    // 进退刀参数
    m_safeDistance = new QLineEdit("2.0", this);
    form->addRow("安全距离 SafeDistance", m_safeDistance);
    m_engagementDistance = new QLineEdit("2.0", this);
    form->addRow("进刀距离 EngagementDistance", m_engagementDistance);
    m_disengagementDistance = new QLineEdit("10.0", this);
    form->addRow("退刀距离 DisengagementDistance", m_disengagementDistance);
    m_engagementDirection = new QComboBox(this);
    m_engagementDirection->addItem("0 - 自动方向");
    m_engagementDirection->addItem("1 - 沿法向");
    form->addRow("进刀方向 EngagementDirection", m_engagementDirection);

    mainLayout->addLayout(form);

    auto* generateBtn = new QPushButton("生成 NC", this);
    mainLayout->addWidget(generateBtn);

    m_status = new QLabel("", this);
    mainLayout->addWidget(m_status);

    connect(browseInput, &QPushButton::clicked, this, &NcExportDialog::BrowseInput);
    connect(browseOutput, &QPushButton::clicked, this, &NcExportDialog::BrowseOutput);
    connect(generateBtn, &QPushButton::clicked, this, &NcExportDialog::Generate);
}

void NcExportDialog::BrowseInput()
{
    const QString qPath = QFileDialog::getOpenFileName(
        this,
        tr("选择 toolpath 文件"),
        QString("D:/Data/Study/vulkan/FirstApp/output_stuff"),
        tr("Toolpath Files (*.txt);;All Files (*.*)"));
    if (qPath.isEmpty()) return;

    m_inputPath->setText(qPath);

    if (m_outputPath->text().trimmed().isEmpty()) {
        QFileInfo fi(qPath);
        m_outputPath->setText(fi.absolutePath() + "/" + fi.completeBaseName() + ".nc");
    }
}

void NcExportDialog::BrowseOutput()
{
    QString startDir = m_inputPath->text().trimmed();
    if (!startDir.isEmpty()) {
        QFileInfo fi(startDir);
        startDir = fi.absolutePath() + "/" + fi.completeBaseName() + ".nc";
    }

    const QString qPath = QFileDialog::getSaveFileName(
        this, tr("保存 NC 文件"), startDir, tr("NC Files (*.nc);;All Files (*.*)"));
    if (qPath.isEmpty()) return;

    m_outputPath->setText(qPath);
}

void NcExportDialog::Generate()
{
    const QString inPath = m_inputPath->text().trimmed();
    if (inPath.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("请先选择刀轨文件"));
        return;
    }

    const QString outPath = m_outputPath->text().trimmed();
    if (outPath.isEmpty()) {
        QMessageBox::warning(this, tr("警告"), tr("请指定输出 .nc 文件"));
        return;
    }

    std::vector<nc::NcInputSeg> segs;
    if (!nc::LoadToolPathTxt(Utf8Path(inPath), segs)) {
        QMessageBox::critical(this, tr("错误"), tr("刀轨文件解析失败"));
        return;
    }

    auto parseDouble = [this](QLineEdit* edit, double& out, const QString& name) -> bool {
        bool ok = false;
        out = edit->text().trimmed().toDouble(&ok);
        if (!ok) {
            QMessageBox::warning(this, tr("警告"), tr("参数无效: ") + name);
            return false;
        }
        return true;
    };

    nc::NcProcessorConfig pcfg;
    nc::NcWriterConfig wcfg;

    bool ok = false;
    wcfg.toolNum = m_toolNum->text().trimmed().toInt(&ok);
    if (!ok) {
        QMessageBox::warning(this, tr("警告"), tr("参数无效: ToolNum"));
        return;
    }

    if (!parseDouble(m_cylinderRate, wcfg.cylinderRate, tr("CylinderRate")) ||
        !parseDouble(m_engamentRate, wcfg.engamentRate, tr("EngamentRate")) ||
        !parseDouble(m_disengagementRate, wcfg.disengagementRate, tr("DisengagementRate")) ||
        !parseDouble(m_toolLength, wcfg.toolLength, tr("ToolLength")) ||
        !parseDouble(m_fLangeDist, wcfg.fLangeDist, tr("FLangeDist")) ||
        !parseDouble(m_wheelPackageLength, wcfg.wheelPackageLength, tr("WheelPackageLength")) ||
        !parseDouble(m_clampingLength, wcfg.clampingLength, tr("ClampingLength")) ||
        !parseDouble(m_rs, wcfg.rs, tr("Rs")) ||
        !parseDouble(m_lp, wcfg.lp, tr("Lp")) ||
        !parseDouble(m_safeDistance, pcfg.safeDistance, tr("SafeDistance")) ||
        !parseDouble(m_engagementDistance, pcfg.engagementDistance, tr("EngagementDistance")) ||
        !parseDouble(m_disengagementDistance, pcfg.disengagementDistance, tr("DisengagementDistance"))) {
        return;
    }

    const nc::OperationType op =
        nc::AllOperationTypes().at(static_cast<size_t>(m_operation->currentIndex()));
    pcfg.operationType = op;
    wcfg.pathType = op;
    wcfg.wheelClampOut = m_wheelClampOut->isChecked();
    wcfg.mountingShaftIndex = m_mountingShaftIndex->currentIndex();
    pcfg.engagementDirection = m_engagementDirection->currentIndex();

    std::vector<nc::NcMotionSeg> motions = nc::ProcessToolPath(segs, pcfg);
    if (motions.empty()) {
        QMessageBox::warning(this, tr("警告"), tr("没有生成任何刀轨段"));
        return;
    }

    nc::NcWriteResult r = nc::WriteNc(motions, wcfg, Utf8Path(outPath));
    if (!r.success) {
        QMessageBox::critical(this, tr("错误"), QString::fromStdString(r.error));
        return;
    }

    m_status->setText(r.isOverTravel ? "已生成（检测到超程）" : "已生成");

    QString msg = QString("已生成 NC 文件:\n%1").arg(outPath);
    if (r.isOverTravel) {
        msg += tr("\n\n注意：检测到超程（IsOverTravel = true），请检查参数或软限位。");
    }
    QMessageBox::information(this, tr("完成"), msg);
}
