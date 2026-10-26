// 20261024 ZJH 平台运动学实现
#include "motion/StageKinematics.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double s_dPi = 3.14159265358979323846;
constexpr double s_dDeg2Rad = s_dPi / 180.0;  // 度 → 弧度
constexpr double s_dRad2Deg = 180.0 / s_dPi;  // 弧度 → 度
constexpr double s_dDetEps = 1e-9;            // 行列式判奇异阈值 (相对值)
constexpr int s_nMaxNewtonIter = 40;          // UVW 正解最大迭代次数 (含大角度阻尼回退)
constexpr double s_dNewtonTol = 1e-10;        // UVW 正解收敛阈值: 轴值残差 (mm)

// ============================================================
// XYD: 轴值即位姿
// ============================================================
class XydKinematics : public IStageKinematics
{
public:
    Pose axesToPose(const AxisValues& axes, bool* pOk) const override
    {
        // 20261024 ZJH 恒等映射, 总是成功
        if (pOk) {
            *pOk = true;
        }
        return Pose{ axes.dA1, axes.dA2, axes.dA3 };  // X Y D 直接作为位姿
    }
    AxisValues poseToAxes(const Pose& pose) const override
    {
        return AxisValues{ pose.dX, pose.dY, pose.dThetaDeg };  // 位姿直接作为轴值
    }
    QStringList axisNames() const override { return { QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("D") }; }
    bool isAngleAxis(int nIndex) const override { return nIndex == 2; }  // 第 3 轴 D 为角度
};

// ============================================================
// UVW: 三直线轴
// ============================================================
class UvwKinematics : public IStageKinematics
{
public:
    explicit UvwKinematics(const StageGeometry& geo)
    {
        // 20261024 ZJH 预计算每个轴脚相对旋转中心 (at, bt) 的力臂向量 (Ux - at, Uy - bt) 与驱动方向单位向量
        // 对应吉嘉 G+ 说明书 3-4 移动算法公式:
        //   Y1(U) = Y' + (Ux - at)*(sin(θ+θ') - sin(θ)) + (Uy - bt)*(cos(θ+θ') - cos(θ))   [dir = +90°]
        //   X (V) = -X' - (Vx - at)*(cos(θ+θ') - cos(θ)) + (Vy - bt)*(sin(θ+θ') - sin(θ))  [dir = 180°]
        //   Y2(W) = -Y' - (Wx - at)*(sin(θ+θ') - sin(θ)) - (Wy - bt)*(cos(θ+θ') - cos(θ))  [dir = -90°]
        for (int i = 0; i < 3; ++i) {
            m_arrPx[i] = geo.arrUvw[i].dMountX - geo.dRotCenterX;
            m_arrPy[i] = geo.arrUvw[i].dMountY - geo.dRotCenterY;
            m_arrDx[i] = std::cos(geo.arrUvw[i].dDirDeg * s_dDeg2Rad);
            m_arrDy[i] = std::sin(geo.arrUvw[i].dDirDeg * s_dDeg2Rad);
        }
    }

    AxisValues poseToAxes(const Pose& pose) const override
    {
        const double dTheta = pose.dThetaDeg * s_dDeg2Rad;  // 转角 (弧度)
        const double dCos = std::cos(dTheta);
        const double dSin = std::sin(dTheta);
        double arrS[3] = {};  // 三轴行程
        // 20261024 ZJH s_i = d_i · (R·P_i + t − P_i)
        for (int i = 0; i < 3; ++i) {
            const double dMoveX = dCos * m_arrPx[i] - dSin * m_arrPy[i] + pose.dX - m_arrPx[i];  // 安装点 X 位移
            const double dMoveY = dSin * m_arrPx[i] + dCos * m_arrPy[i] + pose.dY - m_arrPy[i];  // 安装点 Y 位移
            arrS[i] = m_arrDx[i] * dMoveX + m_arrDy[i] * dMoveY;  // 投影到轴方向
        }
        return AxisValues{ arrS[0], arrS[1], arrS[2] };  // 返回 U V W
    }

