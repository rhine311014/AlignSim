// 20261024 ZJH PickScene 实现
#include "sim/PickScene.h"

#include "motion/Stage.h"

#include <QMutexLocker>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace {
constexpr double s_dDeg2Rad = 3.14159265358979323846 / 180.0;

// 20261024 ZJH 世界点 → 以 pose 为原点、pose 角度为 X 轴的局部坐标
QPointF worldToLocal(const Pose& pose, const QPointF& ptWorld)
{
    const double dTheta = pose.dThetaDeg * s_dDeg2Rad;
    const double dRelX = ptWorld.x() - pose.dX;  // 相对中心
    const double dRelY = ptWorld.y() - pose.dY;
    // 20261024 ZJH 反向旋转 −θ
    return QPointF(std::cos(dTheta) * dRelX + std::sin(dTheta) * dRelY,
                   -std::sin(dTheta) * dRelX + std::cos(dTheta) * dRelY);
}

// 20261024 ZJH 局部坐标 → 世界点
QPointF localToWorld(const Pose& pose, const QPointF& ptLocal)
{
    const double dTheta = pose.dThetaDeg * s_dDeg2Rad;
    return QPointF(std::cos(dTheta) * ptLocal.x() - std::sin(dTheta) * ptLocal.y() + pose.dX,
                   std::sin(dTheta) * ptLocal.x() + std::cos(dTheta) * ptLocal.y() + pose.dY);
}
}  // namespace

PickScene::PickScene(Stage* pHead, QObject* pParent)
    : QObject(pParent)
    , m_pHead(pHead)
{
    // 20261024 ZJH 拾取头运动时, 被吸着的产品跟着刷新 (由 products() 实时计算, 这里只转发信号)
    if (m_pHead) {
        connect(m_pHead, &Stage::changed, this, &PickScene::changed);
    }
}

void PickScene::setConfig(const PickSceneConfig& cfg)
{
    {
        QMutexLocker locker(&m_mutex);
        m_cfg = cfg;
        // 20261024 ZJH 防御: 视野/产品尺寸至少 1mm, 数量 0~100
        m_cfg.dFieldW = std::max(1.0, m_cfg.dFieldW);
        m_cfg.dFieldH = std::max(1.0, m_cfg.dFieldH);
        m_cfg.dProductW = std::max(1.0, m_cfg.dProductW);
        m_cfg.dProductH = std::max(1.0, m_cfg.dProductH);
        m_cfg.dMarkTextHeightMm = std::max(0.2, m_cfg.dMarkTextHeightMm);
        m_cfg.nCount = std::clamp(m_cfg.nCount, 0, 100);
    }
    emit changed();
}

PickSceneConfig PickScene::config() const
{
    QMutexLocker locker(&m_mutex);
    return m_cfg;
}

