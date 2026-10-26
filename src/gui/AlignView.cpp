// 20261024 ZJH AlignView / ViewWindow 实现
#include "gui/AlignView.h"

#include "motion/Stage.h"
#include "sim/AlignScene.h"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cstdint>

namespace {
// 20261026 ZJH 针对黑白工业相机高对比度重新规划配色:
//   - 相机视野底色: 纯白 #FFFFFF (灰度 255)
//   - 目标 Mark / 标定 Mark / 纯净相机对象 Mark: 纯黑 #000000 (灰度 0, 对比度 255:0)
//   - 全局视图同时显示目标与对象时, 对象 Mark 用碳黑深藏青 #08182E (灰度 18, 黑白相机下仍为近纯黑高对比, 人眼可微辨)
const QColor s_colorTarget(0, 0, 0);            // 目标 Mark: 纯黑 (灰度 0)
const QColor s_colorObject(0, 0, 0);            // 对象 Mark (单显/相机拍屏): 纯黑 (灰度 0)
const QColor s_colorObjectDual(8, 24, 46);      // 对象 Mark (全局双显区分): 碳黑藏青 (灰度 18)
const QColor s_colorCalib(0, 0, 0);             // 标定 Mark: 纯黑 (灰度 0, 避免彩色在黑白相机下灰度偏浅抓空)
const QColor s_colorBackground(255, 255, 255);  // 相机视野底色: 纯白 (灰度 255)
const QColor s_colorLetterbox(30, 34, 42);      // 视野外留边: 深石板灰
const QColor s_colorGlobalBg(226, 232, 240);    // 全局视图视野外画布底色: 柔和浅灰蓝
}

// ============================================================
// WorldViewMapper
// ============================================================
void WorldViewMapper::setup(const QRectF& rcWorld, const QRectF& rcScreen, bool bYDown)
{
    m_rcWorld = rcWorld;
    m_bYDown = bYDown;
    // 20261024 ZJH 等比缩放: 取宽高两个方向缩放比的较小值, 世界区域整体可见
    const double dSx = rcWorld.width() > 0 ? rcScreen.width() / rcWorld.width() : 1.0;
    const double dSy = rcWorld.height() > 0 ? rcScreen.height() / rcWorld.height() : 1.0;
    m_dScale = std::max(1e-6, std::min(dSx, dSy));
    // 20261024 ZJH 居中偏移
    m_dOffX = rcScreen.left() + (rcScreen.width() - rcWorld.width() * m_dScale) / 2.0;
    m_dOffY = rcScreen.top() + (rcScreen.height() - rcWorld.height() * m_dScale) / 2.0;
}

void WorldViewMapper::setupFixed(const QPointF& ptWorldCenter, double dPxPerMm, const QRectF& rcScreen, bool bYDown)
{
    m_bYDown = bYDown;
    m_dScale = std::max(1e-6, dPxPerMm);
    // 20261025 ZJH 整个控件就是可见世界区域: 宽高 = 像素 / 比例, 以 ptWorldCenter 为中心
    const double dWorldW = rcScreen.width() / m_dScale;
    const double dWorldH = rcScreen.height() / m_dScale;
    m_rcWorld = QRectF(ptWorldCenter.x() - dWorldW / 2, ptWorldCenter.y() - dWorldH / 2, dWorldW, dWorldH);
    m_dOffX = rcScreen.left();
    m_dOffY = rcScreen.top();
}

QPointF WorldViewMapper::toScreen(const QPointF& ptWorld) const
{
    const double dSx = m_dOffX + (ptWorld.x() - m_rcWorld.left()) * m_dScale;
    // 20261024 ZJH Y 朝上时以世界区域上边 (最大 Y) 为屏幕顶部
    const double dSy = m_bYDown ? m_dOffY + (ptWorld.y() - m_rcWorld.top()) * m_dScale
                                : m_dOffY + (m_rcWorld.bottom() - ptWorld.y()) * m_dScale;
    return QPointF(dSx, dSy);  // 返回屏幕坐标
}