    Pose axesToPose(const AxisValues& axes, bool* pOk) const override
    {
        const double arrTarget[3] = { axes.dA1, axes.dA2, axes.dA3 };  // 目标轴值
        Pose pose;          // 迭代初值: 原点 (第一步即 θ=0 处的线性解, 纯平移时一步精确)
        bool bOk = false;   // 是否收敛
        // 20261024 ZJH 牛顿迭代 + 步长回退阻尼 (Armijo line-search):
        //   常规小角度 (<10°) 3~4 步即二次收敛到 1e-12; 大角度或强非线性时自动回退步长防止发散
        auto calcMaxRes = [&](const Pose& p) {
            const AxisValues a = poseToAxes(p);
            return std::max({ std::abs(arrTarget[0] - a.dA1), std::abs(arrTarget[1] - a.dA2), std::abs(arrTarget[2] - a.dA3) });
        };
        double dCurRes = calcMaxRes(pose);
        if (dCurRes < s_dNewtonTol) {
            if (pOk) *pOk = true;
            return pose;
        }
        for (int nIter = 0; nIter < s_nMaxNewtonIter; ++nIter) {
            Pose poseStep = pose;
            if (!solveNewtonStep(poseStep, arrTarget)) {
                break;
            }
            const double dDx = poseStep.dX - pose.dX;
            const double dDy = poseStep.dY - pose.dY;
            // 20261026 ZJH 单步转角变化限制在 30° 内, 防止极端初值跳过分支
            const double dDTheta = std::clamp(poseStep.dThetaDeg - pose.dThetaDeg, -30.0, 30.0);
            double dAlpha = 1.0;
            Pose poseCand = Pose{ pose.dX + dDx, pose.dY + dDy, pose.dThetaDeg + dDTheta };
            double dCandRes = calcMaxRes(poseCand);
            while (dCandRes > dCurRes && dAlpha > 1.0 / 64.0) {
                dAlpha *= 0.5;
                poseCand = Pose{ pose.dX + dAlpha * dDx, pose.dY + dAlpha * dDy, pose.dThetaDeg + dAlpha * dDTheta };
                dCandRes = calcMaxRes(poseCand);
            }
            pose = poseCand;
            dCurRes = dCandRes;
            if (dCurRes < s_dNewtonTol) {
                bOk = true;
                break;
            }
        }
        // 20261024 ZJH 未收敛 (超出可达范围/发散) 时 bOk=false, Stage 会保留上一次位姿并提示"正解失败"

        // 20261024 ZJH 输出求解状态
        if (pOk) {
            *pOk = bOk;
        }
        return pose;  // 返回位姿
    }

    QStringList axisNames() const override { return { QStringLiteral("U"), QStringLiteral("V"), QStringLiteral("W") }; }
    bool isAngleAxis(int) const override { return false; }  // UVW 三轴都是直线轴

    // 20261024 ZJH 在当前 pose 处做一次牛顿步: J·Δ = s_target − f(pose), 原地更新 pose
    // 雅可比行 i: [ d_ix, d_iy, d_i · (dR/dθ · P_i) ]   (θ 用弧度)
    // 返回: false = 雅可比奇异 (几何无效)
    bool solveNewtonStep(Pose& pose, const double arrTarget[3]) const
    {
        const double dTheta = pose.dThetaDeg * s_dDeg2Rad;
        const double dCos = std::cos(dTheta);
        const double dSin = std::sin(dTheta);
        const AxisValues axesNow = poseToAxes(pose);  // 当前位姿对应轴值
        const double arrResidual[3] = { arrTarget[0] - axesNow.dA1, arrTarget[1] - axesNow.dA2,
                                        arrTarget[2] - axesNow.dA3 };  // 轴值残差
        double arrJ[3][3] = {};  // 雅可比矩阵
        for (int i = 0; i < 3; ++i) {  // 逐行填雅可比
            arrJ[i][0] = m_arrDx[i];
            arrJ[i][1] = m_arrDy[i];
            const double dDRx = -dSin * m_arrPx[i] - dCos * m_arrPy[i];  // dR/dθ·P 的 X 分量
            const double dDRy = dCos * m_arrPx[i] - dSin * m_arrPy[i];   // dR/dθ·P 的 Y 分量
            arrJ[i][2] = m_arrDx[i] * dDRx + m_arrDy[i] * dDRy;
        }
        double arrDelta[3] = {};  // 位姿增量 (dx, dy, dθ弧度)
        // 20261024 ZJH 雅可比奇异 → 失败
        if (!solve3x3(arrJ, arrResidual, arrDelta)) {
            return false;
        }
        pose.dX += arrDelta[0];
        pose.dY += arrDelta[1];
        pose.dThetaDeg += arrDelta[2] * s_dRad2Deg;
        return true;  // 返回成功
    }

    // 20261024 ZJH 克莱姆法则解 3x3 线性方程组 A·x = b; 奇异返回 false
    static bool solve3x3(const double a[3][3], const double b[3], double x[3])
    {
        auto det3 = [](const double m[3][3]) {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                 - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        };
        const double dDet = det3(a);  // 系数行列式
        // 20261024 ZJH 用矩阵量级做相对阈值, 避免单位 (mm/弧度) 不同导致误判
        double dScale = 0.0;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                dScale = std::max(dScale, std::abs(a[r][c]));
            }
        }
        if (dScale <= 0.0 || std::abs(dDet) < s_dDetEps * dScale * dScale * dScale) {
            return false;  // 奇异
        }
        for (int nCol = 0; nCol < 3; ++nCol) {  // 依次替换第 nCol 列
            double m[3][3];
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    m[r][c] = (c == nCol) ? b[r] : a[r][c];
                }
            }
            x[nCol] = det3(m) / dDet;
        }
        return true;  // 返回成功
    }

