// 20261024 ZJH AlignScene 实现
#include "sim/AlignScene.h"

#include "motion/Stage.h"

#include <QMutexLocker>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace {
constexpr double s_dRad2Deg = 180.0 / 3.14159265358979323846;
}

AlignScene::AlignScene(Stage* pStage, QObject* pParent)
    : QObject(pParent)
    , m_pStage(pStage)
{
    applyPresetSingleCamera();  // 默认: 单相机双 Mark
}

void AlignScene::setCameras(const std::vector<AlignCamera>& vecCameras)
{
    {
        QMutexLocker locker(&m_mutex);
        m_vecCameras = vecCameras;
    }
    emit changed();
}

std::vector<AlignCamera> AlignScene::cameras() const
{
    QMutexLocker locker(&m_mutex);
    return m_vecCameras;
}

void AlignScene::setMarks(const std::vector<MarkPair>& vecMarks)
{
    {
        QMutexLocker locker(&m_mutex);
        m_vecMarks = vecMarks;
    }
    emit changed();
}

std::vector<MarkPair> AlignScene::marks() const
{
    QMutexLocker locker(&m_mutex);
    return m_vecMarks;
}

void AlignScene::setStyle(const MarkStyle& style)
{
    {
        QMutexLocker locker(&m_mutex);
        m_style = style;
    }
    emit changed();
}

MarkStyle AlignScene::style() const
{
    QMutexLocker locker(&m_mutex);
    return m_style;
}

void AlignScene::setDisplay(const AlignDisplay& display)
{
    {
        QMutexLocker locker(&m_mutex);
        m_display = display;
        m_display.dPxPerMm = std::max(0.01, m_display.dPxPerMm);  // 防御: 比例必须为正
    }
    emit changed();
}

AlignDisplay AlignScene::display() const
{
    QMutexLocker locker(&m_mutex);
    return m_display;
}

bool AlignScene::objectMarkScreenOffset(int nIndex, QPointF& ptOffsetPx) const
{
    QPointF ptCamCenter;    // 所属相机视窗中心
    AlignDisplay display;   // 显示配置副本
    {
        QMutexLocker locker(&m_mutex);
        // 20261025 ZJH 非固定比例或下标越界 → 无意义
        if (!m_display.bFixedScale || nIndex < 0 || nIndex >= static_cast<int>(m_vecMarks.size())) {
            return false;
        }
        const int nCam = m_vecMarks[nIndex].nCamera;
        if (nCam < 0 || nCam >= static_cast<int>(m_vecCameras.size())) {
            return false;
        }
        ptCamCenter = QPointF(m_vecCameras[nCam].dCenterX, m_vecCameras[nCam].dCenterY);
        display = m_display;
    }
    // 20261025 ZJH objectMarkWorld 内部自己加锁, 放在锁外调用
    ptOffsetPx = screenOffsetPx(objectMarkWorld(nIndex).ptPos, ptCamCenter, display.dPxPerMm, display.bYDown);
    return true;  // 返回有效
}

void AlignScene::setObjectOffset(const Pose& offset)
{
    {
        QMutexLocker locker(&m_mutex);
        m_objectOffset = offset;
    }
    emit changed();
}

Pose AlignScene::objectOffset() const
{
    QMutexLocker locker(&m_mutex);
    return m_objectOffset;
}

void AlignScene::randomizeObjectOffset(double dMaxXY, double dMaxThetaDeg)
{
    QRandomGenerator* pRng = QRandomGenerator::global();  // 线程安全的全局随机源
    // 20261024 ZJH 在 [-max, max] 内均匀取值
    auto uniform = [pRng](double dMax) { return (pRng->generateDouble() * 2.0 - 1.0) * dMax; };
    setObjectOffset(Pose{ uniform(dMaxXY), uniform(dMaxXY), uniform(dMaxThetaDeg) });
}

QPointF AlignScene::objectCentroidLocked() const
{
    QPointF ptSum;  // 坐标累加
    for (const MarkPair& mark : m_vecMarks) {
        ptSum += QPointF(mark.dObjectX, mark.dObjectY);
    }
    // 20261024 ZJH 无 Mark 时返回原点
    return m_vecMarks.empty() ? QPointF() : ptSum / static_cast<double>(m_vecMarks.size());
}