QPointF WorldViewMapper::toWorld(const QPointF& ptScreen) const
{
    const double dWx = m_rcWorld.left() + (ptScreen.x() - m_dOffX) / m_dScale;
    const double dWy = m_bYDown ? m_rcWorld.top() + (ptScreen.y() - m_dOffY) / m_dScale
                                : m_rcWorld.bottom() - (ptScreen.y() - m_dOffY) / m_dScale;
    return QPointF(dWx, dWy);
}

QRectF WorldViewMapper::worldRectOnScreen() const
{
    return QRectF(m_dOffX, m_dOffY, m_rcWorld.width() * m_dScale, m_rcWorld.height() * m_dScale);
}

void WorldViewMapper::applyLocalFrame(QPainter& painter, const QPointF& ptWorld, double dAngleDeg) const
{
    painter.translate(toScreen(ptWorld));
    // 20261024 ZJH 先缩放 (Y 朝上时翻转 Y), 再旋转: 局部坐标与世界坐标手性一致, 正角度始终为世界逆时针
    painter.scale(m_dScale, m_bYDown ? m_dScale : -m_dScale);
    painter.rotate(dAngleDeg);
}

// ============================================================
// Mark 绘制
// ============================================================
void drawMarkShape(QPainter& painter, int nShape, double dSize, double dLineWidth, const QColor& color, bool bDefect)
{
    QPen pen(color, dLineWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);  // 线宽单位 mm (已缩放)
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const double dHalf = dSize / 2.0;
    switch (static_cast<MarkShape>(nShape)) {
    case MarkShape::Circle:
        // 20261024 ZJH 实心圆
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(0, 0), dHalf, dHalf);
        break;
    case MarkShape::LCorner: {
        // 20261024 ZJH L 角: 角点在原点, 两臂沿 +X 和 +Y, 臂宽 = 2 倍线宽
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const double dArm = dLineWidth * 2.0;
        painter.drawRect(QRectF(0, 0, dSize, dArm));
        painter.drawRect(QRectF(0, 0, dArm, dSize));
        break;
    }
    case MarkShape::Rect:
        // 20261024 ZJH 实心正方形
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRect(QRectF(-dHalf, -dHalf, dSize, dSize));
        break;
    case MarkShape::CrossCircle:
        // 20261024 ZJH 圆环 + 内十字
        painter.drawEllipse(QPointF(0, 0), dHalf, dHalf);
        painter.drawLine(QPointF(-dHalf * 0.7, 0), QPointF(dHalf * 0.7, 0));
        painter.drawLine(QPointF(0, -dHalf * 0.7), QPointF(0, dHalf * 0.7));
        break;
    case MarkShape::Cross:
    default:
        // 20261024 ZJH 十字
        painter.drawLine(QPointF(-dHalf, 0), QPointF(dHalf, 0));
        painter.drawLine(QPointF(0, -dHalf), QPointF(0, dHalf));
        break;
    }
    // 20261026 ZJH 模拟边缘缺口与旁侧小污点瑕疵, 供测试视觉边缘抗干扰能力
    if (bDefect) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(QPointF(dSize * 0.22, 0.0), dLineWidth * 0.55, dLineWidth * 0.55);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(-dSize * 0.28, dSize * 0.26), dLineWidth * 0.45, dLineWidth * 0.45);
    }
}

