// 20261024 ZJH PickView 实现
#include "gui/PickView.h"

#include "gui/AlignView.h"
#include "motion/Stage.h"
#include "sim/PickScene.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <cmath>

namespace {
// 20261026 ZJH 针对黑白工业相机高对比度重新规划配色:
//   - 相机视野底色: 纯白 #FFFFFF (灰度 255)
//   - 产品本体: 碳黑 #08080A (灰度 8, 与白底对比度 255:8, 边缘锐利不漏抓)
//   - 产品正面丝印字符与方向圆点: 纯白 #FFFFFF (灰度 255, 与碳黑产品本体满对比度)
//   - 标定 Mark: 纯黑 #000000 (灰度 0, 与白底对比度 255:0)
const QColor s_colorProduct(8, 8, 10);        // 产品本体: 碳黑 (灰度 8)
const QColor s_colorHeld(234, 88, 12);        // 1号爪被吸产品描边: 工业橙
const QColor s_colorHeld2(37, 99, 235);       // 2号爪被吸产品描边: 工业蓝
const QColor s_colorNozzle(220, 38, 38);      // 1号吸嘴: 鲜红
const QColor s_colorNozzle2(2, 132, 199);     // 2号吸嘴: 亮青蓝
const QColor s_colorLetterbox(30, 34, 42);    // 视野外背景: 深石板灰

// 20261024 ZJH 产品轮廓 (局部 mm 坐标, 中心在原点, 与 PickScene::isInsideProduct 一致)
QPainterPath productPath(const PickSceneConfig& cfg)
{
    QPainterPath path;
    const double dW = cfg.dProductW;
    const double dH = cfg.dProductH;
    switch (cfg.eShape) {
    case ProductShape::Circle:
        path.addEllipse(QPointF(0, 0), dW / 2, dW / 2);  // 圆: 直径 = 宽
        break;
    case ProductShape::LShape: {
        // 20261024 ZJH L 形: 外框去掉 x>0 且 y>0 的四分之一
        QPolygonF poly;
        poly << QPointF(-dW / 2, -dH / 2) << QPointF(dW / 2, -dH / 2) << QPointF(dW / 2, 0)
             << QPointF(0, 0) << QPointF(0, dH / 2) << QPointF(-dW / 2, dH / 2);
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case ProductShape::Rect:
    default:
        path.addRect(QRectF(-dW / 2, -dH / 2, dW, dH));
        break;
    }
    return path;  // 返回轮廓
}

// 20261024 ZJH 方向圆点位置: 放在 +X 一侧边缘附近, 小尺寸标识 (类似实物盖板边缘的小孔/标识)
QPointF featurePoint(const PickSceneConfig& cfg)
{
    if (cfg.eShape == ProductShape::Circle) {
        return QPointF(cfg.dProductW * 0.35, 0.0);
    }
    if (cfg.eShape == ProductShape::LShape) {
        return QPointF(-cfg.dProductW * 0.25, -cfg.dProductH * 0.32);
    }
    return QPointF(cfg.dProductW * 0.36, 0.0);
}

// 20261026 ZJH 生成产品正面丝印字符轮廓路径 (局部 mm 坐标, +X 向右, +Y 向上)
// 说明:
//   1. 只有正面 (bFlipped=false) 才绘制该字符, 反面朝上时看不到字符 (模拟背面无丝印)
//   2. 字符按配置的实际物理高度 dMarkTextHeightMm (默认 3.5mm 小字) 放在方向圆点旁/底部边缘附近, 类似实物底部小标识
QPainterPath productTextPath(const PickSceneConfig& cfg)
{
    QPainterPath pathOut;
    const QString strText = cfg.strMarkText.trimmed();
    if (strText.isEmpty()) {
        return pathOut;
    }
    QFont font(QStringLiteral("Arial"));
    font.setBold(true);
    font.setPixelSize(100);  // 先在字体像素空间生成高分辨率矢量轮廓
    QPainterPath pathRaw;
    pathRaw.addText(QPointF(0, 0), font, strText);
    const QRectF rcRaw = pathRaw.boundingRect();
    if (rcRaw.width() <= 1e-6 || rcRaw.height() <= 1e-6) {
        return pathOut;
    }
    // 20261026 ZJH 按设定字高 (mm) 缩放, 同时限制不超过产品宽度的 45%
    const double dTargetH = std::clamp(cfg.dMarkTextHeightMm, 0.2, std::min(cfg.dProductW, cfg.dProductH) * 0.6);
    const double dMaxW = cfg.dProductW * 0.45;
    const double dScale = std::min(dTargetH / rcRaw.height(), dMaxW / rcRaw.width());
    // 20261026 ZJH 放在靠近方向圆点的一侧 (与小圆点组成类似实物底部边缘的小标识区)
    const QPointF ptTargetCenter = (cfg.eShape == ProductShape::LShape)
                                       ? QPointF(0.0, -cfg.dProductH * 0.32)
                                       : QPointF(cfg.dProductW * 0.14, 0.0);
    QTransform tf;
    tf.translate(ptTargetCenter.x(), ptTargetCenter.y());
    tf.scale(dScale, -dScale);
    tf.translate(-rcRaw.center().x(), -rcRaw.center().y());
    return tf.map(pathRaw);
}
}  // namespace

PickView::PickView(PickScene* pScene, QWidget* pParent)
    : QWidget(pParent)
    , m_pScene(pScene)
{
    setMinimumSize(240, 160);
    // 20261024 ZJH 场景变化 (含拾取头运动, PickScene 已转发 Stage::changed) → 排队重绘
    connect(m_pScene, &PickScene::changed, this, qOverload<>(&QWidget::update), Qt::QueuedConnection);
}

void PickView::setShowLabels(bool bShow)
{
    m_bShowLabels = bShow;
    update();
}

void PickView::setShowHead(bool bShow)
{
    m_bShowHead = bShow;
    update();
}

void PickView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const PickSceneConfig cfg = m_pScene->config();
    const PickDisplay disp = m_pScene->display();
    WorldViewMapper mapper;
    // 20261025 ZJH 固定比例: 视野中心显示在窗口中心, 比例恒定; 否则等比适配窗口 (开启标注时右侧留出料盘与NG盒可视区)
    if (disp.bFixedScale) {
        mapper.setupFixed(QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2), disp.dPxPerMm, rect(), disp.bYDown);
    } else {
        const double dExtraRight = m_bShowLabels ? 185.0 : 0.0;
        mapper.setup(QRectF(0, 0, cfg.dFieldW + dExtraRight, cfg.dFieldH), rect(), disp.bYDown);
    }

    // 20261026 ZJH 视野外深石板灰, 视野内 100% 纯白 #FFFFFF (黑白相机高对比背光底色)
    painter.fillRect(rect(), s_colorLetterbox);
    // 20261025 ZJH 白底只画视野范围 (固定比例时视野可能小于或大于窗口)
    const QRectF rcField = QRectF(mapper.toScreen(QPointF(0, 0)), mapper.toScreen(QPointF(cfg.dFieldW, cfg.dFieldH))).normalized();
    painter.fillRect(rcField, Qt::white);

    // 20261026 ZJH 在自适应观察模式 (m_bShowLabels) 下绘制右侧 2×5 OK 料盘区与 NG 抛料盒示意框, 且不裁剪放料区产品
    if (!disp.bFixedScale && m_bShowLabels) {
        painter.save();
        painter.setPen(QPen(QColor(110, 180, 120), 1.2, Qt::DashLine));
        painter.setBrush(QColor(80, 120, 85, 45));
        const QRectF rcTray = QRectF(mapper.toScreen(QPointF(620, 40)), mapper.toScreen(QPointF(775, 320))).normalized();
        painter.drawRect(rcTray);
        painter.setPen(QColor(160, 230, 170));
        painter.drawText(rcTray.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft, QStringLiteral("OK 料盘区 (2×5)"));

        painter.setPen(QPen(QColor(220, 100, 100), 1.2, Qt::DashLine));
        painter.setBrush(QColor(140, 60, 60, 50));
        const QRectF rcNg = QRectF(mapper.toScreen(QPointF(620, 330)), mapper.toScreen(QPointF(775, 395))).normalized();
        painter.drawRect(rcNg);
        painter.setPen(QColor(255, 160, 160));
        painter.drawText(rcNg.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft, QStringLiteral("NG 抛料盒"));
        painter.restore();
    } else {
        painter.setClipRect(rcField);
    }

    const QPainterPath pathProduct = productPath(cfg);
    const QPainterPath pathText = productTextPath(cfg);
    const QPointF ptFeature = featurePoint(cfg);
    const double dFeatureR = std::clamp(std::min(cfg.dProductW, cfg.dProductH) * 0.045, 0.5, 4.0);  // 小定位圆点半径
    // 20261025 ZJH 标定时可隐藏产品, 避免视觉误识别
    const std::vector<Product> vecProd = disp.bShowProducts ? m_pScene->products() : std::vector<Product>();
    for (const Product& prod : vecProd) {  // 逐个画产品
        painter.save();
        mapper.applyLocalFrame(painter, QPointF(prod.pose.dX, prod.pose.dY), prod.pose.dThetaDeg);
        // 20261026 ZJH 反面产品: 物理翻面 (L形轮廓镜像), 且背面看不到正面的丝印字符
        if (prod.bFlipped) {
            painter.scale(1.0, -1.0);
        }
        if (disp.nBlurLevel > 0) {
            painter.setPen(QPen(QColor(80, 80, 80, 85), (1.5 + disp.nBlurLevel * 0.8) / mapper.scale()));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(pathProduct);
        }
        const QColor colorOutline = (prod.nPickedByNozzle == 2) ? s_colorHeld2 : s_colorHeld;
        painter.setPen(prod.bPicked ? QPen(colorOutline, 1.8 / mapper.scale()) : Qt::NoPen);  // 1.8 像素宽描边
        painter.setBrush(s_colorProduct);
        painter.drawPath(pathProduct);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(ptFeature, dFeatureR, dFeatureR);
        // 20261026 ZJH 只有正面 (bFlipped=false) 才画丝印字符; 反面时看不到字符
        if (!prod.bFlipped && !pathText.isEmpty()) {
            painter.drawPath(pathText);
        }
        // 20261026 ZJH 模拟产品边缘微小崩边/表面亮斑瑕疵
        if (disp.bSimulateDefect) {
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPointF(-cfg.dProductW * 0.48, cfg.dProductH * 0.2), dFeatureR * 1.1, dFeatureR * 0.8);
            painter.drawEllipse(QPointF(-cfg.dProductW * 0.2, cfg.dProductH * 0.15), dFeatureR * 0.6, dFeatureR * 0.6);
        }
        painter.restore();
        // 20261026 ZJH 编号仅在非固定比例的人眼观察模式下显示 (固定比例拍屏时自动隐藏文字, 防止黑白相机把编号误识为特征点)
        if (m_bShowLabels && !disp.bFixedScale) {
            painter.setPen(prod.bFlipped ? QColor(220, 38, 38) : QColor(30, 41, 59));
            const QString strTag = QStringLiteral("#%1%2").arg(prod.nId).arg(prod.bFlipped ? QStringLiteral("反") : QStringLiteral("正"));
            const double dCornerPx = std::min(cfg.dProductW, cfg.dProductH) * 0.42 * mapper.scale();
            painter.drawText(QRectF(mapper.toScreen(QPointF(prod.pose.dX, prod.pose.dY)) + QPointF(-26, -dCornerPx - 18),
                                    QSizeF(52, 18)),
                             Qt::AlignCenter, strTag);
        }
    }

    // 20261025 ZJH 拾取头标定 Mark (黑色, 随拾取头平移和旋转), 供大视野相机做手眼标定
    if (disp.bShowCalibMark) {
        auto drawOneCalibMark = [&](int nOverrideMount) {
            const Pose poseMark = m_pScene->calibMarkPose(nOverrideMount);
            painter.save();
            mapper.applyLocalFrame(painter, QPointF(poseMark.dX, poseMark.dY), poseMark.dThetaDeg);
            if (disp.nBlurLevel > 0) {
                drawMarkShape(painter, disp.nCalibShape,
                              disp.dCalibSize + disp.nBlurLevel * 0.2,
                              disp.dCalibLineWidth + disp.nBlurLevel * 0.15,
                              QColor(0, 0, 0, 75), false);
            }
            drawMarkShape(painter, disp.nCalibShape, disp.dCalibSize, disp.dCalibLineWidth, Qt::black, disp.bSimulateDefect);
            painter.restore();
        };
        if (disp.nCalibMount == 3) {
            // 双爪同时显示两个标定 Mark (1号爪 + 2号爪)
            drawOneCalibMark(1);
            drawOneCalibMark(2);
        } else {
            drawOneCalibMark(-1);
        }
    }
    if (disp.nNoiseLevel > 0) {
        drawOpticalNoiseOverlay(painter, rcField, disp.nNoiseLevel);
    }

    // 20261024 ZJH 拾取头(平台): 单爪画 1 个吸嘴, 双爪画横梁 + 1号爪(红) + 2号爪(蓝); 标定模式或关闭显示平台时自动隐藏, 避免干扰黑白相机识别
    if (m_bShowHead && disp.bShowStage && !disp.bShowCalibMark) {
        const double dPx = 1.0 / mapper.scale();  // 1 像素对应的 mm
        auto drawNozzleAt = [&](const Pose& poseN, const QColor& color, const QString& strTag) {
            painter.save();
            mapper.applyLocalFrame(painter, QPointF(poseN.dX, poseN.dY), poseN.dThetaDeg);
            painter.setPen(QPen(color, 2 * dPx));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(0, 0), 8 * dPx, 8 * dPx);
            painter.drawLine(QPointF(-14 * dPx, 0), QPointF(22 * dPx, 0));  // 正方向更长
            painter.drawLine(QPointF(0, -14 * dPx), QPointF(0, 14 * dPx));
            painter.restore();
            if (m_bShowLabels && !strTag.isEmpty()) {
                painter.setPen(color);
                painter.drawText(QRectF(mapper.toScreen(QPointF(poseN.dX, poseN.dY)) + QPointF(10, -18), QSizeF(42, 16)),
                                 Qt::AlignLeft | Qt::AlignVCenter, strTag);
            }
        };
        if (!cfg.bDualGripper) {
            drawNozzleAt(m_pScene->nozzlePose(1), s_colorNozzle, QString());
        } else {
            const Pose poseCenter = m_pScene->nozzlePose(0);
            const Pose poseN1 = m_pScene->nozzlePose(1);
            const Pose poseN2 = m_pScene->nozzlePose(2);
            // 20261026 ZJH 双爪连接横梁与旋转中心小圆点 (屏幕坐标绘制虚线)
            painter.save();
            QPen penBeam(QColor(120, 120, 120, 200), 1.5, Qt::DashLine);
            painter.setPen(penBeam);
            const QPointF ptS1 = mapper.toScreen(QPointF(poseN1.dX, poseN1.dY));
            const QPointF ptSC = mapper.toScreen(QPointF(poseCenter.dX, poseCenter.dY));
            const QPointF ptS2 = mapper.toScreen(QPointF(poseN2.dX, poseN2.dY));
            painter.drawLine(ptS1, ptSC);
            painter.drawLine(ptSC, ptS2);
            painter.setPen(QPen(QColor(90, 90, 90), 1.5));
            painter.setBrush(Qt::white);
            painter.drawEllipse(ptSC, 3.5, 3.5);
            painter.restore();
            drawNozzleAt(poseN1, s_colorNozzle, QStringLiteral("爪1"));
            drawNozzleAt(poseN2, s_colorNozzle2, QStringLiteral("爪2"));
        }
    }

    painter.setClipping(false);
    // 20261024 ZJH 右下角状态
    if (m_bShowLabels) {
        painter.setPen(Qt::lightGray);
        painter.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom,
                         QStringLiteral("视野 %1×%2 mm  比例 %3 px/mm  (左键拖拽产品/双击翻正反/右键移吸嘴)")
                             .arg(cfg.dFieldW).arg(cfg.dFieldH).arg(mapper.scale(), 0, 'f', 3));
    }
}