private:
    double m_arrPx[3] = {};  // 安装点 X (相对旋转中心)
    double m_arrPy[3] = {};  // 安装点 Y
    double m_arrDx[3] = {};  // 方向单位向量 X
    double m_arrDy[3] = {};  // 方向单位向量 Y
};

}  // namespace

std::array<UvwAxisMount, 3> makeUvwPreset(UvwPreset ePreset)
{
    switch (ePreset) {
    case UvwPreset::GPlus_Manual_25_456:
        // 20261026 ZJH 吉嘉 G+ 说明书 3-4 移动算法计算示例 (R=36mm, 45° 对角布局)
        return { UvwAxisMount{ 25.456, 25.456, 90.0 },
                 UvwAxisMount{ -25.456, 25.456, 180.0 },
                 UvwAxisMount{ -25.456, -25.456, -90.0 } };
    case UvwPreset::X1X2Y_Diag100:
        // 经典 X1-X2-Y 对角结构
        return { UvwAxisMount{ -100.0, 100.0, 0.0 },
                 UvwAxisMount{ 100.0, -100.0, 0.0 },
                 UvwAxisMount{ 100.0, 100.0, 90.0 } };
    case UvwPreset::XY1Y2_R85:
        // 经典 X-Y1-Y2 结构
        return { UvwAxisMount{ 0.0, -85.0, 0.0 },
                 UvwAxisMount{ -85.0, 0.0, 90.0 },
                 UvwAxisMount{ 85.0, 0.0, 90.0 } };
    case UvwPreset::GPlus_XXY250_R85:
    default:
        // 20261026 ZJH 吉嘉 G+ XXY250-90-250-SV-X 规格表 (R=85mm)
        return { UvwAxisMount{ 85.0, 0.0, 90.0 },
                 UvwAxisMount{ 0.0, 85.0, 180.0 },
                 UvwAxisMount{ -85.0, 0.0, -90.0 } };
    }
}

std::unique_ptr<IStageKinematics> createKinematics(const StageGeometry& geo)
{
    // 20261024 ZJH 按平台类型创建
    if (geo.eType == StageType::UVW) {
        return std::make_unique<UvwKinematics>(geo);
    }
    return std::make_unique<XydKinematics>();
}

bool isUvwGeometryValid(const StageGeometry& geo)
{
    UvwKinematics kin(geo);  // 临时实例
    Pose pose;               // 原点
    const double arrTarget[3] = { 0.0, 0.0, 0.0 };
    return kin.solveNewtonStep(pose, arrTarget);  // 原点处雅可比可逆即有效
}

QPointF applyPose(const Pose& pose, const QPointF& ptRotCenter, const QPointF& ptHome)
{
    const double dTheta = pose.dThetaDeg * s_dDeg2Rad;  // 弧度
    const double dRelX = ptHome.x() - ptRotCenter.x();  // 相对旋转中心
    const double dRelY = ptHome.y() - ptRotCenter.y();
    // 20261024 ZJH p' = R(θ)(p − c) + c + t
    return QPointF(std::cos(dTheta) * dRelX - std::sin(dTheta) * dRelY + ptRotCenter.x() + pose.dX,
                   std::sin(dTheta) * dRelX + std::cos(dTheta) * dRelY + ptRotCenter.y() + pose.dY);
}

QPointF screenOffsetPx(const QPointF& ptWorld, const QPointF& ptViewCenter, double dPxPerMm, bool bYDown)
{
    const double dDx = (ptWorld.x() - ptViewCenter.x()) * dPxPerMm;  // 屏幕 X 与世界 X 同向
    const double dDy = (ptWorld.y() - ptViewCenter.y()) * dPxPerMm;
    // 20261025 ZJH 屏幕 Y 向下: 世界 Y 朝上显示时取反
    return QPointF(dDx, bYDown ? dDy : -dDy);
}

double normalizeDeg(double dDeg)
{
    double dOut = std::fmod(dDeg, 360.0);  // 先落到 (-360, 360)
    // 20261024 ZJH 收敛到 (-180, 180]
    if (dOut <= -180.0) {
        dOut += 360.0;
    } else if (dOut > 180.0) {
        dOut -= 360.0;
    }
    return dOut;  // 返回归一化角度
}
