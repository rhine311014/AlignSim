// 20261103 ZJH CalibBoardPage 实现
#include "gui/CalibBoardPage.h"

#include "gui/AlignView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace {
// 20261106 ZJH 15 组相机内参标定标准 3D 姿态 (Pitch, Yaw, Roll, 名称)
struct Pose3DPreset {
    double dPitch;
    double dYaw;
    double dRoll;
    const char* szName;
};

constexpr Pose3DPreset kStandardPoses[] = {
    {   0.0,   0.0,   0.0, "01: 正对基准 (0°, 0°, 0°)" },
    {  22.0,   0.0,   0.0, "02: 上仰 +22° (Pitch +22°)" },
    { -22.0,   0.0,   0.0, "03: 下俯 -22° (Pitch -22°)" },
    {   0.0,  22.0,   0.0, "04: 左偏 +22° (Yaw +22°)" },
    {   0.0, -22.0,   0.0, "05: 右偏 -22° (Yaw -22°)" },
    {  18.0,  18.0,   8.0, "06: 左上斜倾 (+18°, +18°, +8°)" },
    {  18.0, -18.0,  -8.0, "07: 右上斜倾 (+18°, -18°, -8°)" },
    { -18.0,  18.0,  -8.0, "08: 左下斜倾 (-18°, +18°, -8°)" },
    { -18.0, -18.0,   8.0, "09: 右下斜倾 (-18°, -18°, +8°)" },
    {  30.0,   0.0,  15.0, "10: 大倾角上仰 (+30°, 0°, +15°)" },
    { -30.0,   0.0, -15.0, "11: 大倾角下俯 (-30°, 0°, -15°)" },
    {   0.0,  30.0, -15.0, "12: 大倾角左偏 (0°, +30°, -15°)" },
    {   0.0, -30.0,  15.0, "13: 大倾角右偏 (0°, -30°, +15°)" },
    {  24.0, -20.0,  25.0, "14: 复合旋转 A (+24°, -20°, +25°)" },
    { -24.0,  20.0, -25.0, "15: 复合旋转 B (-24°, +20°, -25°)" },
};
constexpr int kStandardPoseCount = static_cast<int>(sizeof(kStandardPoses) / sizeof(kStandardPoses[0]));

// 20261106 ZJH 构造绕中心点 (cx, cy) 的 3D 透视单应性变换 QTransform
QTransform makePerspectiveTransform(double cx, double cy, double dPitchDeg, double dYawDeg, double dRollDeg)
{
    QTransform tr;
    tr.translate(cx, cy);
    if (std::abs(dRollDeg) > 1e-4) {
        tr.rotate(dRollDeg, Qt::ZAxis);
    }
    if (std::abs(dPitchDeg) > 1e-4) {
        tr.rotate(dPitchDeg, Qt::XAxis);
    }
    if (std::abs(dYawDeg) > 1e-4) {
        tr.rotate(dYawDeg, Qt::YAxis);
    }
    tr.translate(-cx, -cy);
    return tr;
}
}  // namespace

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

void CalibBoardView::setPose3D(double dPitchDeg, double dYawDeg, double dRollDeg)
{
    m_dPitchDeg = dPitchDeg;
    m_dYawDeg = dYawDeg;
    m_dRollDeg = dRollDeg;
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
    const bool bHas3D = (std::abs(m_dPitchDeg) > 1e-4 || std::abs(m_dYawDeg) > 1e-4 || std::abs(m_dRollDeg) > 1e-4);
    if (m_bExact) {
        // 20261103 ZJH 1:1 物理像素: 图像设 DPR 后按物理像素大小绘制; 偏移取整到物理像素, 避免插值模糊
        const double dDpr = devicePixelRatioF();
        QImage img = m_image;
        img.setDevicePixelRatio(dDpr);
        const long nPhysW = static_cast<long>(std::floor(width() * dDpr));   // 窗口物理宽
        const long nPhysH = static_cast<long>(std::floor(height() * dDpr));  // 窗口物理高
        const long nOffX = (nPhysW - m_image.width()) / 2;  // 物理像素整数偏移 (可为负 = 裁切)
        const long nOffY = (nPhysH - m_image.height()) / 2;
        if (bHas3D) {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.setWorldTransform(makePerspectiveTransform(width() * 0.5, height() * 0.5, m_dPitchDeg, m_dYawDeg, m_dRollDeg), true);
        } else {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        }
        painter.drawImage(QPointF(nOffX / dDpr, nOffY / dDpr), img);
        // 20261109 ZJH 拍屏窗口带模拟倾斜时左上角打红字: 防止拿屏上画的倾斜给真相机标内参 (现场重投影 15.9 px)
        if (bHas3D) {
            painter.resetTransform();
            QFont font = painter.font();
            font.setPixelSize(18);
            painter.setFont(font);
            const QString strWarn = QStringLiteral("屏上模拟倾斜, 不能用于真实相机内参标定: 请「姿态归零」并物理倾斜相机或显示器");
            const QRect rcText = painter.fontMetrics().boundingRect(strWarn).adjusted(-8, -4, 8, 4).translated(12 + 8, 12 + 4 + painter.fontMetrics().ascent());
            painter.fillRect(rcText, QColor(255, 255, 255, 230));  // 白底保证可读
            painter.setPen(QColor(220, 38, 38));
            painter.drawText(rcText, Qt::AlignCenter, strWarn);
        }
        return;
    }
    // 20261103 ZJH 预览: 按窗口尺寸矢量重绘 (不放大位图, 圆点边缘清晰; 仅供查看, 不用于拍屏)
    const QSizeF sizePitch = CalibBoard::boardSizeInPitch(m_spec);  // 整板尺寸 (格距单位)
    const double dAvailW = std::max(1, width() - 24);   // 可用宽
    const double dAvailH = std::max(1, height() - 24);  // 可用高
    const double u = std::min(dAvailW / sizePitch.width(), dAvailH / sizePitch.height());  // 每格距对应逻辑像素
    const QSizeF sizeFit(sizePitch.width() * u, sizePitch.height() * u);  // 预览整板尺寸
    const QPointF ptOrigin((width() - sizeFit.width()) / 2.0, (height() - sizeFit.height()) / 2.0);
    painter.save();
    if (bHas3D) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setWorldTransform(makePerspectiveTransform(width() * 0.5, height() * 0.5, m_dPitchDeg, m_dYawDeg, m_dRollDeg), true);
    }
    painter.translate(ptOrigin);
    CalibBoard::paint(painter, m_spec, u, true);  // 圆点抗锯齿
    painter.setPen(QColor(150, 150, 150));
    painter.drawRect(QRectF(QPointF(0, 0), sizeFit).adjusted(0, 0, -1, -1));
    painter.restore();
}