int PickView::hitTestProduct(const QPointF& ptWorld) const
{
    const PickSceneConfig cfg = m_pScene->config();
    const double dHitRadius = 0.5 * std::max(cfg.dProductW, cfg.dProductH);
    const std::vector<Product> vecProd = m_pScene->products();
    int nBest = -1;
    double dBestDist = dHitRadius;
    for (int i = 0; i < static_cast<int>(vecProd.size()); ++i) {
        const double d = std::hypot(ptWorld.x() - vecProd[i].pose.dX, ptWorld.y() - vecProd[i].pose.dY);
        if (d <= dBestDist) {
            dBestDist = d;
            nBest = i;
        }
    }
    return nBest;
}

void PickView::mousePressEvent(QMouseEvent* pEvent)
{
    const PickSceneConfig cfg = m_pScene->config();
    const PickDisplay disp = m_pScene->display();
    WorldViewMapper mapper;
    if (disp.bFixedScale) {
        mapper.setupFixed(QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2), disp.dPxPerMm, rect(), disp.bYDown);
    } else {
        const double dExtraRight = m_bShowLabels ? 185.0 : 0.0;
        mapper.setup(QRectF(0, 0, cfg.dFieldW + dExtraRight, cfg.dFieldH), rect(), disp.bYDown);
    }
    const QPointF ptWorld = mapper.toWorld(pEvent->position());
    if (pEvent->button() == Qt::LeftButton) {
        const int nIdx = hitTestProduct(ptWorld);
        if (nIdx >= 0) {
            const std::vector<Product> vecProd = m_pScene->products();
            emit productSelected(nIdx);
            if (!vecProd[nIdx].bPicked) {
                m_nDragIndex = nIdx;
                m_ptDragOffset = QPointF(vecProd[nIdx].pose.dX, vecProd[nIdx].pose.dY) - ptWorld;
            }
        }
    } else if (pEvent->button() == Qt::RightButton) {
        // 20261026 ZJH 右键点击视野任意位置 → 直接把当前选定爪 (1号爪或2号爪) 移到该世界坐标
        const int nNozzle = cfg.bDualGripper ? m_nActiveNozzle : 1;
        const double dCurTheta = m_pScene->head()->commandedPose().dThetaDeg;
        const Pose poseCmd = m_pScene->headPoseForNozzleTarget(nNozzle, Pose{ ptWorld.x(), ptWorld.y(), dCurTheta });
        m_pScene->head()->movePoseAbs(poseCmd);
    }
}