MarkWorldPose AlignScene::objectMarkWorld(int nIndex) const
{
    QPointF ptAfterOffset;     // 加了来料偏差、平台在原点时的位置
    double dAngleAfterOffset;  // 加了来料偏差后的角度
    {
        QMutexLocker locker(&m_mutex);
        // 20261024 ZJH 下标越界返回原点
        if (nIndex < 0 || nIndex >= static_cast<int>(m_vecMarks.size())) {
            return {};
        }
        const MarkPair& mark = m_vecMarks[nIndex];
        // 20261024 ZJH 第一步: 来料偏差, 绕对象 Mark 质心做刚体变换
        ptAfterOffset = applyPose(m_objectOffset, objectCentroidLocked(), QPointF(mark.dObjectX, mark.dObjectY));
        dAngleAfterOffset = mark.dObjectAngle + m_objectOffset.dThetaDeg;
    }
    // 20261024 ZJH 第二步: 平台运动 (Stage 自带锁, 放在本锁外调用避免嵌套)
    MarkWorldPose result;
    result.ptPos = m_pStage ? m_pStage->mapFromHome(ptAfterOffset) : ptAfterOffset;
    result.dAngleDeg = m_pStage ? m_pStage->mapAngleFromHome(dAngleAfterOffset) : dAngleAfterOffset;
    return result;  // 返回实时位姿
}

MarkWorldPose AlignScene::calibMarkWorld(int nCamIndex) const
{
    QPointF ptHome;
    {
        QMutexLocker locker(&m_mutex);
        if (nCamIndex < 0 || nCamIndex >= static_cast<int>(m_vecCameras.size())) {
            return {};
        }
        const AlignCamera& cam = m_vecCameras[nCamIndex];
        ptHome = QPointF(cam.dCenterX + m_display.dCalibOffsetX, cam.dCenterY + m_display.dCalibOffsetY);
    }
    MarkWorldPose result;
    result.ptPos = m_pStage ? m_pStage->mapFromHome(ptHome) : ptHome;
    result.dAngleDeg = m_pStage ? m_pStage->mapAngleFromHome(0.0) : 0.0;
    return result;
}

bool AlignScene::calibMarkScreenOffset(int nCamIndex, QPointF& ptOffsetPx) const
{
    QPointF ptCamCenter;
    AlignDisplay display;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_display.bFixedScale || nCamIndex < 0 || nCamIndex >= static_cast<int>(m_vecCameras.size())) {
            return false;
        }
        ptCamCenter = QPointF(m_vecCameras[nCamIndex].dCenterX, m_vecCameras[nCamIndex].dCenterY);
        display = m_display;
    }
    ptOffsetPx = screenOffsetPx(calibMarkWorld(nCamIndex).ptPos, ptCamCenter, display.dPxPerMm, display.bYDown);
    return true;
}

void AlignScene::centerCalibMark()
{
    double dOffX = 0.0;
    double dOffY = 0.0;
    {
        QMutexLocker locker(&m_mutex);
        m_objectOffset = Pose{};
        dOffX = m_display.bShowCalibMark ? m_display.dCalibOffsetX : 0.0;
        dOffY = m_display.bShowCalibMark ? m_display.dCalibOffsetY : 0.0;
    }
    if (m_pStage) {
        // 20261026 ZJH 移动对位平台至 (-dOffX, -dOffY, 0), 使标定 Mark 正好落在相机视野中心 (cam.dCenterX, cam.dCenterY)
        m_pStage->movePoseAbs(Pose{ -dOffX, -dOffY, 0.0 });
    }
    emit changed();
}