void PickScene::spawnProducts(int nCount, int nSeed)
{
    {
        QMutexLocker locker(&m_mutex);
        // 20261024 ZJH 有种子用固定随机序列 (测试可复现), 否则用系统随机
        QRandomGenerator rng = nSeed >= 0 ? QRandomGenerator(static_cast<quint32>(nSeed))
                                          : QRandomGenerator(QRandomGenerator::global()->generate());
        const int nTarget = std::clamp(nCount > 0 ? nCount : m_cfg.nCount, 0, 100);  // 本次数量
        const double dRadius = 0.5 * std::hypot(m_cfg.dProductW, m_cfg.dProductH);   // 产品外接圆半径
        const double dMargin = dRadius;  // 离视野边缘至少一个外接圆半径, 保证整个产品可见
        m_vecProducts.clear();
        m_arrHeldIndex[0] = -1;  // 重新摆放后不再吸着任何产品
        m_arrHeldIndex[1] = -1;
        for (int i = 0; i < nTarget; ++i) {  // 逐个产品找不重叠的位置
            Product prod;
            prod.nId = i + 1;
            // 20261024 ZJH 拒绝采样: 最多尝试 200 次, 外接圆不相交即接受; 视野太小放不下时接受最后一次
            for (int nTry = 0; nTry < 200; ++nTry) {
                prod.pose.dX = dMargin + rng.generateDouble() * std::max(0.0, m_cfg.dFieldW - 2 * dMargin);
                prod.pose.dY = dMargin + rng.generateDouble() * std::max(0.0, m_cfg.dFieldH - 2 * dMargin);
                bool bOverlap = false;  // 是否与已放产品重叠
                for (const Product& other : m_vecProducts) {
                    if (std::hypot(other.pose.dX - prod.pose.dX, other.pose.dY - prod.pose.dY) < 2 * dRadius) {
                        bOverlap = true;
                        break;
                    }
                }
                if (!bOverlap) {
                    break;
                }
            }
            prod.pose.dThetaDeg = (rng.generateDouble() * 2.0 - 1.0) * m_cfg.dMaxAngleDeg;  // 随机角度
            // 20261026 ZJH 开启随机正反时, 约 50% 概率翻面 (同时若数量≥2 保证至少正反各一方便测试)
            if (m_cfg.bRandomFlip) {
                prod.bFlipped = (rng.bounded(2) == 1);
            }
            m_vecProducts.push_back(prod);
        }
        // 20261026 ZJH 数量≥2 且开启随机正反时, 若碰巧全正或全反则把最后一个翻转, 确保界面每次都有正反对比
        if (m_cfg.bRandomFlip && m_vecProducts.size() >= 2) {
            const bool bFirst = m_vecProducts.front().bFlipped;
            const bool bAllSame = std::all_of(m_vecProducts.begin(), m_vecProducts.end(),
                                              [bFirst](const Product& p) { return p.bFlipped == bFirst; });
            if (bAllSame) {
                m_vecProducts.back().bFlipped = !bFirst;
            }
        }
    }
    emit changed();
}

void PickScene::stepConveyor(double dStepX, int nNewCount)
{
    {
        QMutexLocker locker(&m_mutex);
        QRandomGenerator* pRng = QRandomGenerator::global();
        const double dRadius = 0.5 * std::hypot(m_cfg.dProductW, m_cfg.dProductH);
        const double dMargin = dRadius;
        int nMaxId = 0;
        std::vector<Product> vecKept;
        for (Product& p : m_vecProducts) {
            nMaxId = std::max(nMaxId, p.nId);
            if (p.bPicked || p.bPlaced) {
                vecKept.push_back(p);  // 已被吸起或已在料盘区的产品不受皮带移动影响
                continue;
            }
            p.pose.dX += dStepX;
            if (p.pose.dX <= m_cfg.dFieldW - dMargin * 0.5) {
                vecKept.push_back(p);
            }
        }
        // 在视野左侧入料区补入 nNewCount 个新产品
        const int nToAdd = std::clamp(nNewCount, 0, 20);
        for (int i = 0; i < nToAdd; ++i) {
            Product prod;
            prod.nId = ++nMaxId;
            for (int nTry = 0; nTry < 100; ++nTry) {
                prod.pose.dX = dMargin + pRng->generateDouble() * std::max(10.0, std::min(dStepX, m_cfg.dFieldW * 0.35));
                prod.pose.dY = dMargin + pRng->generateDouble() * std::max(0.0, m_cfg.dFieldH - 2 * dMargin);
                bool bOverlap = false;
                for (const Product& other : vecKept) {
                    if (!other.bPicked && std::hypot(other.pose.dX - prod.pose.dX, other.pose.dY - prod.pose.dY) < 2 * dRadius) {
                        bOverlap = true;
                        break;
                    }
                }
                if (!bOverlap) {
                    break;
                }
            }
            prod.pose.dThetaDeg = (pRng->generateDouble() * 2.0 - 1.0) * m_cfg.dMaxAngleDeg;
            if (m_cfg.bRandomFlip) {
                prod.bFlipped = (pRng->bounded(2) == 1);
            }
            vecKept.push_back(prod);
        }
        m_vecProducts = std::move(vecKept);
    }
    emit changed();
}