void PickView::mouseMoveEvent(QMouseEvent* pEvent)
{
    if (m_nDragIndex < 0) {
        return;
    }
    const PickSceneConfig cfg = m_pScene->config();
    const PickDisplay disp = m_pScene->display();
    WorldViewMapper mapper;
    if (disp.bFixedScale) {
        mapper.setupFixed(QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2), disp.dPxPerMm, rect(), disp.bYDown);
    } else {
        const double dExtraRight = m_bShowLabels ? 185.0 : 0.0;
        mapper.setup(QRectF(0, 0, cfg.dFieldW + dExtraRight, cfg.dFieldH), rect(), disp.bYDown);
    }
    const QPointF ptWorld = mapper.toWorld(pEvent->position()) + m_ptDragOffset;
    const std::vector<Product> vecProd = m_pScene->products();
    if (m_nDragIndex < static_cast<int>(vecProd.size())) {
        m_pScene->setProductPose(m_nDragIndex, Pose{ ptWorld.x(), ptWorld.y(), vecProd[m_nDragIndex].pose.dThetaDeg });
    }
}

void PickView::mouseReleaseEvent(QMouseEvent*)
{
    m_nDragIndex = -1;
}

void PickView::mouseDoubleClickEvent(QMouseEvent* pEvent)
{
    const PickSceneConfig cfg = m_pScene->config();
    const PickDisplay disp = m_pScene->display();
    WorldViewMapper mapper;
    if (disp.bFixedScale) {
        mapper.setupFixed(QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2), disp.dPxPerMm, rect(), disp.bYDown);
    } else {
        const double dExtraRight = m_bShowLabels ? 185.0 : 0.0;
        mapper.setup(QRectF(0, 0, cfg.dFieldW + dExtraRight, cfg.dFieldH), rect(), disp.bYDown);
    }
    const int nIdx = hitTestProduct(mapper.toWorld(pEvent->position()));
    if (nIdx >= 0) {
        const std::vector<Product> vecProd = m_pScene->products();
        m_pScene->setProductFlipped(nIdx, !vecProd[nIdx].bFlipped);
        return;
    }
    QWidget::mouseDoubleClickEvent(pEvent);
}