void drawOpticalNoiseOverlay(QPainter& painter, const QRectF& rcArea, int nNoiseLevel)
{
    if (nNoiseLevel <= 0 || rcArea.isEmpty()) {
        return;
    }
    const int nClamped = std::clamp(nNoiseLevel, 1, 100);
    painter.save();
    // 1. 模拟镜头渐晕/斜射光照不均
    QLinearGradient grad(rcArea.topLeft(), rcArea.bottomRight());
    grad.setColorAt(0.0, QColor(0, 0, 0, nClamped));
    grad.setColorAt(0.5, QColor(0, 0, 0, 0));
    grad.setColorAt(1.0, QColor(0, 0, 0, nClamped * 3 / 2));
    painter.fillRect(rcArea, grad);

    // 2. 模拟传感器散斑噪点 (固定伪随机序列, 保证不闪烁)
    const int nDots = std::min(1200, static_cast<int>(rcArea.width() * rcArea.height() * 0.002 * (nClamped / 15.0 + 0.5)));
    uint32_t uState = 0x9E3779B9u;
    auto nextRand = [&uState]() {
        uState ^= uState << 13;
        uState ^= uState >> 17;
        uState ^= uState << 5;
        return static_cast<double>(uState & 0xFFFFu) / 65535.0;
    };
    painter.setPen(QPen(QColor(40, 40, 40, std::min(220, nClamped * 2 + 20)), 1.3));
    for (int i = 0; i < nDots; ++i) {
        const double dX = rcArea.left() + nextRand() * rcArea.width();
        const double dY = rcArea.top() + nextRand() * rcArea.height();
        painter.drawPoint(QPointF(dX, dY));
    }
    painter.restore();
}

// ============================================================
// AlignView
// ============================================================
AlignView::AlignView(AlignScene* pScene, int nCameraIndex, QWidget* pParent)
    : QWidget(pParent)
    , m_pScene(pScene)
    , m_nCameraIndex(nCameraIndex)
{
    setMinimumSize(160, 120);
    // 20261024 ZJH 场景或平台变化时重绘 (信号可能来自脚本线程, 用排队连接回到界面线程)
    connect(m_pScene, &AlignScene::changed, this, qOverload<>(&QWidget::update), Qt::QueuedConnection);
    connect(m_pScene->stage(), &Stage::changed, this, qOverload<>(&QWidget::update), Qt::QueuedConnection);
}

void AlignView::setCameraIndex(int nIndex)
{
    m_nCameraIndex = nIndex;
    update();
}

QRectF AlignView::worldRect() const
{
    const std::vector<AlignCamera> vecCams = m_pScene->cameras();
    // 20261024 ZJH 相机模式: 正好是该相机视野
    if (m_nCameraIndex >= 0 && m_nCameraIndex < static_cast<int>(vecCams.size())) {
        const AlignCamera& cam = vecCams[m_nCameraIndex];
        return QRectF(cam.dCenterX - cam.dFovW / 2, cam.dCenterY - cam.dFovH / 2, cam.dFovW, cam.dFovH);
    }
    // 20261024 ZJH 全局模式: 所有相机视野 + 所有 Mark 的外包框, 四周留 10%
    QRectF rcAll;
    for (const AlignCamera& cam : vecCams) {
        rcAll = rcAll.united(QRectF(cam.dCenterX - cam.dFovW / 2, cam.dCenterY - cam.dFovH / 2, cam.dFovW, cam.dFovH));
    }
    for (const MarkPair& mark : m_pScene->marks()) {
        rcAll = rcAll.united(QRectF(mark.dTargetX - 1, mark.dTargetY - 1, 2, 2));
    }
    rcAll = rcAll.united(QRectF(m_pScene->stage()->rotCenter() - QPointF(1, 1), QSizeF(2, 2)));  // 包含旋转中心
    // 20261024 ZJH 没有任何内容时给一个默认 100x75 区域
    if (rcAll.isEmpty()) {
        rcAll = QRectF(-50, -37.5, 100, 75);
    }
    const double dRatio = m_pScene->display().bShowStage ? 0.24 : 0.10;
    const double dMargin = std::max(rcAll.width(), rcAll.height()) * dRatio;
    return rcAll.adjusted(-dMargin, -dMargin, dMargin, dMargin);  // 返回带边距区域
}