bool PickScene::setProductPose(int nIndex, const Pose& pose)
{
    {
        QMutexLocker locker(&m_mutex);
        // 20261024 ZJH 越界或正被吸着的产品不允许手动改
        if (nIndex < 0 || nIndex >= static_cast<int>(m_vecProducts.size())
            || nIndex == m_arrHeldIndex[0] || nIndex == m_arrHeldIndex[1]) {
            return false;
        }
        m_vecProducts[nIndex].pose = pose;
    }
    emit changed();
    return true;  // 返回成功
}

bool PickScene::setProductFlipped(int nIndex, bool bFlipped)
{
    {
        QMutexLocker locker(&m_mutex);
        // 20261026 ZJH 越界或正被吸着的产品不允许手动改
        if (nIndex < 0 || nIndex >= static_cast<int>(m_vecProducts.size())
            || nIndex == m_arrHeldIndex[0] || nIndex == m_arrHeldIndex[1]) {
            return false;
        }
        m_vecProducts[nIndex].bFlipped = bFlipped;
    }
    emit changed();
    return true;  // 返回成功
}

Pose PickScene::headCenterPoseFromStage() const
{
    // 20261024 ZJH 无拾取头时在原点
    if (!m_pHead) {
        return {};
    }
    // 20261025 ZJH 拾取头旋转中心物理上随实际运动 (含误差注入)
    const QPointF ptCenter = m_pHead->mapFromHome(m_pHead->actualRotCenter());
    return Pose{ ptCenter.x(), ptCenter.y(), m_pHead->pose().dThetaDeg };
}

QPointF PickScene::nozzleOffsetLocked(int nNozzle) const
{
    // 20261026 ZJH 单爪模式或 nNozzle<=0 时吸嘴就在旋转中心 (偏移 0,0); 双爪模式下按爪号取偏移
    if (!m_cfg.bDualGripper || nNozzle <= 0) {
        return QPointF(0.0, 0.0);
    }
    return (nNozzle == 2) ? QPointF(m_cfg.dNozzle2OffX, m_cfg.dNozzle2OffY)
                          : QPointF(m_cfg.dNozzle1OffX, m_cfg.dNozzle1OffY);
}

int PickScene::nozzleCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_cfg.bDualGripper ? 2 : 1;
}

QPointF PickScene::nozzleLocalOffset(int nNozzle) const
{
    QMutexLocker locker(&m_mutex);
    return nozzleOffsetLocked(nNozzle);
}

Pose PickScene::nozzlePose(int nNozzle) const
{
    const Pose poseCenter = headCenterPoseFromStage();
    QMutexLocker locker(&m_mutex);
    const QPointF ptOff = nozzleOffsetLocked(nNozzle);
    const QPointF ptNozzle = localToWorld(poseCenter, ptOff);
    return Pose{ ptNozzle.x(), ptNozzle.y(), poseCenter.dThetaDeg };
}

Pose PickScene::headPoseForNozzleTarget(int nNozzle, const Pose& targetPose) const
{
    const QPointF ptRc = m_pHead ? m_pHead->rotCenter() : QPointF();
    QMutexLocker locker(&m_mutex);
    const QPointF ptOff = nozzleOffsetLocked(nNozzle);
    // 20261026 ZJH 目标角度为 θ 时, 爪相对旋转中心在世界坐标下的偏移 = R(θ)·ptOff
    // 要使 爪世界坐标 = 旋转中心 + 平台平移 + R(θ)·ptOff = target(X,Y)
    // 则 平台平移 = target(X,Y) - 旋转中心 - R(θ)·ptOff
    const double dRad = targetPose.dThetaDeg * s_dDeg2Rad;
    const double dRotOffX = std::cos(dRad) * ptOff.x() - std::sin(dRad) * ptOff.y();
    const double dRotOffY = std::sin(dRad) * ptOff.x() + std::cos(dRad) * ptOff.y();
    return Pose{ targetPose.dX - ptRc.x() - dRotOffX,
                 targetPose.dY - ptRc.y() - dRotOffY,
                 targetPose.dThetaDeg };
}