void PickView::wheelEvent(QWheelEvent* pEvent)
{
    const PickSceneConfig cfg = m_pScene->config();
    const PickDisplay disp = m_pScene->display();
    WorldViewMapper mapper;
    if (disp.bFixedScale) {
        mapper.setupFixed(QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2), disp.dPxPerMm, rect(), disp.bYDown);
    } else {
        const double dExtraRight = m_bShowLabels ? 185.0 : 0.0;
        mapper.setup(QRectF(0, 0, cfg.dFieldW + dExtraRight, cfg.dFieldH), rect(), disp.bYDown);
    }
    const int nIdx = hitTestProduct(mapper.toWorld(pEvent->position()));
    if (nIdx >= 0) {
        const std::vector<Product> vecProd = m_pScene->products();
        if (!vecProd[nIdx].bPicked) {
            const double dStepDeg = pEvent->angleDelta().y() > 0 ? 5.0 : -5.0;
            Pose pose = vecProd[nIdx].pose;
            pose.dThetaDeg = normalizeDeg(pose.dThetaDeg + dStepDeg);
            m_pScene->setProductPose(nIdx, pose);
            emit productSelected(nIdx);
            pEvent->accept();
            return;
        }
    }
    QWidget::wheelEvent(pEvent);
}

QImage renderPickSnapshot(PickScene* pScene, const QSize& szImage)
{
    QImage img(szImage.isValid() ? szImage : QSize(900, 600), QImage::Format_RGB32);
    img.fill(Qt::white);
    if (!pScene) {
        return img;
    }
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const PickSceneConfig cfg = pScene->config();
    const PickDisplay disp = pScene->display();
    const QRectF rcScreen(0, 0, img.width(), img.height());
    WorldViewMapper mapper;
    if (disp.bFixedScale) {
        mapper.setupFixed(QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2), disp.dPxPerMm, rcScreen, disp.bYDown);
    } else {
        mapper.setup(QRectF(0, 0, cfg.dFieldW, cfg.dFieldH), rcScreen, disp.bYDown);
    }
    const QPainterPath pathProduct = productPath(cfg);
    const QPainterPath pathText = productTextPath(cfg);
    const QPointF ptFeature = featurePoint(cfg);
    const double dFeatureR = std::clamp(std::min(cfg.dProductW, cfg.dProductH) * 0.045, 0.5, 4.0);
    const std::vector<Product> vecProd = disp.bShowProducts ? pScene->products() : std::vector<Product>();
    for (const Product& prod : vecProd) {
        painter.save();
        mapper.applyLocalFrame(painter, QPointF(prod.pose.dX, prod.pose.dY), prod.pose.dThetaDeg);
        if (prod.bFlipped) {
            painter.scale(1.0, -1.0);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(s_colorProduct);
        painter.drawPath(pathProduct);
        painter.setBrush(Qt::white);
        painter.drawEllipse(ptFeature, dFeatureR, dFeatureR);
        if (!prod.bFlipped && !pathText.isEmpty()) {
            painter.drawPath(pathText);
        }
        if (disp.bSimulateDefect) {
            painter.drawEllipse(QPointF(-cfg.dProductW * 0.48, cfg.dProductH * 0.2), dFeatureR * 1.1, dFeatureR * 0.8);
        }
        painter.restore();
    }
    if (disp.bShowCalibMark) {
        auto drawOne = [&](int nOverrideMount) {
            const Pose poseMark = pScene->calibMarkPose(nOverrideMount);
            painter.save();
            mapper.applyLocalFrame(painter, QPointF(poseMark.dX, poseMark.dY), poseMark.dThetaDeg);
            drawMarkShape(painter, disp.nCalibShape, disp.dCalibSize, disp.dCalibLineWidth, Qt::black, disp.bSimulateDefect);
            painter.restore();
        };
        if (disp.nCalibMount == 3) {
            drawOne(1);
            drawOne(2);
        } else {
            drawOne(-1);
        }
    }
    if (disp.nNoiseLevel > 0) {
        drawOpticalNoiseOverlay(painter, rcScreen, disp.nNoiseLevel);
    }
    return img;
}