void AlignView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const std::vector<AlignCamera> vecCams = m_pScene->cameras();
    const AlignDisplay display = m_pScene->display();
    // 20261025 ZJH 相机模式: 下标必须有效 (相机被删后退化为全局视图)
    const bool bCameraMode = m_nCameraIndex >= 0 && m_nCameraIndex < static_cast<int>(vecCams.size());
    const bool bFixed = display.bFixedScale;  // 20261026 ZJH 单画面模式下全局视图与相机视图均支持固定比例
    WorldViewMapper mapper;
    if (bFixed) {
        const QPointF ptCenter = bCameraMode
            ? QPointF(vecCams[m_nCameraIndex].dCenterX, vecCams[m_nCameraIndex].dCenterY)
            : worldRect().center();
        mapper.setupFixed(ptCenter, display.dPxPerMm, rect(), display.bYDown);
    } else {
        QRectF rcW = worldRect();
        if (m_dZoom != 1.0 || !m_ptPanWorld.isNull()) {
            const QPointF ptCenter = rcW.center() + m_ptPanWorld;
            const double dW = rcW.width() / m_dZoom;
            const double dH = rcW.height() / m_dZoom;
            rcW = QRectF(ptCenter.x() - dW / 2.0, ptCenter.y() - dH / 2.0, dW, dH);
        }
        mapper.setup(rcW, rect(), display.bYDown);
    }

    // 20261026 ZJH 背景: 相机模式视野外深石板灰 + 视野内纯白 #FFFFFF; 全局模式视野外柔和浅灰蓝
    painter.fillRect(rect(), bCameraMode ? s_colorLetterbox : s_colorGlobalBg);
    if (bCameraMode) {
        painter.fillRect(mapper.worldRectOnScreen(), s_colorBackground);
        painter.setClipRect(mapper.worldRectOnScreen());  // Mark 超出视野部分不画
    }

    const std::vector<MarkPair> vecMarks = m_pScene->marks();
    const MarkStyle style = m_pScene->style();

    // 20261026 ZJH 全局模式: 先在底层绘制对位平台台面外框 (在相机视野纯白背景之前绘制, 绝不污染相机视野内部纯白底色!)
    Stage* pStage = m_pScene->stage();
    const StageGeometry geo = pStage->geometry();
    const Pose poseAct = pStage->pose();
    const AxisValues axesAct = pStage->actualAxes();
    const QPointF ptRcHome = pStage->actualRotCenter();
    const QPointF ptRcWorld = pStage->mapFromHome(ptRcHome);
    const double dStageAngle = pStage->mapAngleFromHome(0.0);
    double dHalfW = 28.0;
    double dHalfH = 20.0;
    for (const AlignCamera& cam : vecCams) {
        dHalfW = std::max(dHalfW, std::abs(cam.dCenterX - ptRcHome.x()) + cam.dFovW * 0.58);
        dHalfH = std::max(dHalfH, std::abs(cam.dCenterY - ptRcHome.y()) + cam.dFovH * 0.58);
    }
    for (const MarkPair& mark : vecMarks) {
        dHalfW = std::max(dHalfW, std::abs(mark.dObjectX - ptRcHome.x()) + 12.0);
        dHalfH = std::max(dHalfH, std::abs(mark.dObjectY - ptRcHome.y()) + 10.0);
    }

    if (!bCameraMode && display.bShowStage) {
        painter.save();
        mapper.applyLocalFrame(painter, ptRcWorld, dStageAngle);
        const double dPxMm = 1.0 / std::max(1e-6, mapper.scale());
        const QRectF rcTable(-dHalfW, -dHalfH, dHalfW * 2.0, dHalfH * 2.0);
        painter.setPen(QPen(QColor(37, 99, 235, 210), 2.0 * dPxMm, Qt::SolidLine));
        painter.setBrush(QColor(219, 234, 254, 140));
        painter.drawRoundedRect(rcTable, 6.0 * dPxMm, 6.0 * dPxMm);
        painter.setPen(QPen(QColor(59, 130, 246, 130), 1.0 * dPxMm, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rcTable.adjusted(4.0 * dPxMm, 4.0 * dPxMm, -4.0 * dPxMm, -4.0 * dPxMm), 4.0 * dPxMm, 4.0 * dPxMm);
        // 台面四角安装定位孔
        painter.setPen(QPen(QColor(37, 99, 235, 190), 1.4 * dPxMm));
        painter.setBrush(Qt::white);
        const double dHoleOff = 9.0 * dPxMm;
        const double dHoleR = 3.0 * dPxMm;
        for (const QPointF& ptCorner : { QPointF(-dHalfW + dHoleOff, -dHalfH + dHoleOff),
                                         QPointF(dHalfW - dHoleOff, -dHalfH + dHoleOff),
                                         QPointF(dHalfW - dHoleOff, dHalfH - dHoleOff),
                                         QPointF(-dHalfW + dHoleOff, dHalfH - dHoleOff) }) {
            painter.drawEllipse(ptCorner, dHoleR, dHoleR);
        }
        painter.restore();
    }

    // 20261026 ZJH 全局模式: 画每个相机的纯白视野框 (100% #FFFFFF 覆盖底层台面底色), 并将相机名称画在视野框【上方外部】避免干扰黑白相机抓取
    if (!bCameraMode) {
        for (const AlignCamera& cam : vecCams) {
            const QPointF ptA = mapper.toScreen(QPointF(cam.dCenterX - cam.dFovW / 2, cam.dCenterY - cam.dFovH / 2));
            const QPointF ptB = mapper.toScreen(QPointF(cam.dCenterX + cam.dFovW / 2, cam.dCenterY + cam.dFovH / 2));
            const QRectF rcCam = QRectF(ptA, ptB).normalized();
            painter.fillRect(rcCam, s_colorBackground);
            painter.setPen(QPen(QColor(100, 116, 139), 1.2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rcCam);
            if (display.bShowStage) {
                painter.setPen(QColor(30, 41, 59));
                painter.drawText(rcCam.topLeft() + QPointF(2, -5), cam.strName);
            }
        }
    }

    // 20261024 ZJH 画目标 Mark (固定, 纯黑 #000000, 可单独隐藏)
    if (display.bShowTarget) {
        for (const MarkPair& mark : vecMarks) {
            const double dSz = mark.dSize > 0.0 ? mark.dSize : style.dSize;
            const double dLw = mark.dLineWidth > 0.0 ? mark.dLineWidth : style.dLineWidth;
            painter.save();
            mapper.applyLocalFrame(painter, QPointF(mark.dTargetX, mark.dTargetY), mark.dTargetAngle);
            if (display.nBlurLevel > 0) {
                QColor cHalo = s_colorTarget;
                cHalo.setAlpha(70);
                drawMarkShape(painter, static_cast<int>(style.eTargetShape),
                              dSz + display.nBlurLevel * 0.12, dLw + display.nBlurLevel * 0.10, cHalo, false);
            }
            drawMarkShape(painter, static_cast<int>(style.eTargetShape), dSz, dLw, s_colorTarget, display.bSimulateDefect);
            painter.restore();
        }
    }
    // 20261026 ZJH 画对象 Mark (随平台运动: 相机模式或单显时用纯黑 #000000, 全局双显时用灰度18的碳黑藏青, 确保黑白相机 93%~100% 满对比度)
    if (display.bShowObject) {
        const QColor cObj = (!bCameraMode && display.bShowTarget) ? s_colorObjectDual : s_colorObject;
        for (int i = 0; i < static_cast<int>(vecMarks.size()); ++i) {
            const MarkPair& mark = vecMarks[i];
            const double dSz = mark.dSize > 0.0 ? mark.dSize : style.dSize;
            const double dLw = mark.dLineWidth > 0.0 ? mark.dLineWidth : style.dLineWidth;
            const MarkWorldPose obj = m_pScene->objectMarkWorld(i);
            painter.save();
            mapper.applyLocalFrame(painter, obj.ptPos, obj.dAngleDeg);
            if (display.nBlurLevel > 0) {
                QColor cHalo = cObj;
                cHalo.setAlpha(70);
                drawMarkShape(painter, static_cast<int>(style.eObjectShape),
                              dSz + display.nBlurLevel * 0.12, dLw + display.nBlurLevel * 0.10, cHalo, false);
            }
            drawMarkShape(painter, static_cast<int>(style.eObjectShape), dSz, dLw, cObj, display.bSimulateDefect);
            painter.restore();
        }
    }
    // 20261026 ZJH 画对位平台专用标定 Mark (纯黑 #000000, 不在视野内画文字, 防止黑白相机将文字误当特征或因浅色抓空)
    if (display.bShowCalibMark) {
        for (int i = 0; i < static_cast<int>(vecCams.size()); ++i) {
            const MarkWorldPose calibPose = m_pScene->calibMarkWorld(i);
            painter.save();
            mapper.applyLocalFrame(painter, calibPose.ptPos, calibPose.dAngleDeg);
            if (display.nBlurLevel > 0) {
                QColor cHalo = s_colorCalib;
                cHalo.setAlpha(70);
                drawMarkShape(painter, display.nCalibShape,
                              display.dCalibSize + display.nBlurLevel * 0.12,
                              display.dCalibLineWidth + display.nBlurLevel * 0.10, cHalo, false);
            }
            drawMarkShape(painter, display.nCalibShape, display.dCalibSize, display.dCalibLineWidth,
                          s_colorCalib, display.bSimulateDefect);
            painter.restore();
        }
    }
    if (display.nNoiseLevel > 0) {
        drawOpticalNoiseOverlay(painter, bCameraMode ? mapper.worldRectOnScreen() : QRectF(rect()), display.nNoiseLevel);
    }

    // 20261026 ZJH 全局模式: 在相机视野外部绘制平台状态铭牌、UVW 驱动支点与旋转中心 (可由 display.bShowStage 一键隐藏)
    if (!bCameraMode) {
        if (display.bShowStage) {
            // 台面左上角状态铭牌 (位于台面外侧上方)
            const QPointF ptTableTopLeft = mapper.toScreen(pStage->mapFromHome(ptRcHome + QPointF(-dHalfW, display.bYDown ? -dHalfH : dHalfH)));
            painter.setPen(QColor(30, 64, 175));
            painter.drawText(ptTableTopLeft + QPointF(4, -8),
                             QStringLiteral("对位平台 [%1]  X=%2  Y=%3  θ=%4°")
                                 .arg(geo.eType == StageType::UVW ? QStringLiteral("UVW") : QStringLiteral("XYD"))
                                 .arg(poseAct.dX, 0, 'f', 3).arg(poseAct.dY, 0, 'f', 3).arg(poseAct.dThetaDeg, 0, 'f', 3));

            // 若为 UVW 平台, 在台面外边缘绘制 U / V / W 三个驱动滑块与方向箭头
            if (geo.eType == StageType::UVW) {
                const QStringList lstUvwNames = { QStringLiteral("U"), QStringLiteral("V"), QStringLiteral("W") };
                const double arrAxisVal[3] = { axesAct.dA1, axesAct.dA2, axesAct.dA3 };
                for (int i = 0; i < 3; ++i) {
                    const double dDx = geo.arrUvw[i].dMountX - geo.dRotCenterX;
                    const double dDy = geo.arrUvw[i].dMountY - geo.dRotCenterY;
                    const double dAng = std::atan2(dDy, dDx);
                    const QPointF ptVisHome = ptRcHome + QPointF(std::cos(dAng) * dHalfW, std::sin(dAng) * dHalfH);
                    const QPointF ptPinScreen = mapper.toScreen(pStage->mapFromHome(ptVisHome));
                    const double dDirRad = geo.arrUvw[i].dDirDeg * 3.14159265358979323846 / 180.0;
                    const QPointF ptDirScreen(std::cos(dDirRad), display.bYDown ? std::sin(dDirRad) : -std::sin(dDirRad));

                    painter.save();
                    painter.setPen(QPen(QColor(217, 119, 6), 2.0));
                    painter.setBrush(QColor(254, 243, 199));
                    painter.drawRoundedRect(QRectF(ptPinScreen - QPointF(7, 7), QSizeF(14, 14)), 3, 3);
                    painter.drawLine(ptPinScreen - ptDirScreen * 16.0, ptPinScreen + ptDirScreen * 16.0);
                    painter.setPen(QColor(180, 83, 9));
                    painter.drawText(ptPinScreen + QPointF(10, 4),
                                     QStringLiteral("%1=%2mm (%3°)")
                                         .arg(lstUvwNames[i])
                                         .arg(arrAxisVal[i], 0, 'f', 3)
                                         .arg(geo.arrUvw[i].dDirDeg, 0, 'f', 0));
                    painter.restore();
                }
            }

            // 绘制实际旋转中心 (位于相机视野之间的中心区)
            const QPointF ptRc = mapper.toScreen(ptRcWorld);
            painter.setPen(QPen(QColor(220, 38, 38), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(ptRc, 5, 5);
            painter.drawLine(ptRc - QPointF(9, 0), ptRc + QPointF(9, 0));
            painter.drawLine(ptRc - QPointF(0, 9), ptRc + QPointF(0, 9));
            painter.drawText(ptRc + QPointF(7, 13), QStringLiteral("旋转中心"));
        }
        painter.setPen(QColor(71, 85, 105));
        painter.drawText(rect().adjusted(8, 4, -8, -6), Qt::AlignRight | Qt::AlignBottom,
                         QStringLiteral("黑白相机高对比模式 (白底#FFFFFF / 黑标#000000)  |  比例 %1 px/mm").arg(mapper.scale(), 0, 'f', 2));
    }
}

void AlignView::wheelEvent(QWheelEvent* pEvent)
{
    const bool bCameraMode = m_nCameraIndex >= 0 && m_nCameraIndex < static_cast<int>(m_pScene->cameras().size());
    if (bCameraMode && m_pScene->display().bFixedScale) {
        pEvent->ignore();
        return;
    }
    const double dFactor = pEvent->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    m_dZoom = std::clamp(m_dZoom * dFactor, 0.25, 25.0);
    update();
}

void AlignView::mousePressEvent(QMouseEvent* pEvent)
{
    const bool bCameraMode = m_nCameraIndex >= 0 && m_nCameraIndex < static_cast<int>(m_pScene->cameras().size());
    if (!(bCameraMode && m_pScene->display().bFixedScale)
        && (pEvent->button() == Qt::LeftButton || pEvent->button() == Qt::MiddleButton || pEvent->button() == Qt::RightButton)) {
        m_bPanning = true;
        m_ptLastMouse = pEvent->position();
    }
}

void AlignView::mouseMoveEvent(QMouseEvent* pEvent)
{
    if (!m_bPanning) {
        return;
    }
    const AlignDisplay display = m_pScene->display();
    WorldViewMapper mapper;
    QRectF rcW = worldRect();
    const double dW = rcW.width() / m_dZoom;
    const double dH = rcW.height() / m_dZoom;
    rcW = QRectF(rcW.center().x() - dW / 2.0, rcW.center().y() - dH / 2.0, dW, dH);
    mapper.setup(rcW, rect(), display.bYDown);
    const QPointF ptDeltaPx = pEvent->position() - m_ptLastMouse;
    m_ptLastMouse = pEvent->position();
    if (mapper.scale() > 1e-6) {
        m_ptPanWorld -= QPointF(ptDeltaPx.x() / mapper.scale(),
                                (display.bYDown ? ptDeltaPx.y() : -ptDeltaPx.y()) / mapper.scale());
        update();
    }
}

void AlignView::mouseReleaseEvent(QMouseEvent*)
{
    m_bPanning = false;
}

void AlignView::mouseDoubleClickEvent(QMouseEvent* pEvent)
{
    m_dZoom = 1.0;
    m_ptPanWorld = QPointF();
    update();
    QWidget::mouseDoubleClickEvent(pEvent);
}

QImage renderAlignSnapshot(AlignScene* pScene, int nCameraIndex, const QSize& szImage)
{
    QImage img(szImage.isValid() ? szImage : QSize(800, 600), QImage::Format_RGB32);
    img.fill(Qt::white);
    if (!pScene) {
        return img;
    }
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const std::vector<AlignCamera> vecCams = pScene->cameras();
    const AlignDisplay display = pScene->display();
    const bool bCameraMode = nCameraIndex >= 0 && nCameraIndex < static_cast<int>(vecCams.size());
    WorldViewMapper mapper;
    const QRectF rcScreen(0, 0, img.width(), img.height());
    if (bCameraMode && display.bFixedScale) {
        const AlignCamera& cam = vecCams[nCameraIndex];
        mapper.setupFixed(QPointF(cam.dCenterX, cam.dCenterY), display.dPxPerMm, rcScreen, display.bYDown);
    } else if (bCameraMode) {
        const AlignCamera& cam = vecCams[nCameraIndex];
        mapper.setup(QRectF(cam.dCenterX - cam.dFovW / 2, cam.dCenterY - cam.dFovH / 2, cam.dFovW, cam.dFovH),
                     rcScreen, display.bYDown);
    } else {
        QRectF rcAll(-120, -80, 240, 160);
        for (const AlignCamera& cam : vecCams) {
            rcAll = rcAll.united(QRectF(cam.dCenterX - cam.dFovW / 2, cam.dCenterY - cam.dFovH / 2, cam.dFovW, cam.dFovH));
        }
        mapper.setup(rcAll, rcScreen, display.bYDown);
    }
    const std::vector<MarkPair> vecMarks = pScene->marks();
    const MarkStyle style = pScene->style();
    if (display.bShowTarget) {
        for (const MarkPair& mark : vecMarks) {
            const double dSz = mark.dSize > 0.0 ? mark.dSize : style.dSize;
            const double dLw = mark.dLineWidth > 0.0 ? mark.dLineWidth : style.dLineWidth;
            painter.save();
            mapper.applyLocalFrame(painter, QPointF(mark.dTargetX, mark.dTargetY), mark.dTargetAngle);
            drawMarkShape(painter, static_cast<int>(style.eTargetShape), dSz, dLw, s_colorTarget, display.bSimulateDefect);
            painter.restore();
        }
    }
    if (display.bShowObject) {
        for (int i = 0; i < static_cast<int>(vecMarks.size()); ++i) {
            const MarkPair& mark = vecMarks[i];
            const double dSz = mark.dSize > 0.0 ? mark.dSize : style.dSize;
            const double dLw = mark.dLineWidth > 0.0 ? mark.dLineWidth : style.dLineWidth;
            const MarkWorldPose obj = pScene->objectMarkWorld(i);
            painter.save();
            mapper.applyLocalFrame(painter, obj.ptPos, obj.dAngleDeg);
            drawMarkShape(painter, static_cast<int>(style.eObjectShape), dSz, dLw, s_colorObject, display.bSimulateDefect);
            painter.restore();
        }
    }
    if (display.bShowCalibMark) {
        for (int i = 0; i < static_cast<int>(vecCams.size()); ++i) {
            const MarkWorldPose calibPose = pScene->calibMarkWorld(i);
            painter.save();
            mapper.applyLocalFrame(painter, calibPose.ptPos, calibPose.dAngleDeg);
            drawMarkShape(painter, display.nCalibShape, display.dCalibSize, display.dCalibLineWidth,
                          s_colorCalib, display.bSimulateDefect);
            painter.restore();
        }
    }
    if (display.nNoiseLevel > 0) {
        drawOpticalNoiseOverlay(painter, rcScreen, display.nNoiseLevel);
    }
    return img;
}

// ============================================================
// ViewWindow
// ============================================================
ViewWindow::ViewWindow(QWidget* pView, const QString& strTitle, QWidget* pParent)
    : QWidget(pParent, Qt::Window)
{
    setWindowTitle(strTitle + QStringLiteral("  (F11/双击 全屏, Esc 退出全屏)"));
    setAttribute(Qt::WA_DeleteOnClose);  // 关闭即释放
    auto* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(pView);
    resize(800, 600);
}

void ViewWindow::keyPressEvent(QKeyEvent* pEvent)
{
    // 20261024 ZJH F11 切换全屏; Esc 仅在全屏时退出全屏
    if (pEvent->key() == Qt::Key_F11) {
        toggleFullScreen();
    } else if (pEvent->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
    } else {
        QWidget::keyPressEvent(pEvent);
    }
}

void ViewWindow::mouseDoubleClickEvent(QMouseEvent*)
{
    toggleFullScreen();
}

void ViewWindow::toggleFullScreen()
{
    // 20261024 ZJH 全屏 ↔ 普通
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}