AlignResidual AlignScene::residual() const
{
    const std::vector<MarkPair> vecMarks = marks();  // 拷贝一份, 避免长时间持锁
    AlignResidual res;
    const int nCount = static_cast<int>(vecMarks.size());
    // 20261024 ZJH 没有 Mark 无残差
    if (nCount == 0) {
        return res;
    }
    std::vector<QPointF> vecObj(nCount);  // 对象 Mark 实时位置
    std::vector<QPointF> vecTgt(nCount);  // 目标 Mark 位置
    QPointF ptObjMean, ptTgtMean;         // 两组质心
    for (int i = 0; i < nCount; ++i) {    // 收集点集并累加质心
        const MarkWorldPose obj = objectMarkWorld(i);
        vecObj[i] = obj.ptPos;
        vecTgt[i] = QPointF(vecMarks[i].dTargetX, vecMarks[i].dTargetY);
        res.vecMarkDelta.push_back(vecObj[i] - vecTgt[i]);
        ptObjMean += vecObj[i];
        ptTgtMean += vecTgt[i];
    }
    ptObjMean /= nCount;
    ptTgtMean /= nCount;
    res.bValid = true;
    res.dX = ptObjMean.x() - ptTgtMean.x();  // 质心差
    res.dY = ptObjMean.y() - ptTgtMean.y();

    // 20261024 ZJH 单 Mark: 角度差直接用 Mark 角度
    if (nCount == 1) {
        res.dThetaDeg = normalizeDeg(objectMarkWorld(0).dAngleDeg - vecMarks[0].dTargetAngle);
        return res;
    }
    // 20261024 ZJH 多 Mark: 二维 Procrustes, θ = atan2(Σ t×o, Σ t·o) (去质心后)
    double dCross = 0.0;  // Σ 叉积
    double dDot = 0.0;    // Σ 点积
    for (int i = 0; i < nCount; ++i) {
        const QPointF o = vecObj[i] - ptObjMean;
        const QPointF t = vecTgt[i] - ptTgtMean;
        dCross += t.x() * o.y() - t.y() * o.x();
        dDot += t.x() * o.x() + t.y() * o.y();
    }
    res.dThetaDeg = std::atan2(dCross, dDot) * s_dRad2Deg;
    return res;  // 返回残差
}

void AlignScene::applyPresetSingleCamera()
{
    // 20261024 ZJH 一个相机 60x45mm 视野, 内有两对 Mark, 间距 40mm
    setCameras({ AlignCamera{ QStringLiteral("相机1"), 0.0, 0.0, 60.0, 45.0 } });
    setMarks({ MarkPair{ QStringLiteral("Mark1"), 0, -20.0, 0.0, 0.0, -20.0, 0.0, 0.0 },
               MarkPair{ QStringLiteral("Mark2"), 0, 20.0, 0.0, 0.0, 20.0, 0.0, 0.0 } });
}

void AlignScene::applyPresetDualCamera()
{
    // 20261024 ZJH 两个相机相距 200mm, 各看一对 Mark (视野 20x15mm, 典型高精度对位)
    setCameras({ AlignCamera{ QStringLiteral("相机1"), -100.0, 0.0, 20.0, 15.0 },
                 AlignCamera{ QStringLiteral("相机2"), 100.0, 0.0, 20.0, 15.0 } });
    setMarks({ MarkPair{ QStringLiteral("Mark1"), 0, -100.0, 0.0, 0.0, -100.0, 0.0, 0.0 },
               MarkPair{ QStringLiteral("Mark2"), 1, 100.0, 0.0, 0.0, 100.0, 0.0, 0.0 } });
}

void AlignScene::applyPresetTripleCamera()
{
    // 20261026 ZJH 三个相机呈品字形/L形分布, 各看一对 Mark
    setCameras({ AlignCamera{ QStringLiteral("相机1"), -100.0, -60.0, 20.0, 15.0 },
                 AlignCamera{ QStringLiteral("相机2"), 100.0, -60.0, 20.0, 15.0 },
                 AlignCamera{ QStringLiteral("相机3"), 0.0, 60.0, 20.0, 15.0 } });
    setMarks({ MarkPair{ QStringLiteral("Mark1"), 0, -100.0, -60.0, 0.0, -100.0, -60.0, 0.0 },
               MarkPair{ QStringLiteral("Mark2"), 1, 100.0, -60.0, 0.0, 100.0, -60.0, 0.0 },
               MarkPair{ QStringLiteral("Mark3"), 2, 0.0, 60.0, 0.0, 0.0, 60.0, 0.0 } });
}

void AlignScene::applyPresetQuadCamera()
{
    // 20261024 ZJH 四个相机分布在 200x120mm 矩形四角, 各看一对 Mark
    std::vector<AlignCamera> vecCams;
    std::vector<MarkPair> vecMarks;
    const double arrX[4] = { -100.0, 100.0, 100.0, -100.0 };  // 四角 X
    const double arrY[4] = { 60.0, 60.0, -60.0, -60.0 };      // 四角 Y
    for (int i = 0; i < 4; ++i) {  // 逐角创建相机和 Mark
        vecCams.push_back(AlignCamera{ QStringLiteral("相机%1").arg(i + 1), arrX[i], arrY[i], 20.0, 15.0 });
        vecMarks.push_back(MarkPair{ QStringLiteral("Mark%1").arg(i + 1), i, arrX[i], arrY[i], 0.0, arrX[i], arrY[i], 0.0 });
    }
    setCameras(vecCams);
    setMarks(vecMarks);
}