void PickScene::setDisplay(const PickDisplay& display)
{
    {
        QMutexLocker locker(&m_mutex);
        m_display = display;
        // 20261025 ZJH 防御: 比例/尺寸必须为正
        m_display.dPxPerMm = std::max(0.01, m_display.dPxPerMm);
        m_display.dCalibSize = std::max(0.1, m_display.dCalibSize);
        m_display.dCalibLineWidth = std::max(0.01, m_display.dCalibLineWidth);
    }
    emit changed();
}

PickDisplay PickScene::display() const
{
    QMutexLocker locker(&m_mutex);
    return m_display;
}

Pose PickScene::calibMarkPose(int nOverrideMount) const
{
    const Pose poseCenter = headCenterPoseFromStage();  // 先读 Stage (不持本锁)
    QMutexLocker locker(&m_mutex);
    const int nMount = nOverrideMount >= 0 ? nOverrideMount : (m_display.nCalibMount == 3 ? 0 : m_display.nCalibMount);
    // 20261026 ZJH 支持标定 Mark 挂载在旋转中心(0)、1号爪(1) 或 2号爪(2) 上
    const QPointF ptMount = nozzleOffsetLocked(nMount);
    const QPointF ptTotalOff = ptMount + QPointF(m_display.dCalibOffsetX, m_display.dCalibOffsetY);
    const QPointF ptMark = localToWorld(poseCenter, ptTotalOff);
    return Pose{ ptMark.x(), ptMark.y(), poseCenter.dThetaDeg };  // 返回标定 Mark 位姿
}

void PickScene::moveCalibMarkToFieldCenter()
{
    if (!m_pHead) {
        return;
    }
    double dCenterX = 0.0, dCenterY = 0.0;
    {
        QMutexLocker locker(&m_mutex);
        dCenterX = m_cfg.dFieldW * 0.5;
        dCenterY = m_cfg.dFieldH * 0.5;
        // 20261026 ZJH 若用户曾把相对吸嘴的偏移误填成超大坐标 (超过视野半径), 自动复位为默认 (20, 0) mm
        if (std::abs(m_display.dCalibOffsetX) > m_cfg.dFieldW * 0.4
            || std::abs(m_display.dCalibOffsetY) > m_cfg.dFieldH * 0.4) {
            m_display.dCalibOffsetX = 20.0;
            m_display.dCalibOffsetY = 0.0;
        }
        // θ=0 时: 标定Mark世界坐标 = 旋转中心 + 平台位姿平移 + 挂载爪偏移 + Mark偏移 = (W/2, H/2)
        // nCalibMount==3 (双爪同时显示) 时以旋转中心+Mark偏移对准视野中央, 使左右两爪的标定 Mark 对称落在视野中央两侧
        const QPointF ptRc = m_pHead->rotCenter();
        const int nMount = (m_display.nCalibMount == 3) ? 0 : m_display.nCalibMount;
        const QPointF ptMount = nozzleOffsetLocked(nMount);
        dCenterX -= (ptRc.x() + ptMount.x() + m_display.dCalibOffsetX);
        dCenterY -= (ptRc.y() + ptMount.y() + m_display.dCalibOffsetY);
    }
    m_pHead->movePoseAbs(Pose{ dCenterX, dCenterY, 0.0 });
    emit changed();
}

bool PickScene::calibMarkScreenOffset(QPointF& ptOffsetPx) const
{
    const PickDisplay disp = display();
    // 20261025 ZJH 非固定比例时像素位置随窗口变化, 无意义
    if (!disp.bFixedScale) {
        return false;
    }
    const PickSceneConfig cfg = config();
    const Pose poseMark = calibMarkPose();
    ptOffsetPx = screenOffsetPx(QPointF(poseMark.dX, poseMark.dY), QPointF(cfg.dFieldW / 2, cfg.dFieldH / 2),
                                disp.dPxPerMm, disp.bYDown);
    return true;  // 返回有效
}