// ============================================================
// CalibBoardPage
// ============================================================
namespace {
// 20261103 ZJH 屏幕物理像素尺寸: 逻辑尺寸 × 缩放倍率向下取整
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
    return pSpin;
}

QSpinBox* makeIntSpin(int nMin, int nMax, int nValue, QWidget* pParent)
{
    auto* pSpin = new QSpinBox(pParent);
    pSpin->setRange(nMin, nMax);
    pSpin->setValue(nValue);
    pSpin->setKeyboardTracking(false);
    return pSpin;
}
}  // namespace

CalibBoardPage::CalibBoardPage(QWidget* pParent)
    : QWidget(pParent)
{
    auto* pMain = new QHBoxLayout(this);
    pMain->setContentsMargins(10, 8, 10, 8);
    pMain->setSpacing(10);
    auto* pSplitter = new QSplitter(Qt::Horizontal, this);
    pMain->addWidget(pSplitter);

    // ====================================================================
    // 左侧: 顶部标定板快捷工具栏 + 3D 透视姿态栏 + 标定板大画布预览
    // ====================================================================
    auto* pLeftHost = new QWidget(pSplitter);
    auto* pLeftLayout = new QVBoxLayout(pLeftHost);
    pLeftLayout->setContentsMargins(0, 0, 0, 0);
    pLeftLayout->setSpacing(6);

    auto* pToolbarCard = new QFrame(pLeftHost);
    pToolbarCard->setObjectName(QStringLiteral("ToolbarCard"));
    auto* pToolbarLayout = new QVBoxLayout(pToolbarCard);
    pToolbarLayout->setContentsMargins(10, 8, 10, 8);
    pToolbarLayout->setSpacing(6);

    // 第一行: 图案类型 + 列×行 + 格距 + 反色 + 铺满目标屏 + 全屏 1:1 显示
    auto* pTopRow1 = new QHBoxLayout();
    pTopRow1->setSpacing(8);
    m_pComboType = new QComboBox(pToolbarCard);
    m_pComboType->addItems({ CalibBoard::typeName(BoardType::Chessboard), CalibBoard::typeName(BoardType::CircleSymmetric),
                             CalibBoard::typeName(BoardType::CircleAsymmetric), CalibBoard::typeName(BoardType::HalconCircle),
                             CalibBoard::typeName(BoardType::DmChessboard) });
    m_pSpinCols = makeIntSpin(2, 200, 10, pToolbarCard);
    m_pSpinRows = makeIntSpin(2, 200, 7, pToolbarCard);
    m_pSpinPitchPx = makeIntSpin(4, 2000, 40, pToolbarCard);
    m_pSpinPitchPx->setSuffix(QStringLiteral(" px"));
    m_pSpinPitchPx->setToolTip(QStringLiteral("棋盘格方格边长 / 圆心距, 整数物理像素 (保证边缘锐利)"));
    m_pChkInvert = new QCheckBox(QStringLiteral("黑白反色"), pToolbarCard);
    auto* pBtnFit = new QPushButton(QStringLiteral("铺满目标屏"), pToolbarCard);
    pBtnFit->setToolTip(QStringLiteral("按目标显示器物理分辨率, 计算能完整放下整板 (含留白) 的最大整数格距; 全屏显示时会自动执行"));
    // 20261109 ZJH 格距受宽高比限制后, 另一方向自动加列/行占满 (会改动列×行)
    m_pChkFitGrow = new QCheckBox(QStringLiteral("自动补行列"), pToolbarCard);
    m_pChkFitGrow->setChecked(true);
    m_pChkFitGrow->setToolTip(QStringLiteral("铺满时格距定好后, 把列数/行数加到屏上放得下的最大值, 让标定板两个方向都占满屏幕"));
    auto* pBtnFull = new QPushButton(QStringLiteral("全屏 1:1 显示 (Esc退出)"), pToolbarCard);
    pBtnFull->setProperty("primary", true);
    pBtnFull->setToolTip(QStringLiteral("在目标显示器全屏 1:1 物理像素显示标定板, 支持与下方 3D 姿态轮播实时联动"));

    pTopRow1->addWidget(new QLabel(QStringLiteral("图案:"), pToolbarCard));
    pTopRow1->addWidget(m_pComboType);
    pTopRow1->addWidget(new QLabel(QStringLiteral("列×行:"), pToolbarCard));
    pTopRow1->addWidget(m_pSpinCols);
    pTopRow1->addWidget(new QLabel(QStringLiteral("×"), pToolbarCard));
    pTopRow1->addWidget(m_pSpinRows);
    pTopRow1->addWidget(new QLabel(QStringLiteral("格距:"), pToolbarCard));
    pTopRow1->addWidget(m_pSpinPitchPx);
    pTopRow1->addWidget(m_pChkInvert);
    pTopRow1->addStretch();
    pTopRow1->addWidget(m_pChkFitGrow);
    pTopRow1->addWidget(pBtnFit);
    pTopRow1->addWidget(pBtnFull);
    pToolbarLayout->addLayout(pTopRow1);

    // 第二行: 3D 多角度透视姿态模拟
    // 20261109 ZJH 只用于导出合成 PNG 离线测算法; 屏上画的倾斜对真相机仍是正对平面, 不能代替物理倾斜做内参标定
    auto* pTopRow2 = new QHBoxLayout();
    pTopRow2->setSpacing(6);
    m_pSpinPitchDeg = makeSpin(-60.0, 60.0, 1, 0.0, pToolbarCard);
    m_pSpinPitchDeg->setSuffix(QStringLiteral("°"));
    m_pSpinPitchDeg->setSingleStep(2.0);
    m_pSpinPitchDeg->setKeyboardTracking(true);
    m_pSpinYawDeg = makeSpin(-60.0, 60.0, 1, 0.0, pToolbarCard);
    m_pSpinYawDeg->setSuffix(QStringLiteral("°"));
    m_pSpinYawDeg->setSingleStep(2.0);
    m_pSpinYawDeg->setKeyboardTracking(true);
    m_pSpinRollDeg = makeSpin(-180.0, 180.0, 1, 0.0, pToolbarCard);
    m_pSpinRollDeg->setSuffix(QStringLiteral("°"));
    m_pSpinRollDeg->setSingleStep(5.0);
    m_pSpinRollDeg->setKeyboardTracking(true);

    m_pComboPosePreset = new QComboBox(pToolbarCard);
    for (int i = 0; i < kStandardPoseCount; ++i) {
        m_pComboPosePreset->addItem(QString::fromUtf8(kStandardPoses[i].szName));
    }
    auto* pBtnResetPose = new QPushButton(QStringLiteral("姿态归零"), pToolbarCard);
    // 20261108 ZJH 手动切到下一个标准姿态 (第 15 个之后回到第 1 个), 全屏窗口内也可按 → / 空格
    auto* pBtnNextPose = new QPushButton(QStringLiteral("下一姿态 ▶"), pToolbarCard);
    pBtnNextPose->setToolTip(QStringLiteral("切到下一个 3D 姿态 (预览与全屏窗口同步); 全屏时可按 → / 空格 下一张, ← 上一张"));
    auto* pBtnAutoPose = new QPushButton(QStringLiteral("⚡ 15姿态自动轮播"), pToolbarCard);
    pBtnAutoPose->setCheckable(true);
    // 20261109 ZJH 纠正: 屏上模拟倾斜不能给真相机标内参, 只用于查看/导出合成图
    pBtnAutoPose->setToolTip(QStringLiteral("每隔 1.2 秒自动切换一个 3D 倾斜姿态 (预览与全屏窗口同步切换)。\n"
                                            "注意: 屏上画出的倾斜对真实相机仍是正对平面, 不能用于真相机内参标定;\n"
                                            "真相机请姿态归零并物理倾斜相机或显示器。合成图请用「一键批量导出 15 姿态」。"));

    pTopRow2->addWidget(new QLabel(QStringLiteral("3D姿态 俯仰X:"), pToolbarCard));
    pTopRow2->addWidget(m_pSpinPitchDeg);
    pTopRow2->addWidget(new QLabel(QStringLiteral("偏航Y:"), pToolbarCard));
    pTopRow2->addWidget(m_pSpinYawDeg);
    pTopRow2->addWidget(new QLabel(QStringLiteral("滚转Z:"), pToolbarCard));
    pTopRow2->addWidget(m_pSpinRollDeg);
    pTopRow2->addWidget(m_pComboPosePreset, 1);
    pTopRow2->addWidget(pBtnResetPose);
    pTopRow2->addWidget(pBtnNextPose);  // 20261108 ZJH 位于归零与自动轮播之间
    pTopRow2->addWidget(pBtnAutoPose);
    pToolbarLayout->addLayout(pTopRow2);
    pLeftLayout->addWidget(pToolbarCard);

    m_pPreview = new CalibBoardView(false, pLeftHost);
    pLeftLayout->addWidget(m_pPreview, 1);

    // ====================================================================
    // 右侧: 物理尺寸看板 + 分页配置与算子说明 (无外层滚动条)
    // ====================================================================
    auto* pRightPanel = new QWidget(pSplitter);
    auto* pRightLayout = new QVBoxLayout(pRightPanel);
    pRightLayout->setContentsMargins(2, 0, 0, 0);
    pRightLayout->setSpacing(8);
    pRightPanel->setMinimumWidth(460);

    // 1. 物理尺寸与显示器点距实时看板
    auto* pGrpInfo = new QGroupBox(QStringLiteral("标定板物理尺寸与目标屏状态"), pRightPanel);
    auto* pInfoLayout = new QVBoxLayout(pGrpInfo);
    pInfoLayout->setSpacing(6);
    m_pLabelInfo = new QLabel(pGrpInfo);
    m_pLabelInfo->setWordWrap(true);
    m_pLabelInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pLabelInfo->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 12px; color: #0F172A;"));
    m_pLabelSysPitch = new QLabel(pGrpInfo);
    m_pLabelSysPitch->setStyleSheet(QStringLiteral("color: #475569; font-size: 12px;"));
    pInfoLayout->addWidget(m_pLabelInfo);
    pInfoLayout->addWidget(m_pLabelSysPitch);
    pRightLayout->addWidget(pGrpInfo);

    // 2. 分页配置区
    auto* pCfgTabs = new QTabWidget(pRightPanel);

    // ---- Tab 1: 标定板细节、显示器与导出 ----
    auto* pTabParam = new QWidget(pCfgTabs);
    auto* pParamLayout = new QVBoxLayout(pTabParam);
    pParamLayout->setContentsMargins(8, 8, 8, 8);
    pParamLayout->setSpacing(10);

    auto* pGrpDetail = new QGroupBox(QStringLiteral("图案细节与显示器点距校准"), pTabParam);
    auto* pDetailForm = new QFormLayout(pGrpDetail);
    pDetailForm->setSpacing(8);
    m_pSpinRatio = makeSpin(0.1, 0.9, 2, 0.5, pGrpDetail);
    pDetailForm->addRow(QStringLiteral("圆直径 / 圆心距"), m_pSpinRatio);
    m_pSpinCodeSpacing = makeIntSpin(4, 100, 10, pGrpDetail);
    m_pSpinCodeSpacing->setSingleStep(2);
    m_pSpinCodeSpacing->setSuffix(QStringLiteral(" 格"));
    m_pSpinCodeSpacing->setToolTip(QStringLiteral("DM 码间距 (偶数): 同排相邻码间隔, 相邻排错开一半; 康耐视板常用 10"));
    pDetailForm->addRow(QStringLiteral("DM 码间距"), m_pSpinCodeSpacing);
    m_pSpinMargin = makeIntSpin(0, 10, 1, pGrpDetail);
    m_pSpinMargin->setSuffix(QStringLiteral(" 格"));
    pDetailForm->addRow(QStringLiteral("四周静区留白"), m_pSpinMargin);

    m_pComboScreen = new QComboBox(pGrpDetail);
    pDetailForm->addRow(QStringLiteral("目标显示器"), m_pComboScreen);
    m_pSpinPixelPitch = makeSpin(0.0, 5.0, 5, 0.0, pGrpDetail);
    m_pSpinPixelPitch->setSuffix(QStringLiteral(" mm/px"));
    m_pSpinPixelPitch->setSpecialValueText(QStringLiteral("使用系统读数"));
    m_pSpinPixelPitch->setToolTip(QStringLiteral("显示器像素点距; 系统读数常不准, 建议按规格书或用尺量 N 个格子换算后填写"));
    auto* pBtnSys = new QPushButton(QStringLiteral("填入系统读数"), pGrpDetail);
    auto* pPitchRow = new QHBoxLayout();
    pPitchRow->addWidget(m_pSpinPixelPitch, 1);
    pPitchRow->addWidget(pBtnSys);
    pDetailForm->addRow(QStringLiteral("实测像素点距"), pPitchRow);

    m_pSpinTargetMm = makeSpin(0.01, 1000, 3, 5.0, pGrpDetail);
    m_pSpinTargetMm->setSuffix(QStringLiteral(" mm"));
    auto* pBtnTarget = new QPushButton(QStringLiteral("换算像素格距"), pGrpDetail);
    auto* pTargetRow = new QHBoxLayout();
    pTargetRow->addWidget(m_pSpinTargetMm, 1);
    pTargetRow->addWidget(pBtnTarget);
    pDetailForm->addRow(QStringLiteral("按目标物理格距"), pTargetRow);
    pParamLayout->addWidget(pGrpDetail);

    auto* pGrpExport = new QGroupBox(QStringLiteral("文件导出与 15 姿态标定图集批量生成"), pTabParam);
    auto* pExportForm = new QFormLayout(pGrpExport);
    pExportForm->setSpacing(8);
    m_pSpinPrintMm = makeSpin(0.1, 1000, 3, 10.0, pGrpExport);
    m_pSpinPrintMm->setSuffix(QStringLiteral(" mm"));
    m_pSpinPrintMm->setToolTip(QStringLiteral("PDF 打印用的格距; 打印时选「实际大小 / 100%」, 打印后用卡尺复核"));
    pExportForm->addRow(QStringLiteral("PDF 矢量打印格距"), m_pSpinPrintMm);
    auto* pExportRow = new QHBoxLayout();
    auto* pBtnPng = new QPushButton(QStringLiteral("导出当前 PNG"), pGrpExport);
    auto* pBtnPdf = new QPushButton(QStringLiteral("导出矢量 PDF"), pGrpExport);
    auto* pBtnTxt = new QPushButton(QStringLiteral("导出参数 TXT"), pGrpExport);
    pExportRow->addWidget(pBtnPng);
    pExportRow->addWidget(pBtnPdf);
    pExportRow->addWidget(pBtnTxt);
    pExportForm->addRow(pExportRow);

    auto* pBtnBatchPoses = new QPushButton(QStringLiteral("📦 一键批量导出 15 姿态标定图集 (PNG)"), pGrpExport);
    pBtnBatchPoses->setProperty("primary", true);
    pBtnBatchPoses->setToolTip(QStringLiteral("自动渲染 15 张不同 Pitch/Yaw/Roll 3D 透视姿态的高清标定板 PNG 及说明文件, 直接拖入 Halcon/OpenCV 即可标定内参"));
    pExportForm->addRow(pBtnBatchPoses);
    pParamLayout->addWidget(pGrpExport);
    pParamLayout->addStretch();
    pCfgTabs->addTab(pTabParam, QStringLiteral("参数校准与批量导出"));

    // ---- Tab 2: 视觉算子调用代码与须知 ----
    auto* pTabDesc = new QWidget(pCfgTabs);
    auto* pDescLayout = new QVBoxLayout(pTabDesc);
    pDescLayout->setContentsMargins(8, 8, 8, 8);
    pDescLayout->setSpacing(8);
    pDescLayout->addWidget(new QLabel(QStringLiteral("OpenCV / Halcon 标定调用参数 (随格距与点距自动更新, 可直接复制):"), pTabDesc));
    m_pTextDesc = new QPlainTextEdit(pTabDesc);
    m_pTextDesc->setReadOnly(true);
    m_pTextDesc->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 12px;"));
    pDescLayout->addWidget(m_pTextDesc, 1);

    auto* pTip = new QLabel(QStringLiteral(
        "<b>拍屏与多姿态内参标定须知</b><br>"
        "1. <b>张正友标定法要求</b>: 单平面标定必须包含 10~15 个不同倾斜角度 (±20°~30° Pitch/Yaw), 仅拍正对平面无法解耦焦距 f 与物距 Z。<br>"
        "2. <b>真相机必须物理倾斜</b>: 屏上的「3D 姿态」只是把倾斜画在平的屏幕上, 相机看到的仍是正对平面, 标出的焦距/主点会混入虚拟相机参数, 不能用。"
        "真相机拍屏请<b>姿态归零</b>, 实际倾斜相机或显示器 (前后倾、左右倾各 ≥ 4 张, 整板入画)。"
        "「一键批量导出 15 姿态标定图集」生成的合成 PNG 只用于离线测试算法。<br>"
        "3. <b>点距复核</b>: 用卡尺量全屏显示的 N 个格子总长 L(mm), 实测点距 = L / (N × 格距px)。"), pTabDesc);
    pTip->setWordWrap(true);
    pTip->setStyleSheet(QStringLiteral("color: #475569; font-size: 12px;"));
    pDescLayout->addWidget(pTip);
    pCfgTabs->addTab(pTabDesc, QStringLiteral("OpenCV / Halcon 算子说明"));

    pRightLayout->addWidget(pCfgTabs, 1);

    pSplitter->setStretchFactor(0, 3);
    pSplitter->setStretchFactor(1, 2);
    pSplitter->setSizes({ 780, 480 });

    // ---------- 信号连接 ----------
    for (QSpinBox* pSpin : { m_pSpinCols, m_pSpinRows, m_pSpinPitchPx, m_pSpinMargin }) {
        connect(pSpin, qOverload<int>(&QSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    }
    connect(m_pSpinRatio, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pSpinCodeSpacing, qOverload<int>(&QSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pSpinPixelPitch, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pComboType, qOverload<int>(&QComboBox::currentIndexChanged), this, &CalibBoardPage::onParamsChanged);
    connect(m_pChkInvert, &QCheckBox::toggled, this, &CalibBoardPage::onParamsChanged);

    for (QDoubleSpinBox* pSpin3D : { m_pSpinPitchDeg, m_pSpinYawDeg, m_pSpinRollDeg }) {
        connect(pSpin3D, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &CalibBoardPage::onPoseChanged);
    }
    connect(m_pComboPosePreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int nIdx) {
        if (nIdx >= 0 && nIdx < kStandardPoseCount) {
            const QSignalBlocker b1(m_pSpinPitchDeg), b2(m_pSpinYawDeg), b3(m_pSpinRollDeg);
            m_pSpinPitchDeg->setValue(kStandardPoses[nIdx].dPitch);
            m_pSpinYawDeg->setValue(kStandardPoses[nIdx].dYaw);
            m_pSpinRollDeg->setValue(kStandardPoses[nIdx].dRoll);
            onPoseChanged();
        }
    });
    connect(pBtnResetPose, &QPushButton::clicked, this, [this] {
        m_pComboPosePreset->setCurrentIndex(0);
        const QSignalBlocker b1(m_pSpinPitchDeg), b2(m_pSpinYawDeg), b3(m_pSpinRollDeg);
        m_pSpinPitchDeg->setValue(0.0);
        m_pSpinYawDeg->setValue(0.0);
        m_pSpinRollDeg->setValue(0.0);
        onPoseChanged();
    });
    m_pPoseCarouselTimer = new QTimer(this);
    m_pPoseCarouselTimer->setInterval(1200);
    // 20261108 ZJH 轮播与手动按钮共用 stepPose, 行为一致
    connect(m_pPoseCarouselTimer, &QTimer::timeout, this, [this] { stepPose(1); });
    connect(pBtnNextPose, &QPushButton::clicked, this, [this] { stepPose(1); });
    connect(pBtnAutoPose, &QPushButton::toggled, this, [this, pBtnAutoPose](bool bOn) {
        pBtnAutoPose->setText(bOn ? QStringLiteral("⏹ 停止姿态轮播") : QStringLiteral("⚡ 15姿态自动轮播"));
        if (bOn) {
            m_pPoseCarouselTimer->start();
        } else {
            m_pPoseCarouselTimer->stop();
        }
    });

    connect(m_pComboScreen, qOverload<int>(&QComboBox::currentIndexChanged), this, &CalibBoardPage::onScreenChanged);
    connect(pBtnSys, &QPushButton::clicked, this, &CalibBoardPage::onUseSystemPitch);
    connect(pBtnTarget, &QPushButton::clicked, this, &CalibBoardPage::onApplyTargetMm);
    connect(pBtnFit, &QPushButton::clicked, this, &CalibBoardPage::onFitScreen);
    connect(pBtnFull, &QPushButton::clicked, this, &CalibBoardPage::onShowFullScreen);
    connect(pBtnPng, &QPushButton::clicked, this, &CalibBoardPage::onExportPng);
    connect(pBtnBatchPoses, &QPushButton::clicked, this, &CalibBoardPage::onExportBatchPoses);
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
    m_pLabelSysPitch->setText(QStringLiteral("系统点距参考: %1 mm/px  (EDID 物理尺寸 %2×%3 mm)")
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
    // 20261109 ZJH 算法移到 CalibBoard::fitToScreen (可单测); 勾选「自动补行列」时另一方向再加列/行
    const QSize sizeScreen = physicalScreenSize(selectedScreen());  // 目标屏物理像素
    if (sizeScreen.isEmpty()) {
        return;  // 读不到屏幕尺寸, 不改格距
    }
    const CalibBoardSpec fit = CalibBoard::fitToScreen(spec(), sizeScreen, m_pChkFitGrow->isChecked());
    {
        // 20261109 ZJH 三个值一起回填, 屏蔽中间刷新, 最后统一重绘一次
        const QSignalBlocker b1(m_pSpinPitchPx), b2(m_pSpinCols), b3(m_pSpinRows), b4(m_pSpinCodeSpacing);
        m_pSpinPitchPx->setValue(fit.nPitchPx);
        m_pSpinCols->setValue(fit.nCols);
        m_pSpinRows->setValue(fit.nRows);
        m_pSpinCodeSpacing->setValue(fit.nCodeSpacing);  // DM 码棋盘格可能被调小以保证码数
    }
    onParamsChanged();  // 刷新预览、全屏窗口与视觉软件参数
}

void CalibBoardPage::followScreen(QScreen* pScreen)
{
    // 20261109 ZJH 只处理仍在列表里的屏 (拔掉的屏由 refreshScreens 处理)
    const int nIndex = static_cast<int>(QGuiApplication::screens().indexOf(pScreen));
    if (nIndex < 0) {
        return;  // 未知屏, 不动
    }
    // 20261109 ZJH 目标显示器跟随窗口所在屏; 不同时 setCurrentIndex 会触发 onScreenChanged 刷新系统点距
    if (m_pComboScreen->currentIndex() != nIndex) {
        m_pComboScreen->setCurrentIndex(nIndex);
    }
    onFitScreen();  // 按新屏重新铺满
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

void CalibBoardPage::onPoseChanged()
{
    const double dPitch = m_pSpinPitchDeg->value();
    const double dYaw = m_pSpinYawDeg->value();
    const double dRoll = m_pSpinRollDeg->value();
    m_pPreview->setPose3D(dPitch, dYaw, dRoll);
    if (m_pFullView) {
        m_pFullView->setPose3D(dPitch, dYaw, dRoll);
    }
    onParamsChanged();  // 20261109 ZJH 刷新信息栏 (姿态不为 0 时的红字警告)
}

void CalibBoardPage::onParamsChanged()
{
    const CalibBoardSpec board = spec();
    const bool bCircle = board.eType != BoardType::Chessboard && board.eType != BoardType::DmChessboard;
    m_pSpinRatio->setEnabled(bCircle);  // 直径比只对圆点有效
    m_pSpinCodeSpacing->setEnabled(board.eType == BoardType::DmChessboard);  // 码间距只对 DM 码棋盘格有效
    const double dPitchMm = effectivePixelPitchMm();
    const QImage img = CalibBoard::render(board, dPitchMm);
    m_pPreview->setBoard(img, board);
    if (m_pFullView) {
        m_pFullView->setBoard(img, board);
    }

    // 20261103 ZJH 信息栏: 像素尺寸 / 物理尺寸 / 是否放得下目标屏
    const QSize sizePx = CalibBoard::boardSizePx(board);
    QScreen* pScreen = selectedScreen();
    const QSize sizeScreen = physicalScreenSize(pScreen);
    const bool bFits = sizePx.width() <= sizeScreen.width() && sizePx.height() <= sizeScreen.height();
    // 20261109 ZJH 第一行醒目给出视觉软件要填的格距 (现场把 Halcon MarkDist 填成 0.001 m = 1 mm, 比例差十几倍)
    const double dPitchPhysMm = board.nPitchPx * dPitchMm;  // 屏上实际格距 mm
    QString strVision = QStringLiteral("<span style='font-size:14px; color:#1D4ED8;'><b>视觉软件格距 = %1 mm</b>  (%2 × %3)")
                            .arg(dPitchPhysMm, 0, 'f', 4).arg(board.nCols).arg(board.nRows);
    if (board.eType == BoardType::HalconCircle) {
        // 20261109 ZJH Halcon gen_caltab 的 MarkDist 单位是 m
        strVision += QStringLiteral("  →  gen_caltab(%1, %2, <b>%3</b>, %4)")
                         .arg(board.nCols).arg(board.nRows).arg(dPitchPhysMm / 1000.0, 0, 'g', 8).arg(board.dCircleRatio);
    }
    strVision += QStringLiteral("</span><br>");
    QString strInfo = strVision + QStringLiteral("整板: %1×%2 px (%3×%4 mm)  |  格距: %5 px = %6 mm  |  点距: %7 mm/px%8")
                          .arg(sizePx.width()).arg(sizePx.height())
                          .arg(sizePx.width() * dPitchMm, 0, 'f', 2).arg(sizePx.height() * dPitchMm, 0, 'f', 2)
                          .arg(board.nPitchPx).arg(board.nPitchPx * dPitchMm, 0, 'f', 4).arg(dPitchMm, 0, 'f', 5)
                          .arg(m_pSpinPixelPitch->value() > 0.0 ? QString() : QStringLiteral(" (系统)"));
    if (!bFits) {
        strInfo += QStringLiteral("<br><font color='#DC2626'>⚠ 超出目标屏 %1×%2 物理像素, 全屏时会被裁切; 请点击「铺满目标屏」或减小格距/行列数</font>")
                       .arg(sizeScreen.width()).arg(sizeScreen.height());
    }
    // 20261109 ZJH DM 码太少: 视觉至少 2 个码才定得出姿态 (现场 14×7 + 间距 10 只有 1 个码)
    if (board.eType == BoardType::DmChessboard) {
        const int nCodes = static_cast<int>(CalibBoard::dmCodeCorners(board).size());  // 板上码数
        if (nCodes < CalibBoard::s_nMinDmCodes) {
            strInfo += QStringLiteral("<br><font color='#DC2626'>⚠ 板上只有 %1 个 DM 码 (建议 ≥ %2), 视觉至少要 2 个才定得出姿态; 请减小「DM 码间距」或点「铺满目标屏」自动调整</font>")
                           .arg(nCodes).arg(CalibBoard::s_nMinDmCodes);
        }
    }
    // 20261109 ZJH 姿态不为 0: 屏上模拟倾斜不能给真相机标内参
    if (std::abs(m_pSpinPitchDeg->value()) > 1e-4 || std::abs(m_pSpinYawDeg->value()) > 1e-4
        || std::abs(m_pSpinRollDeg->value()) > 1e-4) {
        strInfo += QStringLiteral("<br><font color='#DC2626'>⚠ 3D 姿态不为 0: 屏上模拟倾斜只用于导出合成图, 不能用于真实相机内参标定; 拍屏请「姿态归零」并物理倾斜相机或显示器</font>");
    }
    // 20261109 ZJH 手填点距与本屏系统读数差 > 20%: 多半是别的屏的实测值 (换屏后未改), 提示复核
    const double dSysPitch = systemPixelPitchMm();
    if (m_pSpinPixelPitch->value() > 0.0 && std::abs(m_pSpinPixelPitch->value() / dSysPitch - 1.0) > 0.2) {
        strInfo += QStringLiteral("<br><font color='#D97706'>⚠ 手填点距 %1 mm/px 与本屏系统读数 %2 相差 %3%, 可能是其他显示器的值; 请复核或点「填入系统读数」</font>")
                       .arg(m_pSpinPixelPitch->value(), 0, 'f', 5).arg(dSysPitch, 0, 'f', 5)
                       .arg(std::lround(std::abs(m_pSpinPixelPitch->value() / dSysPitch - 1.0) * 100));
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
    onFitScreen();  // 20261109 ZJH 全屏前先按目标屏铺满, 不用再手动点「铺满目标屏」
    const CalibBoardSpec board = spec();
    auto* pView = new CalibBoardView(true, nullptr);
    pView->setBoard(CalibBoard::render(board, effectivePixelPitchMm()), board);
    pView->setPose3D(m_pSpinPitchDeg->value(), m_pSpinYawDeg->value(), m_pSpinRollDeg->value());
    m_pFullView = pView;
    auto* pWin = new ViewWindow(pView, QStringLiteral("标定板 %1").arg(CalibBoard::typeName(board.eType)), nullptr);
    // 20261103 ZJH 先把窗口放到目标屏再全屏, 否则会在主屏全屏
    pWin->setGeometry(pScreen->geometry().adjusted(50, 50, -50, -50));
    pWin->winId();  // 创建原生窗口句柄
    if (pWin->windowHandle()) {
        pWin->windowHandle()->setScreen(pScreen);
    }
    // 20261108 ZJH 全屏窗口内翻姿态: → / 空格 下一张, ← 上一张 (窗口级快捷键, 只在该窗口激活时生效)
    const QList<QPair<QKeySequence, int>> lstPoseKeys = { { QKeySequence(Qt::Key_Right), 1 },
                                                          { QKeySequence(Qt::Key_Space), 1 },
                                                          { QKeySequence(Qt::Key_Left), -1 } };
    for (const auto& pairKey : lstPoseKeys) {
        auto* pShortcut = new QShortcut(pairKey.first, pWin);  // 随窗口销毁
        const int nDelta = pairKey.second;  // 前进 (+1) 或后退 (-1)
        connect(pShortcut, &QShortcut::activated, this, [this, nDelta] { stepPose(nDelta); });
    }
    pWin->showFullScreen();
    pWin->raise();
    pWin->activateWindow();
    // 20261109 ZJH 显示后再监听换屏 (拖动 / Win+Shift+方向键), 避免初次 setScreen 触发; 窗口销毁时连接自动断开
    if (QWindow* pHandle = pWin->windowHandle()) {
        connect(pHandle, &QWindow::screenChanged, this, &CalibBoardPage::followScreen);
    }
}

void CalibBoardPage::stepPose(int nDelta)
{
    // 20261108 ZJH 循环取下一个预设索引; 加 kStandardPoseCount 防止后退时出现负数
    const int nCur = std::max(0, m_pComboPosePreset->currentIndex());
    const int nNext = ((nCur + nDelta) % kStandardPoseCount + kStandardPoseCount) % kStandardPoseCount;
    m_pComboPosePreset->setCurrentIndex(nNext);  // 触发 currentIndexChanged → 写入三个角度并刷新预览/全屏
}

QImage CalibBoardPage::renderBoardWithPose(const CalibBoardSpec& board, double dPitchMm,
                                           double dPitchDeg, double dYawDeg, double dRollDeg) const
{
    const QImage imgBase = CalibBoard::render(board, dPitchMm);
    if (std::abs(dPitchDeg) <= 1e-4 && std::abs(dYawDeg) <= 1e-4 && std::abs(dRollDeg) <= 1e-4) {
        return imgBase;
    }
    // 20261106 ZJH 带 3D 姿态导出时四周留出 20% 静区画布, 防止旋转或透视四角被裁切
    const int nCanvasW = static_cast<int>(std::ceil(imgBase.width() * 1.24));
    const int nCanvasH = static_cast<int>(std::ceil(imgBase.height() * 1.24));
    QImage imgOut(nCanvasW, nCanvasH, QImage::Format_Grayscale8);
    imgOut.setDotsPerMeterX(imgBase.dotsPerMeterX());
    imgOut.setDotsPerMeterY(imgBase.dotsPerMeterY());
    imgOut.fill(board.bInvert ? 0 : 255);
    QPainter painter(&imgOut);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setWorldTransform(makePerspectiveTransform(nCanvasW * 0.5, nCanvasH * 0.5, dPitchDeg, dYawDeg, dRollDeg), true);
    painter.drawImage(QPointF((nCanvasW - imgBase.width()) * 0.5, (nCanvasH - imgBase.height()) * 0.5), imgBase);
    painter.end();
    return imgOut;
}

void CalibBoardPage::onExportPng()
{
    const CalibBoardSpec board = spec();
    const QString strPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 PNG"),
        QStringLiteral("标定板_%1_%2x%3_%4px.png").arg(CalibBoard::typeName(board.eType)).arg(board.nCols).arg(board.nRows).arg(board.nPitchPx),
        QStringLiteral("PNG (*.png)"));
    if (strPath.isEmpty()) {
        return;
    }
    const QImage img = renderBoardWithPose(board, effectivePixelPitchMm(),
                                           m_pSpinPitchDeg->value(), m_pSpinYawDeg->value(), m_pSpinRollDeg->value());
    if (!img.save(strPath, "PNG")) {
        QMessageBox::warning(this, QStringLiteral("导出 PNG"), QStringLiteral("保存失败: %1").arg(strPath));
    }
}

void CalibBoardPage::onExportBatchPoses()
{
    const QString strDefaultDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs/内参标定图集_15姿态");
    QDir().mkpath(strDefaultDir);
    const QString strDir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 15 姿态标定图集保存目录"), strDefaultDir);
    if (strDir.isEmpty()) {
        return;
    }
    const CalibBoardSpec board = spec();
    const double dPitchMm = effectivePixelPitchMm();
    int nSaved = 0;
    for (int i = 0; i < kStandardPoseCount; ++i) {
        const Pose3DPreset& p = kStandardPoses[i];
        const QImage img = renderBoardWithPose(board, dPitchMm, p.dPitch, p.dYaw, p.dRoll);
        const QString strFile = QStringLiteral("%1/pose_%2_P%3_Y%4_R%5.png")
            .arg(strDir)
            .arg(i + 1, 2, 10, QChar('0'))
            .arg(static_cast<int>(p.dPitch))
            .arg(static_cast<int>(p.dYaw))
            .arg(static_cast<int>(p.dRoll));
        if (img.save(strFile, "PNG")) {
            ++nSaved;
        }
    }
    // 同时保存一份标定参数说明 TXT 到该目录
    QFile file(strDir + QStringLiteral("/标定板参数说明.txt"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString strText = QStringLiteral("===== 15 姿态内参标定图集参数 (点距 %1 mm/px) =====\n").arg(dPitchMm, 0, 'f', 5);
        strText += CalibBoard::describe(board, board.nPitchPx * dPitchMm);
        file.write("\xEF\xBB\xBF");
        file.write(strText.toUtf8());
    }
    m_pLabelInfo->setText(m_pLabelInfo->text() + QStringLiteral("<br><font color='#16A34A'>✔ 已成功导出 %1 张多姿态标定图至: %2</font>")
                                                     .arg(nSaved).arg(strDir));
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
