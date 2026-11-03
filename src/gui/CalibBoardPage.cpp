// 20261103 ZJH CalibBoardPage 实现
#include "gui/CalibBoardPage.h"

#include "gui/AlignView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>

// ============================================================
// CalibBoardView
// ============================================================
CalibBoardView::CalibBoardView(bool bExact, QWidget* pParent)
    : QWidget(pParent)
    , m_bExact(bExact)
{
    setMinimumSize(200, 150);
}

void CalibBoardView::setBoard(const QImage& image, const CalibBoardSpec& spec)
{
    m_image = image;
    m_spec = CalibBoard::normalized(spec);
    m_bInvert = m_spec.bInvert;
    update();
}

bool CalibBoardView::isCropped() const
{
    const double dDpr = devicePixelRatioF();  // 逻辑像素 → 物理像素倍率
    // 20261103 ZJH 与 paintEvent 一致按向下取整的物理尺寸判断
    return m_image.width() > std::floor(width() * dDpr) || m_image.height() > std::floor(height() * dDpr);
}

void CalibBoardView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // 20261103 ZJH 背景与标定板背景同色, 全屏时周围不出现杂色干扰识别
    painter.fillRect(rect(), m_bInvert ? Qt::black : Qt::white);
    if (m_image.isNull()) {
        return;
    }
    if (m_bExact) {
        // 20261103 ZJH 1:1 物理像素: 图像设 DPR 后按物理像素大小绘制; 偏移取整到物理像素, 避免插值模糊
        const double dDpr = devicePixelRatioF();
        QImage img = m_image;
        img.setDevicePixelRatio(dDpr);
        // 20261103 ZJH 物理尺寸必须向下取整: 150% 下逻辑 1067 × 1.5 = 1600.5, 四舍五入成 1601 会使
        //            板与屏尺寸差为奇数时居中偏移多 1 物理像素 (棋盘格实测整体下移 1px)
        const long nPhysW = static_cast<long>(std::floor(width() * dDpr));   // 窗口物理宽
        const long nPhysH = static_cast<long>(std::floor(height() * dDpr));  // 窗口物理高
        const long nOffX = (nPhysW - m_image.width()) / 2;  // 物理像素整数偏移 (可为负 = 裁切)
        const long nOffY = (nPhysH - m_image.height()) / 2;
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(QPointF(nOffX / dDpr, nOffY / dDpr), img);
        return;
    }
    // 20261103 ZJH 预览: 按窗口尺寸矢量重绘 (不放大位图, 圆点边缘清晰; 仅供查看, 不用于拍屏)
    const QSizeF sizePitch = CalibBoard::boardSizeInPitch(m_spec);  // 整板尺寸 (格距单位)
    const double dAvailW = std::max(1, width() - 16);   // 可用宽 (四周留 8px)
    const double dAvailH = std::max(1, height() - 16);  // 可用高
    const double u = std::min(dAvailW / sizePitch.width(), dAvailH / sizePitch.height());  // 每格距对应逻辑像素
    const QSizeF sizeFit(sizePitch.width() * u, sizePitch.height() * u);  // 预览整板尺寸
    const QPointF ptOrigin((width() - sizeFit.width()) / 2.0, (height() - sizeFit.height()) / 2.0);
    painter.save();
    painter.translate(ptOrigin);
    CalibBoard::paint(painter, m_spec, u, true);  // 圆点抗锯齿
    painter.restore();
    painter.setPen(QColor(150, 150, 150));
    painter.drawRect(QRectF(ptOrigin, sizeFit).adjusted(0, 0, -1, -1));
}

