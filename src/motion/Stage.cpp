// 20261024 ZJH Stage 实现
#include "motion/Stage.h"

#include <QMutexLocker>
#include <algorithm>
#include <cmath>

namespace {
constexpr double s_dDeg2Rad = 3.14159265358979323846 / 180.0;
}

Stage::Stage(const QString& strName, QObject* pParent)
    : QObject(pParent)
    , m_strName(strName)
    , m_pKin(createKinematics(m_geo))  // 默认 XYD
{
    QMutexLocker locker(&m_mutex);
    rebuildActualLocked();
}

void Stage::rebuildActualLocked()
{
    m_geoActual = m_geo;
    // 20261025 ZJH 误差模型开启时: 实际旋转中心 = 标称 + 偏差; 实际安装点 = 标称 + 偏差 (µm → mm)
    if (m_err.bEnabled) {
        m_geoActual.dRotCenterX += m_err.dRotCenterErrX;
        m_geoActual.dRotCenterY += m_err.dRotCenterErrY;
        for (int i = 0; i < 3; ++i) {
            // 20261026 ZJH 同步平移安装点基准使 m_pKinActual 的力臂只受 arrUvwMountErrUm 影响,
            //              而旋转中心整体偏移 dRotCenterErrX/Y 由 mapFromHome(actualRotCenter) 体现
            m_geoActual.arrUvw[i].dMountX += m_err.dRotCenterErrX + m_err.arrUvwMountErrUm[i * 2] * 1e-3;
            m_geoActual.arrUvw[i].dMountY += m_err.dRotCenterErrY + m_err.arrUvwMountErrUm[i * 2 + 1] * 1e-3;
        }
    }
    m_pKinActual = createKinematics(m_geoActual);
    m_rng.seed(static_cast<std::mt19937::result_type>(m_err.nSeed));  // 重新播种, 保证可复现
    std::fill(std::begin(m_arrBacklash), std::end(m_arrBacklash), 0.0);
    std::fill(std::begin(m_arrNoise), std::end(m_arrNoise), 0.0);
}

void Stage::setGeometry(const StageGeometry& geo)
{
    {
        QMutexLocker locker(&m_mutex);
        m_geo = geo;
        m_pKin = createKinematics(geo);  // 重建标称运动学
        rebuildActualLocked();
        m_axes = AxisValues{};           // 结构变了, 旧轴值无意义, 回零
        m_actualAxes = AxisValues{};
        updatePosesLocked();
    }
    emit changed();
}

StageGeometry Stage::geometry() const
{
    QMutexLocker locker(&m_mutex);
    return m_geo;  // 返回标称几何副本
}

void Stage::setErrorModel(const StageErrorModel& err)
{
    {
        QMutexLocker locker(&m_mutex);
        m_err = err;
        // 20261025 ZJH 防御: 误差/速度不能为负
        m_err.dRepeatUm = std::max(0.0, m_err.dRepeatUm);
        m_err.dRepeatDeg = std::max(0.0, m_err.dRepeatDeg);
        m_err.dBacklashUm = std::max(0.0, m_err.dBacklashUm);
        m_err.dBacklashDeg = std::max(0.0, m_err.dBacklashDeg);
        m_err.dSpeedMmPerS = std::max(0.0, m_err.dSpeedMmPerS);
        m_err.dSpeedDegPerS = std::max(0.0, m_err.dSpeedDegPerS);
        m_err.nSettleMs = std::max(0, m_err.nSettleMs);
        rebuildActualLocked();
        applyMoveLocked(m_axes);  // 按当前指令位置重算实际位置
    }
    emit changed();
}

StageErrorModel Stage::errorModel() const
{
    QMutexLocker locker(&m_mutex);
    return m_err;
}

void Stage::setUnitPowers(int nLinearPower, int nAnglePower)
{
    QMutexLocker locker(&m_mutex);
    // 20261024 ZJH 限定 0~6 位小数, 防止 10^n 溢出或配置写错
    m_nLinearPower = std::clamp(nLinearPower, 0, 6);
    m_nAnglePower = std::clamp(nAnglePower, 0, 6);
}

int Stage::linearPower() const
{
    QMutexLocker locker(&m_mutex);
    return m_nLinearPower;
}

int Stage::anglePower() const
{
    QMutexLocker locker(&m_mutex);
    return m_nAnglePower;
}

double Stage::int32Divisor(int nIndex) const
{
    QMutexLocker locker(&m_mutex);
    // 20261024 ZJH 角度轴用角度倍率, 其余用直线倍率
    const int nPower = m_pKin->isAngleAxis(nIndex) ? m_nAnglePower : m_nLinearPower;
    return std::pow(10.0, nPower);  // 返回除数
}

bool Stage::isAngleAxis(int nIndex) const
{
    QMutexLocker locker(&m_mutex);
    return m_pKin->isAngleAxis(nIndex);
}