std::vector<Product> PickScene::products() const
{
    const Pose poseCenter = headCenterPoseFromStage();  // 先读 Stage (不持本锁)
    QMutexLocker locker(&m_mutex);
    std::vector<Product> vecOut = m_vecProducts;
    // 20261026 ZJH 被 1号爪 / 2号爪 吸着的产品: 位姿 = 所属吸嘴实时位姿 ∘ 相对位姿
    for (int k = 0; k < 2; ++k) {
        const int nIdx = m_arrHeldIndex[k];
        if (nIdx >= 0 && nIdx < static_cast<int>(vecOut.size())) {
            const QPointF ptOff = nozzleOffsetLocked(k + 1);
            const QPointF ptNozzle = localToWorld(poseCenter, ptOff);
            const Pose poseNozzle{ ptNozzle.x(), ptNozzle.y(), poseCenter.dThetaDeg };
            const QPointF ptProd = localToWorld(poseNozzle, QPointF(m_arrHeldRelative[k].dX, m_arrHeldRelative[k].dY));
            vecOut[nIdx].pose = Pose{ ptProd.x(), ptProd.y(), poseNozzle.dThetaDeg + m_arrHeldRelative[k].dThetaDeg };
        }
    }
    return vecOut;  // 返回实时产品列表
}

bool PickScene::isInsideProduct(const Product& prod, const QPointF& ptWorld) const
{
    QPointF ptLocal = worldToLocal(prod.pose, ptWorld);  // 产品局部坐标
    // 20261026 ZJH 反面产品沿局部 Y 轴镜像 (L 形缺口随翻面镜像到右下)
    if (prod.bFlipped) {
        ptLocal.setY(-ptLocal.y());
    }
    const double dHalfW = m_cfg.dProductW / 2.0;
    const double dHalfH = m_cfg.dProductH / 2.0;
    switch (m_cfg.eShape) {
    case ProductShape::Circle:
        return std::hypot(ptLocal.x(), ptLocal.y()) <= dHalfW;  // 圆: 半径 = 宽/2
    case ProductShape::LShape: {
        // 20261024 ZJH L 形 = 外框去掉右上 1/2×1/2 的缺口 (与 PickView 绘制一致)
        const bool bInBox = std::abs(ptLocal.x()) <= dHalfW && std::abs(ptLocal.y()) <= dHalfH;
        const bool bInNotch = ptLocal.x() > 0.0 && ptLocal.y() > 0.0;
        return bInBox && !bInNotch;
    }
    case ProductShape::Rect:
    default:
        return std::abs(ptLocal.x()) <= dHalfW && std::abs(ptLocal.y()) <= dHalfH;
    }
}