// ============================================================
// CalibBoardPage
// ============================================================
namespace {
// 20261103 ZJH 屏幕物理像素尺寸: 逻辑尺寸 × 缩放倍率向下取整
// (150% 缩放下 2560 物理像素对应逻辑 1706.67, Qt 取整为 1707, 直接相乘四舍五入会得到 2561)
QSize physicalScreenSize(const QScreen* pScreen)
{
    if (!pScreen) {
        return QSize();
    }
    const double dDpr = pScreen->devicePixelRatio();
    return QSize(static_cast<int>(std::floor(pScreen->geometry().width() * dDpr)),
                 static_cast<int>(std::floor(pScreen->geometry().height() * dDpr)));
}

QDoubleSpinBox* makeSpin(double dMin, double dMax, int nDecimals, double dValue, QWidget* pParent)
{
    auto* pSpin = new QDoubleSpinBox(pParent);
    pSpin->setRange(dMin, dMax);
    pSpin->setDecimals(nDecimals);
    pSpin->setValue(dValue);
    pSpin->setKeyboardTracking(false);
    return pSpin;  // 返回输入框
}

QSpinBox* makeIntSpin(int nMin, int nMax, int nValue, QWidget* pParent)
{
    auto* pSpin = new QSpinBox(pParent);
    pSpin->setRange(nMin, nMax);
    pSpin->setValue(nValue);
    pSpin->setKeyboardTracking(false);
    return pSpin;  // 返回输入框
}
}  // namespace