void Stage::applyMoveLocked(const AxisValues& axesNew)
{
    const double arrOld[3] = { m_axes.dA1, m_axes.dA2, m_axes.dA3 };     // 旧指令
    const double arrNew[3] = { axesNew.dA1, axesNew.dA2, axesNew.dA3 };  // 新指令
    double arrAct[3] = { arrNew[0], arrNew[1], arrNew[2] };              // 实际轴值
    m_axes = axesNew;
    // 20261025 ZJH 误差模型开启: 逐轴叠加 比例 + 间隙迟滞 + 随机误差
    if (m_err.bEnabled) {
        for (int i = 0; i < 3; ++i) {
            const bool bAngle = m_pKin->isAngleAxis(i);
            const double dBacklash = bAngle ? m_err.dBacklashDeg : m_err.dBacklashUm * 1e-3;  // 间隙 (mm 或 °)
            const double dSigma = bAngle ? m_err.dRepeatDeg : m_err.dRepeatUm * 1e-3;         // σ (mm 或 °)
            const double dDelta = arrNew[i] - arrOld[i];
            // 20261025 ZJH 迟滞: 正向移动消除间隙, 负向移动后停在 +间隙; 不动保持
            if (dDelta > 0.0) {
                m_arrBacklash[i] = 0.0;
            } else if (dDelta < 0.0) {
                m_arrBacklash[i] = dBacklash;
            }
            // 20261025 ZJH 随机误差: 只在该轴实际移动时重新抽样 (静止的轴不应抖动)
            if (dDelta != 0.0 && dSigma > 0.0) {
                m_arrNoise[i] = std::normal_distribution<double>(0.0, dSigma)(m_rng);
            }
            arrAct[i] = arrNew[i] * (1.0 + m_err.arrScalePpm[i] * 1e-6) + m_arrBacklash[i] + m_arrNoise[i];
        }
    }
    m_actualAxes = AxisValues{ arrAct[0], arrAct[1], arrAct[2] };
    updatePosesLocked();
}

void Stage::moveAxesAbs(const AxisValues& axes)
{
    {
        QMutexLocker locker(&m_mutex);
        applyMoveLocked(axes);  // 绝对移动: 直接设指令轴值
    }
    emit changed();
}

void Stage::moveAxesRel(const AxisValues& delta)
{
    {
        QMutexLocker locker(&m_mutex);
        // 20261024 ZJH 相对移动: 指令轴值累加
        applyMoveLocked(AxisValues{ m_axes.dA1 + delta.dA1, m_axes.dA2 + delta.dA2, m_axes.dA3 + delta.dA3 });
    }
    emit changed();
}

void Stage::movePoseAbs(const Pose& pose)
{
    {
        QMutexLocker locker(&m_mutex);
        applyMoveLocked(m_pKin->poseToAxes(pose));  // 标称逆解 (PLC 只知道标称几何)
    }
    emit changed();
}

void Stage::movePoseRel(const Pose& delta)
{
    {
        QMutexLocker locker(&m_mutex);
        // 20261024 ZJH 位姿相对移动: 在当前**指令**位姿上叠加 (PLC 不知道实际误差)
        const Pose poseNew{ m_poseCmd.dX + delta.dX, m_poseCmd.dY + delta.dY, m_poseCmd.dThetaDeg + delta.dThetaDeg };
        applyMoveLocked(m_pKin->poseToAxes(poseNew));
    }
    emit changed();
}

void Stage::home()
{
    moveAxesAbs(AxisValues{});  // 指令轴值全 0
}

AxisValues Stage::axesForPose(const Pose& pose) const
{
    QMutexLocker locker(&m_mutex);
    return m_pKin->poseToAxes(pose);  // 返回标称逆解
}

AxisValues Stage::axes() const
{
    QMutexLocker locker(&m_mutex);
    return m_axes;
}

AxisValues Stage::actualAxes() const
{
    QMutexLocker locker(&m_mutex);
    return m_actualAxes;
}

Pose Stage::commandedPose() const
{
    QMutexLocker locker(&m_mutex);
    return m_poseCmd;
}

Pose Stage::pose() const
{
    QMutexLocker locker(&m_mutex);
    return m_pose;
}

bool Stage::isPoseValid() const
{
    QMutexLocker locker(&m_mutex);
    return m_bPoseValid;
}

QStringList Stage::axisNames() const
{
    QMutexLocker locker(&m_mutex);
    return m_pKin->axisNames();
}

QPointF Stage::rotCenter() const
{
    QMutexLocker locker(&m_mutex);
    return QPointF(m_geo.dRotCenterX, m_geo.dRotCenterY);
}

QPointF Stage::actualRotCenter() const
{
    QMutexLocker locker(&m_mutex);
    return QPointF(m_geoActual.dRotCenterX, m_geoActual.dRotCenterY);
}

QPointF Stage::mapFromHome(const QPointF& ptHome) const
{
    QMutexLocker locker(&m_mutex);
    // 20261025 ZJH 物体随平台的真实运动: 实际位姿绕实际旋转中心
    return applyPose(m_pose, QPointF(m_geoActual.dRotCenterX, m_geoActual.dRotCenterY), ptHome);
}

double Stage::mapAngleFromHome(double dAngleDeg) const
{
    QMutexLocker locker(&m_mutex);
    return dAngleDeg + m_pose.dThetaDeg;  // 角度随实际转角叠加
}

void Stage::updatePosesLocked()
{
    bool bOkCmd = true;  // 指令正解结果
    bool bOkAct = true;  // 实际正解结果
    const Pose poseCmd = m_pKin->axesToPose(m_axes, &bOkCmd);
    Pose poseAct = m_pKinActual->axesToPose(m_actualAxes, &bOkAct);
    // 20261025 ZJH XYD 垂直度: 实际平移 = X·ex + Y·ey', ey' = (−sin s, cos s)
    if (m_err.bEnabled && m_geo.eType == StageType::XYD && m_err.dSkewDeg != 0.0) {
        const double dS = m_err.dSkewDeg * s_dDeg2Rad;
        const double dY = poseAct.dY;
        poseAct.dX -= dY * std::sin(dS);
        poseAct.dY = dY * std::cos(dS);
    }
    m_bPoseValid = bOkCmd && bOkAct;
    // 20261024 ZJH 正解失败 (UVW 几何奇异) 时保留上一次位姿, 避免显示 NaN
    if (bOkCmd) {
        m_poseCmd = poseCmd;
    }
    if (bOkAct) {
        m_pose = poseAct;
    }
}
