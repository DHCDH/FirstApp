#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

class NcExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NcExportDialog(QWidget* parent = nullptr);
    ~NcExportDialog() override = default;

private:
    void BrowseInput();
    void BrowseOutput();
    void Generate();

    QLineEdit* m_inputPath = nullptr;
    QLineEdit* m_outputPath = nullptr;
    QComboBox* m_operation = nullptr;
    QLineEdit* m_toolNum = nullptr;
    QLineEdit* m_cylinderRate = nullptr;
    QLineEdit* m_engamentRate = nullptr;
    QLineEdit* m_disengagementRate = nullptr;
    QLineEdit* m_toolLength = nullptr;
    QLineEdit* m_fLangeDist = nullptr;
    QLineEdit* m_wheelPackageLength = nullptr;
    QCheckBox* m_wheelClampOut = nullptr;
    QLineEdit* m_clampingLength = nullptr;
    QLineEdit* m_rs = nullptr;
    QLineEdit* m_lp = nullptr;
    QComboBox* m_mountingShaftIndex = nullptr;
    QLineEdit* m_safeDistance = nullptr;
    QLineEdit* m_engagementDistance = nullptr;
    QLineEdit* m_disengagementDistance = nullptr;
    QComboBox* m_engagementDirection = nullptr;
    QLabel* m_status = nullptr;
};
