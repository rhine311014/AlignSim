// 20261103 ZJH CalibBoardPage —— 相机内参标定板页签
// 左: 标定板预览 (缩放适配); 右: 图案参数 / 显示器与像素点距 / 视觉软件参数 / 导出
// 全屏显示: 在选中的显示器上按 1:1 物理像素绘制 (抵消 Windows 显示缩放), 保证格距像素与物理尺寸精确
//
// 注意: 屏幕是一个固定平面, 内参标定需物理倾斜相机或显示器拍 10~20 个姿态; 只拍正对的屏幕解不出焦距/主点
#pragma once

#include "sim/CalibBoard.h"

#include <QImage>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QScreen;
class QSpinBox;

// 20261103 ZJH 标定板绘制控件
//   bExact = true : 1:1 物理像素居中显示 (全屏拍屏用), 超出窗口部分裁掉
//   bExact = false: 等比缩放适配窗口 (预览用)
class CalibBoardView : public QWidget
{
    Q_OBJECT
public:
    CalibBoardView(bool bExact, QWidget* pParent = nullptr);

    // 20261103 ZJH 设置标定板: image 为精确像素图 (1:1 模式用), spec 供预览模式按窗口尺寸矢量重绘
    void setBoard(const QImage& image, const CalibBoardSpec& spec);
    bool isCropped() const;                            // 1:1 模式下整板是否超出窗口

protected:
    void paintEvent(QPaintEvent* pEvent) override;

private:
    bool m_bExact = false;  // 是否 1:1 物理像素
    QImage m_image;         // 精确像素标定板
    CalibBoardSpec m_spec;  // 标定板参数 (预览矢量绘制用)
    bool m_bInvert = false; // 反色 (背景填黑)
};

class CalibBoardPage : public QWidget
{
    Q_OBJECT
public:
    explicit CalibBoardPage(QWidget* pParent = nullptr);

    // 20261103 ZJH 配置读写 (dPixelPitchMm ≤ 0 表示使用系统读数)
    void setSettings(const CalibBoardSpec& spec, double dPixelPitchMm, double dPrintPitchMm);
    CalibBoardSpec spec() const;
    double pixelPitchSetting() const;  // 用户填写的点距 (0 = 未覆盖)
    double printPitchMm() const;

private slots:
    void onParamsChanged();      // 任一参数变化 → 重新渲染预览与说明
    void onScreenChanged();      // 切换显示器 → 刷新系统点距参考
    void onUseSystemPitch();     // 点距用系统读数
    void onApplyTargetMm();      // 按目标 mm 换算格距像素
    void onFitScreen();          // 按目标屏物理分辨率取能放下整板的最大整数格距
    void onShowFullScreen();     // 在选中显示器全屏 1:1 显示
    void onExportPng();          // 导出精确像素 PNG
    void onExportPdf();          // 导出物理尺寸 PDF
    void onExportTxt();          // 导出参数说明

private:
    QScreen* selectedScreen() const;      // 当前选中的显示器
    double systemPixelPitchMm() const;    // 系统报告的点距 (25.4 / 物理DPI)
    double effectivePixelPitchMm() const; // 实际使用的点距 (用户覆盖优先)
    void refreshScreens();                // 刷新显示器列表

    CalibBoardView* m_pPreview = nullptr;
    QComboBox* m_pComboType = nullptr;
    QSpinBox* m_pSpinCols = nullptr;
    QSpinBox* m_pSpinRows = nullptr;
    QSpinBox* m_pSpinPitchPx = nullptr;
    QDoubleSpinBox* m_pSpinRatio = nullptr;
    QSpinBox* m_pSpinCodeSpacing = nullptr;      // DM 码间距 (格)
    QSpinBox* m_pSpinMargin = nullptr;
    QCheckBox* m_pChkInvert = nullptr;
    QComboBox* m_pComboScreen = nullptr;
    QDoubleSpinBox* m_pSpinPixelPitch = nullptr;  // 0 = 使用系统读数
    QLabel* m_pLabelSysPitch = nullptr;
    QDoubleSpinBox* m_pSpinTargetMm = nullptr;
    QDoubleSpinBox* m_pSpinPrintMm = nullptr;
    QLabel* m_pLabelInfo = nullptr;
    QPlainTextEdit* m_pTextDesc = nullptr;
};