CalibBoardPage::CalibBoardPage(QWidget* pParent)
    : QWidget(pParent)
{
    auto* pMain = new QHBoxLayout(this);
    auto* pSplitter = new QSplitter(Qt::Horizontal, this);
    pMain->addWidget(pSplitter);
    m_pPreview = new CalibBoardView(false, pSplitter);
    pSplitter->addWidget(m_pPreview);

    auto* pScroll = new QScrollArea(pSplitter);
    pScroll->setWidgetResizable(true);
    pScroll->setMinimumWidth(460);
    auto* pPanel = new QWidget(pScroll);
    auto* pPanelLayout = new QVBoxLayout(pPanel);
    pScroll->setWidget(pPanel);
    pSplitter->addWidget(pScroll);
    pSplitter->setStretchFactor(0, 3);
    pSplitter->setStretchFactor(1, 2);

    // ---------- 图案参数 ----------
    auto* pGrpPattern = new QGroupBox(QStringLiteral("图案"), pPanel);
    auto* pForm = new QFormLayout(pGrpPattern);
    m_pComboType = new QComboBox(pGrpPattern);
    m_pComboType->addItems({ CalibBoard::typeName(BoardType::Chessboard), CalibBoard::typeName(BoardType::CircleSymmetric),
                             CalibBoard::typeName(BoardType::CircleAsymmetric), CalibBoard::typeName(BoardType::HalconCircle),
                             CalibBoard::typeName(BoardType::DmChessboard) });
    pForm->addRow(QStringLiteral("类型"), m_pComboType);
    m_pSpinCols = makeIntSpin(2, 200, 10, pGrpPattern);
    m_pSpinRows = makeIntSpin(2, 200, 7, pGrpPattern);
    auto* pRowCR = new QHBoxLayout();
    pRowCR->addWidget(m_pSpinCols);
    pRowCR->addWidget(new QLabel(QStringLiteral("×"), pGrpPattern));
    pRowCR->addWidget(m_pSpinRows);
    pForm->addRow(QStringLiteral("列 × 行"), pRowCR);
    m_pSpinPitchPx = makeIntSpin(4, 2000, 40, pGrpPattern);
    m_pSpinPitchPx->setSuffix(QStringLiteral(" px"));
    m_pSpinPitchPx->setToolTip(QStringLiteral("棋盘格方格边长 / 圆心距, 整数物理像素 (保证边缘锐利)"));
    pForm->addRow(QStringLiteral("格距"), m_pSpinPitchPx);
    m_pSpinRatio = makeSpin(0.1, 0.9, 2, 0.5, pGrpPattern);
    pForm->addRow(QStringLiteral("圆直径/圆心距"), m_pSpinRatio);
    m_pSpinCodeSpacing = makeIntSpin(4, 100, 10, pGrpPattern);
    m_pSpinCodeSpacing->setSingleStep(2);
    m_pSpinCodeSpacing->setSuffix(QStringLiteral(" 格"));
    m_pSpinCodeSpacing->setToolTip(QStringLiteral("DM 码间距 (偶数): 同排相邻码间隔, 相邻排错开一半; 康耐视板常用 10"));
    pForm->addRow(QStringLiteral("DM 码间距"), m_pSpinCodeSpacing);
    m_pSpinMargin = makeIntSpin(0, 10, 1, pGrpPattern);
    m_pSpinMargin->setSuffix(QStringLiteral(" 格"));
    pForm->addRow(QStringLiteral("四周留白"), m_pSpinMargin);
    m_pChkInvert = new QCheckBox(QStringLiteral("黑白反色"), pGrpPattern);
    pForm->addRow(m_pChkInvert);
    pPanelLayout->addWidget(pGrpPattern);

    // ---------- 显示器与物理尺寸 ----------
    auto* pGrpScreen = new QGroupBox(QStringLiteral("显示器与物理尺寸"), pPanel);
    auto* pScreenForm = new QFormLayout(pGrpScreen);
    m_pComboScreen = new QComboBox(pGrpScreen);
    pScreenForm->addRow(QStringLiteral("目标显示器"), m_pComboScreen);
    m_pSpinPixelPitch = makeSpin(0.0, 5.0, 5, 0.0, pGrpScreen);
    m_pSpinPixelPitch->setSuffix(QStringLiteral(" mm/px"));
    m_pSpinPixelPitch->setSpecialValueText(QStringLiteral("使用系统读数"));
    m_pSpinPixelPitch->setToolTip(QStringLiteral("显示器像素点距; 系统读数常不准, 建议按规格书或用尺量 N 个格子换算后填写"));
    auto* pBtnSys = new QPushButton(QStringLiteral("用系统读数"), pGrpScreen);
    auto* pPitchRow = new QHBoxLayout();
    pPitchRow->addWidget(m_pSpinPixelPitch);
    pPitchRow->addWidget(pBtnSys);
    pScreenForm->addRow(QStringLiteral("像素点距"), pPitchRow);
    m_pLabelSysPitch = new QLabel(pGrpScreen);
    pScreenForm->addRow(QStringLiteral("系统参考"), m_pLabelSysPitch);
    m_pSpinTargetMm = makeSpin(0.01, 1000, 3, 5.0, pGrpScreen);
    m_pSpinTargetMm->setSuffix(QStringLiteral(" mm"));
    auto* pBtnTarget = new QPushButton(QStringLiteral("换算格距"), pGrpScreen);
    auto* pTargetRow = new QHBoxLayout();
    pTargetRow->addWidget(m_pSpinTargetMm);
    pTargetRow->addWidget(pBtnTarget);
    pScreenForm->addRow(QStringLiteral("按目标格距"), pTargetRow);
    m_pLabelInfo = new QLabel(pGrpScreen);
    m_pLabelInfo->setWordWrap(true);
    m_pLabelInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pScreenForm->addRow(m_pLabelInfo);
    // 20261103 ZJH 铺满目标屏: 改的是格距参数本身 (整数像素), 保证显示/导出/视觉参数一致
    auto* pBtnFit = new QPushButton(QStringLiteral("铺满目标屏 (取最大整数格距)"), pGrpScreen);
    pBtnFit->setToolTip(QStringLiteral("按目标显示器物理分辨率, 计算能完整放下整板 (含留白) 的最大整数格距"));
    pScreenForm->addRow(pBtnFit);
    auto* pBtnFull = new QPushButton(QStringLiteral("在目标显示器全屏显示 (1:1 物理像素, Esc 退出)"), pGrpScreen);
    pScreenForm->addRow(pBtnFull);
    pPanelLayout->addWidget(pGrpScreen);

    // ---------- 视觉软件参数 ----------
    auto* pGrpDesc = new QGroupBox(QStringLiteral("视觉软件参数 (按上面的点距换算)"), pPanel);
    auto* pDescLayout = new QVBoxLayout(pGrpDesc);
    m_pTextDesc = new QPlainTextEdit(pGrpDesc);
    m_pTextDesc->setReadOnly(true);
    m_pTextDesc->setMinimumHeight(150);
    m_pTextDesc->setStyleSheet(QStringLiteral("font-family: Consolas, monospace;"));
    pDescLayout->addWidget(m_pTextDesc);
    pPanelLayout->addWidget(pGrpDesc);

    // ---------- 导出 ----------
    auto* pGrpExport = new QGroupBox(QStringLiteral("导出"), pPanel);
    auto* pExportForm = new QFormLayout(pGrpExport);
    m_pSpinPrintMm = makeSpin(0.1, 1000, 3, 10.0, pGrpExport);
    m_pSpinPrintMm->setSuffix(QStringLiteral(" mm"));
    m_pSpinPrintMm->setToolTip(QStringLiteral("PDF 打印用的格距; 打印时选「实际大小 / 100%」, 打印后用卡尺复核"));
    pExportForm->addRow(QStringLiteral("PDF 打印格距"), m_pSpinPrintMm);
    auto* pExportRow = new QHBoxLayout();
    auto* pBtnPng = new QPushButton(QStringLiteral("导出 PNG"), pGrpExport);
    auto* pBtnPdf = new QPushButton(QStringLiteral("导出 PDF"), pGrpExport);
    auto* pBtnTxt = new QPushButton(QStringLiteral("导出参数 TXT"), pGrpExport);
    pExportRow->addWidget(pBtnPng);
    pExportRow->addWidget(pBtnPdf);
    pExportRow->addWidget(pBtnTxt);
    pExportForm->addRow(pExportRow);
    pPanelLayout->addWidget(pGrpExport);

    // ---------- 提示 ----------
    auto* pTip = new QLabel(QStringLiteral(
        "<b>拍屏内参标定须知</b><br>"
        "1. 屏幕是固定平面: 必须<b>物理倾斜相机或显示器</b>拍 10~20 个姿态 (含 ±30° 左右倾斜), 只拍正对的屏幕解不出焦距/主点; "
        "镜头畸变可由单平面估计。<br>"
        "2. 点距决定物理尺寸: 用尺量全屏显示的 N 个格子, 点距 = 实测 mm / (N × 格距px)。<br>"
        "3. 相机略微虚焦或调低屏幕亮度可减轻摩尔纹; 不要开护眼/夜间模式。<br>"
        "4. 需要更高精度时导出 PDF 打印, 贴在平整硬板上使用。"), pPanel);
    pTip->setWordWrap(true);
    pTip->setStyleSheet(QStringLiteral("color: #555;"));
    pPanelLayout->addWidget(pTip);
    pPanelLayout->addStretch();

    // ---------- 信号 ----------
    for (QSpinBox* pSpin : { m_pSpinCols, m_pSpinRows, m_pSpinPitchPx, m_pSpinMargin }) {
        connect(pSpin, qOverload<int>(&QSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    }
    connect(m_pSpinRatio, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pSpinCodeSpacing, qOverload<int>(&QSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pSpinPixelPitch, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pComboType, qOverload<int>(&QComboBox::currentIndexChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pChkInvert, &QCheckBox::toggled, this, &CalibBoardPage::onParamsChanged);
    connect(m_pComboScreen, qOverload<int>(&QComboBox::currentIndexChanged), this, &CalibBoardPage::onScreenChanged);
    connect(pBtnSys, &QPushButton::clicked, this, &CalibBoardPage::onUseSystemPitch);
    connect(pBtnTarget, &QPushButton::clicked, this, &CalibBoardPage::onApplyTargetMm);
    connect(pBtnFit, &QPushButton::clicked, this, &CalibBoardPage::onFitScreen);  // 点击 → 格距铺满目标屏
    connect(pBtnFull, &QPushButton::clicked, this, &CalibBoardPage::onShowFullScreen);
    connect(pBtnPng, &QPushButton::clicked, this, &CalibBoardPage::onExportPng);
    connect(pBtnPdf, &QPushButton::clicked, this, &CalibBoardPage::onExportPdf);
    connect(pBtnTxt, &QPushButton::clicked, this, &CalibBoardPage::onExportTxt);
    // 20261103 ZJH 插拔显示器时刷新列表
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &CalibBoardPage::refreshScreens);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &CalibBoardPage::refreshScreens);

    refreshScreens();
    onParamsChanged();
}

void CalibBoardPage::refreshScreens()
{
    const QString strKeep = m_pComboScreen->currentText();
    const QSignalBlocker blocker(m_pComboScreen);
    m_pComboScreen->clear();
    const QList<QScreen*> lstScreens = QGuiApplication::screens();
    for (int i = 0; i < lstScreens.size(); ++i) {  // 每块屏: 名称 + 物理分辨率 + 缩放
        QScreen* pScreen = lstScreens[i];
        const double dDpr = pScreen->devicePixelRatio();
        const QSize sizePhys = physicalScreenSize(pScreen);
        m_pComboScreen->addItem(QStringLiteral("%1: %2  %3×%4  缩放 %5%")
                                    .arg(i + 1).arg(pScreen->name()).arg(sizePhys.width()).arg(sizePhys.height())
                                    .arg(std::lround(dDpr * 100)));
    }
    const int nIndex = m_pComboScreen->findText(strKeep);
    m_pComboScreen->setCurrentIndex(nIndex >= 0 ? nIndex : 0);
    onScreenChanged();
}

QScreen* CalibBoardPage::selectedScreen() const
{
    const QList<QScreen*> lstScreens = QGuiApplication::screens();
    const int nIndex = m_pComboScreen->currentIndex();
    // 20261103 ZJH 越界退回主屏
    return (nIndex >= 0 && nIndex < lstScreens.size()) ? lstScreens[nIndex] : QGuiApplication::primaryScreen();
}

double CalibBoardPage::systemPixelPitchMm() const
{
    QScreen* pScreen = selectedScreen();
    // 20261103 ZJH 物理 DPI 按物理像素计: physicalDotsPerInch 是逻辑像素/英寸, 乘缩放倍率得物理像素/英寸
    const double dDpi = pScreen ? pScreen->physicalDotsPerInch() * pScreen->devicePixelRatio() : 0.0;
    return dDpi > 1.0 ? 25.4 / dDpi : 0.25;  // 读不到时按常见 0.25mm 估
}

double CalibBoardPage::effectivePixelPitchMm() const
{
    return m_pSpinPixelPitch->value() > 0.0 ? m_pSpinPixelPitch->value() : systemPixelPitchMm();
}

void CalibBoardPage::onScreenChanged()
{
    QScreen* pScreen = selectedScreen();
    const QSizeF sizeMm = pScreen ? pScreen->physicalSize() : QSizeF();
    m_pLabelSysPitch->setText(QStringLiteral("%1 mm/px  (系统报告屏幕 %2×%3 mm, 常有误差)")
                                  .arg(systemPixelPitchMm(), 0, 'f', 5).arg(sizeMm.width(), 0, 'f', 0).arg(sizeMm.height(), 0, 'f', 0));
    onParamsChanged();
}

void CalibBoardPage::onUseSystemPitch()
{
    m_pSpinPixelPitch->setValue(systemPixelPitchMm());  // 写入后可再微调
}

void CalibBoardPage::onApplyTargetMm()
{
    // 20261103 ZJH 目标 mm → 整数像素 (四舍五入), 实际 mm 在信息栏显示
    const int nPx = static_cast<int>(std::lround(m_pSpinTargetMm->value() / effectivePixelPitchMm()));
    m_pSpinPitchPx->setValue(std::max(4, nPx));
}

void CalibBoardPage::onFitScreen()
{
    // 20261103 ZJH 最大整数格距 = floor(min(屏物理宽 / 整板格数宽, 屏物理高 / 整板格数高))
    const QSize sizeScreen = physicalScreenSize(selectedScreen());  // 目标屏物理像素
    if (sizeScreen.isEmpty()) {
        return;  // 读不到屏幕尺寸, 不改格距
    }
    const QSizeF sizePitch = CalibBoard::boardSizeInPitch(spec());  // 整板尺寸 (格距单位, 含留白)
    const double dMax = std::min(sizeScreen.width() / sizePitch.width(), sizeScreen.height() / sizePitch.height());
    const int nPx = static_cast<int>(std::floor(dMax));  // 向下取整, 保证放得下
    // 20261103 ZJH 夹到输入框范围; 太小 (行列过多) 时退到下限, 信息栏会提示裁切
    m_pSpinPitchPx->setValue(std::clamp(nPx, m_pSpinPitchPx->minimum(), m_pSpinPitchPx->maximum()));
}

CalibBoardSpec CalibBoardPage::spec() const
{
    CalibBoardSpec spec;
    spec.eType = static_cast<BoardType>(m_pComboType->currentIndex());
    spec.nCols = m_pSpinCols->value();
    spec.nRows = m_pSpinRows->value();
    spec.nPitchPx = m_pSpinPitchPx->value();
    spec.dCircleRatio = m_pSpinRatio->value();
    spec.nCodeSpacing = m_pSpinCodeSpacing->value();
    // 20261103 ZJH 码内格距 = 格距像素 × 实际点距 (屏幕显示与 PNG 一致)
    spec.dCodePitchMm = spec.nPitchPx * effectivePixelPitchMm();
    spec.nMarginPitch = m_pSpinMargin->value();
    spec.bInvert = m_pChkInvert->isChecked();
    return CalibBoard::normalized(spec);  // 返回合法化参数
}

double CalibBoardPage::pixelPitchSetting() const
{
    return m_pSpinPixelPitch->value();
}

double CalibBoardPage::printPitchMm() const
{
    return m_pSpinPrintMm->value();
}

void CalibBoardPage::setSettings(const CalibBoardSpec& specIn, double dPixelPitchMm, double dPrintPitchMm)
{
    const CalibBoardSpec spec = CalibBoard::normalized(specIn);
    {
        // 20261103 ZJH 批量回填时屏蔽信号, 最后统一刷新一次
        const QSignalBlocker b1(m_pComboType), b2(m_pSpinCols), b3(m_pSpinRows), b4(m_pSpinPitchPx), b5(m_pSpinRatio),
            b6(m_pSpinMargin), b7(m_pChkInvert), b8(m_pSpinPixelPitch), b9(m_pSpinCodeSpacing);
        m_pComboType->setCurrentIndex(static_cast<int>(spec.eType));
        m_pSpinCols->setValue(spec.nCols);
        m_pSpinRows->setValue(spec.nRows);
        m_pSpinPitchPx->setValue(spec.nPitchPx);
        m_pSpinRatio->setValue(spec.dCircleRatio);
        m_pSpinCodeSpacing->setValue(spec.nCodeSpacing);
        m_pSpinMargin->setValue(spec.nMarginPitch);
        m_pChkInvert->setChecked(spec.bInvert);
        m_pSpinPixelPitch->setValue(std::max(0.0, dPixelPitchMm));
    }
    m_pSpinPrintMm->setValue(dPrintPitchMm > 0.0 ? dPrintPitchMm : 10.0);
    onParamsChanged();
}

void CalibBoardPage::onParamsChanged()
{
    const CalibBoardSpec board = spec();
    const bool bCircle = board.eType != BoardType::Chessboard && board.eType != BoardType::DmChessboard;
    m_pSpinRatio->setEnabled(bCircle);  // 直径比只对圆点有效
    m_pSpinCodeSpacing->setEnabled(board.eType == BoardType::DmChessboard);  // 码间距只对 DM 码棋盘格有效
    const double dPitchMm = effectivePixelPitchMm();
    m_pPreview->setBoard(CalibBoard::render(board, dPitchMm), board);

    // 20261103 ZJH 信息栏: 像素尺寸 / 物理尺寸 / 是否放得下目标屏
    const QSize sizePx = CalibBoard::boardSizePx(board);
    QScreen* pScreen = selectedScreen();
    const QSize sizeScreen = physicalScreenSize(pScreen);
    const bool bFits = sizePx.width() <= sizeScreen.width() && sizePx.height() <= sizeScreen.height();
    QString strInfo = QStringLiteral("整板 %1×%2 px = %3×%4 mm;  格距 %5 px = %6 mm (点距 %7 mm/px%8)")
                          .arg(sizePx.width()).arg(sizePx.height())
                          .arg(sizePx.width() * dPitchMm, 0, 'f', 2).arg(sizePx.height() * dPitchMm, 0, 'f', 2)
                          .arg(board.nPitchPx).arg(board.nPitchPx * dPitchMm, 0, 'f', 4).arg(dPitchMm, 0, 'f', 5)
                          .arg(m_pSpinPixelPitch->value() > 0.0 ? QString() : QStringLiteral(", 系统读数"));
    if (!bFits) {
        strInfo += QStringLiteral("<br><font color=red>超出目标屏 %1×%2 物理像素, 全屏时会被裁切; 请减小格距或行列数</font>")
                       .arg(sizeScreen.width()).arg(sizeScreen.height());
    }
    m_pLabelInfo->setText(strInfo);
    m_pTextDesc->setPlainText(CalibBoard::describe(board, board.nPitchPx * dPitchMm));
}

void CalibBoardPage::onShowFullScreen()
{
    QScreen* pScreen = selectedScreen();
    if (!pScreen) {
        return;
    }
    const CalibBoardSpec board = spec();
    auto* pView = new CalibBoardView(true, nullptr);
    pView->setBoard(CalibBoard::render(board, effectivePixelPitchMm()), board);
    auto* pWin = new ViewWindow(pView, QStringLiteral("标定板 %1").arg(CalibBoard::typeName(board.eType)), nullptr);
    // 20261103 ZJH 先把窗口放到目标屏再全屏, 否则会在主屏全屏
    pWin->setGeometry(pScreen->geometry().adjusted(50, 50, -50, -50));
    pWin->winId();  // 创建原生窗口句柄
    if (pWin->windowHandle()) {
        pWin->windowHandle()->setScreen(pScreen);
    }
    pWin->showFullScreen();
    // 20261103 ZJH 置顶并激活: 避免全屏窗口被其他窗口压住, 同时让 Esc/F11 键盘焦点落在标定板窗口
    pWin->raise();
    pWin->activateWindow();
}

void CalibBoardPage::onExportPng()
{
    const CalibBoardSpec board = spec();
    const QString strPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 PNG"),
        QStringLiteral("标定板_%1_%2x%3_%4px.png").arg(CalibBoard::typeName(board.eType)).arg(board.nCols).arg(board.nRows).arg(board.nPitchPx),
        QStringLiteral("PNG (*.png)"));
    // 20261103 ZJH 用户取消
    if (strPath.isEmpty()) {
        return;
    }
    if (!CalibBoard::render(board, effectivePixelPitchMm()).save(strPath, "PNG")) {
        QMessageBox::warning(this, QStringLiteral("导出 PNG"), QStringLiteral("保存失败: %1").arg(strPath));
    }
}

void CalibBoardPage::onExportPdf()
{
    const CalibBoardSpec board = spec();
    const QString strPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 PDF"),
        QStringLiteral("标定板_%1_%2x%3_%4mm.pdf").arg(CalibBoard::typeName(board.eType)).arg(board.nCols).arg(board.nRows)
            .arg(m_pSpinPrintMm->value()),
        QStringLiteral("PDF (*.pdf)"));
    if (strPath.isEmpty()) {
        return;
    }
    QString strErr;
    if (!CalibBoard::exportPdf(board, m_pSpinPrintMm->value(), strPath, strErr)) {
        QMessageBox::warning(this, QStringLiteral("导出 PDF"), strErr);
    }
}

void CalibBoardPage::onExportTxt()
{
    const CalibBoardSpec board = spec();
    const QString strPath = QFileDialog::getSaveFileName(this, QStringLiteral("导出参数"),
                                                         QStringLiteral("标定板参数.txt"), QStringLiteral("文本 (*.txt)"));
    if (strPath.isEmpty()) {
        return;
    }
    QFile file(strPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("导出参数"), file.errorString());
        return;
    }
    // 20261103 ZJH 同时写屏幕显示与 PDF 打印两套物理尺寸
    QString strText = QStringLiteral("===== 屏幕显示 (点距 %1 mm/px) =====\n").arg(effectivePixelPitchMm(), 0, 'f', 5);
    strText += CalibBoard::describe(board, board.nPitchPx * effectivePixelPitchMm());
    strText += QStringLiteral("\n===== PDF 打印 =====\n") + CalibBoard::describe(board, m_pSpinPrintMm->value());
    file.write("\xEF\xBB\xBF");
    file.write(strText.toUtf8());
}