PickResult PickScene::pick(int nNozzle)
{
    const Pose poseCenter = headCenterPoseFromStage();
    PickResult result;
    {
        QMutexLocker locker(&m_mutex);
        // 20261026 ZJH 单爪模式下统一按 1号爪处理; 双爪模式下允许 1 或 2
        const int nSlot = (m_cfg.bDualGripper && nNozzle == 2) ? 1 : 0;
        result.nNozzle = nSlot + 1;
        ++m_stats.nAttempts;
        // 20261024 ZJH 该爪已吸着产品时不能再吸
        if (m_arrHeldIndex[nSlot] >= 0) {
            m_lastResult = result;
            return result;  // 返回未命中
        }
        const QPointF ptOff = nozzleOffsetLocked(result.nNozzle);
        const QPointF ptNozzle = localToWorld(poseCenter, ptOff);
        const Pose poseNozzle{ ptNozzle.x(), ptNozzle.y(), poseCenter.dThetaDeg };
        int nBest = -1;              // 命中的产品下标
        double dBestDist = 1e300;    // 命中产品中最近的中心距离
        for (int i = 0; i < static_cast<int>(m_vecProducts.size()); ++i) {  // 找该爪吸嘴下方最近的未吸起产品
            const Product& prod = m_vecProducts[i];
            if (prod.bPicked) {
                continue;  // 已被另一只爪吸着的产品跳过
            }
            const double dDist = std::hypot(ptNozzle.x() - prod.pose.dX, ptNozzle.y() - prod.pose.dY);
            // 20261024 ZJH 命中条件: 吸嘴在产品轮廓内, 或距中心在容差内 (L 形中心可能落在缺口里)
            const bool bHit = isInsideProduct(prod, ptNozzle) || dDist <= m_cfg.dPickToleranceMm;
            if (bHit && dDist < dBestDist) {
                nBest = i;
                dBestDist = dDist;
            }
        }
        // 20261024 ZJH 没吸到任何产品
        if (nBest < 0) {
            m_lastResult = result;
            return result;  // 返回未命中
        }
        const Product& prod = m_vecProducts[nBest];
        result.bHit = true;
        result.bFlipped = prod.bFlipped;
        result.nProductId = prod.nId;
        result.dX = poseNozzle.dX - prod.pose.dX;
        result.dY = poseNozzle.dY - prod.pose.dY;
        result.dDist = dBestDist;
        result.dThetaDeg = normalizeDeg(poseNozzle.dThetaDeg - prod.pose.dThetaDeg);
        // 20261026 ZJH 反面产品不应被拾取, 吸到反面视为不合格
        result.bInTolerance = !prod.bFlipped
                           && result.dDist <= m_cfg.dPickToleranceMm
                           && std::abs(result.dThetaDeg) <= m_cfg.dPickToleranceDeg;
        // 20261024 ZJH 记录相对位姿: 产品中心在该爪坐标系下的位置 + 角度差
        const QPointF ptRel = worldToLocal(poseNozzle, QPointF(prod.pose.dX, prod.pose.dY));
        m_arrHeldRelative[nSlot] = Pose{ ptRel.x(), ptRel.y(), prod.pose.dThetaDeg - poseNozzle.dThetaDeg };
        m_arrHeldIndex[nSlot] = nBest;
        m_vecProducts[nBest].bPicked = true;
        m_vecProducts[nBest].nPickedByNozzle = result.nNozzle;
        // 20261024 ZJH 更新统计
        ++m_stats.nHits;
        m_stats.nInTolerance += result.bInTolerance ? 1 : 0;
        m_stats.nFlippedPicked += prod.bFlipped ? 1 : 0;
        m_stats.dMaxDist = std::max(m_stats.dMaxDist, result.dDist);
        m_stats.dSumDist += result.dDist;
        m_stats.dMaxAbsTheta = std::max(m_stats.dMaxAbsTheta, std::abs(result.dThetaDeg));
        m_stats.dSumAbsTheta += std::abs(result.dThetaDeg);
        m_lastResult = result;
    }
    emit changed();
    return result;  // 返回拾取结果
}

bool PickScene::place(int nNozzle)
{
    const std::vector<Product> vecNow = products();  // 含被吸产品的实时位姿
    bool bAnyPlaced = false;
    {
        QMutexLocker locker(&m_mutex);
        for (int k = 0; k < 2; ++k) {
            // 20261026 ZJH nNozzle<=0 表示双爪全部放下; 否则只放下对应爪 (1 或 2)
            if (nNozzle > 0 && nNozzle != k + 1) {
                continue;
            }
            const int nIdx = m_arrHeldIndex[k];
            if (nIdx >= 0 && nIdx < static_cast<int>(m_vecProducts.size())) {
                m_vecProducts[nIdx].pose = vecNow[nIdx].pose;  // 固化到放下时的位置
                m_vecProducts[nIdx].bPicked = false;
                m_vecProducts[nIdx].nPickedByNozzle = 0;
                m_vecProducts[nIdx].bPlaced = true;
                m_arrHeldIndex[k] = -1;
                bAnyPlaced = true;
            }
        }
    }
    if (bAnyPlaced) {
        emit changed();
    }
    return bAnyPlaced;  // 返回是否有产品被放下
}

bool PickScene::isHolding(int nNozzle) const
{
    QMutexLocker locker(&m_mutex);
    if (nNozzle == 1) {
        return m_arrHeldIndex[0] >= 0;
    }
    if (nNozzle == 2) {
        return m_arrHeldIndex[1] >= 0;
    }
    return m_arrHeldIndex[0] >= 0 || m_arrHeldIndex[1] >= 0;
}

PickResult PickScene::lastResult() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastResult;
}

PickStats PickScene::stats() const
{
    QMutexLocker locker(&m_mutex);
    return m_stats;
}

void PickScene::resetStats()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stats = PickStats{};
        m_lastResult = PickResult{};
    }
    emit changed();
}
