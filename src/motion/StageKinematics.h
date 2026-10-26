// 20261024 ZJH 平台运动学: 轴值 (AxisValues) ↔ 平台位姿 (Pose)
//
// 位姿 Pose 的含义: 平台相对"原点状态"的刚体位移 —— 旋转中心平移 (dX, dY) + 绕旋转中心转 dThetaDeg
//   世界坐标中原点状态下的点 p, 运动后变为  p' = R(θ)·(p − c) + c + t   (c = 旋转中心, t = (dX, dY))
//
// XYD 平台: 三个轴就是 X / Y / D(角度), 轴值与位姿一一对应
// UVW 平台: 三个直线轴, 每个轴有安装点 P_i (相对旋转中心) 和运动方向 d_i (单位向量)
//   逆解 (位姿 → 轴值, 闭式):  s_i = d_i · ( R(θ)·P_i + t − P_i )
//   正解 (轴值 → 位姿, 迭代):  解非线性方程组 f(pose) = s, 用牛顿法
#pragma once

#include <QPointF>
#include <QStringList>
#include <array>
#include <memory>

// 20261024 ZJH 平台位姿 (单位: mm, 度)
struct Pose {
    double dX = 0.0;         // 旋转中心 X 平移
    double dY = 0.0;         // 旋转中心 Y 平移
    double dThetaDeg = 0.0;  // 绕旋转中心的转角 (度, 逆时针为正)
};

// 20261024 ZJH 三个轴的值; XYD 时依次为 X(mm) Y(mm) D(度); UVW 时依次为 U V W (mm)
struct AxisValues {
    double dA1 = 0.0;
    double dA2 = 0.0;
    double dA3 = 0.0;
};

// 20261024 ZJH 平台类型 (数值写入配置文件)
enum class StageType {
    XYD = 0,  // X + Y + 旋转
    UVW = 1,  // 三直线轴 (UVW 对位平台)
};

// 20261024 ZJH UVW 单轴安装参数
struct UvwAxisMount {
    double dMountX = 0.0;  // 安装点相对旋转中心 X (mm)
    double dMountY = 0.0;  // 安装点相对旋转中心 Y (mm)
    double dDirDeg = 0.0;  // 轴运动正方向与世界 X 轴夹角 (度): 0 = 沿 X, 90 = 沿 Y
};

// 20261024 ZJH 平台几何参数
struct StageGeometry {
    StageType eType = StageType::XYD;
    double dRotCenterX = 0.0;  // 原点状态下旋转中心的世界坐标 X (mm), 对应说明书 (at, bt)
    double dRotCenterY = 0.0;  // 原点状态下旋转中心的世界坐标 Y (mm)
    // 20261026 ZJH 默认采用吉嘉精密 G+ XXY250-90-250-SV-X 真实对位平台参数 (R=85mm):
    //   U 轴: 脚坐标 (85, 0), 驱动方向 +90° (+Y1)
    //   V 轴: 脚坐标 (0, 85), 驱动方向 180° (-X)
    //   W 轴: 脚坐标 (-85, 0), 驱动方向 -90° (-Y2)
    std::array<UvwAxisMount, 3> arrUvw = { UvwAxisMount{ 85.0, 0.0, 90.0 },
                                           UvwAxisMount{ 0.0, 85.0, 180.0 },
                                           UvwAxisMount{ -85.0, 0.0, -90.0 } };
};

// 20261026 ZJH 常用 UVW 机构几何预设
enum class UvwPreset {
    GPlus_XXY250_R85 = 0,     // 吉嘉 G+ XXY250-90-250-SV-X 规格表 (R=85mm: U(85,0,90°), V(0,85,180°), W(-85,0,-90°))
    GPlus_Manual_25_456 = 1,  // 吉嘉 G+ 说明书 3-4 移动算法示例 (U(25.456,25.456,90°), V(-25.456,25.456,180°), W(-25.456,-25.456,-90°))
    X1X2Y_Diag100 = 2,        // 经典 X1-X2-Y 对角布局 (U(-100,100,0°), V(100,-100,0°), W(100,100,90°))
    XY1Y2_R85 = 3,            // 经典 X-Y1-Y2 布局 (U(0,-85,0°), V(-85,0,90°), W(85,0,90°))
};

// 20261026 ZJH 生成指定预设的 UVW 三轴安装参数
std::array<UvwAxisMount, 3> makeUvwPreset(UvwPreset ePreset);

class IStageKinematics
{
public:
    virtual ~IStageKinematics() = default;
    // 20261024 ZJH 正解: 轴值 → 位姿; pOk 输出是否求解成功 (UVW 奇异/不收敛时为 false)
    virtual Pose axesToPose(const AxisValues& axes, bool* pOk = nullptr) const = 0;
    // 20261024 ZJH 逆解: 位姿 → 轴值
    virtual AxisValues poseToAxes(const Pose& pose) const = 0;
    // 20261024 ZJH 三个轴的显示名
    virtual QStringList axisNames() const = 0;
    // 20261024 ZJH 第 i 个轴是否为角度轴 (决定 Int32 单位换算用 XY 还是 D 倍率)
    virtual bool isAngleAxis(int nIndex) const = 0;
};

// 20261024 ZJH 按几何参数创建运动学实例
std::unique_ptr<IStageKinematics> createKinematics(const StageGeometry& geo);

// 20261024 ZJH UVW 几何是否可解 (三轴雅可比在原点处非奇异)
bool isUvwGeometryValid(const StageGeometry& geo);

// 20261024 ZJH 把原点状态下的世界点 ptHome 按位姿变换到运动后的世界坐标
QPointF applyPose(const Pose& pose, const QPointF& ptRotCenter, const QPointF& ptHome);

// 20261024 ZJH 角度归一化到 (-180, 180]
double normalizeDeg(double dDeg);

// 20261025 ZJH 固定比例显示下, 世界点相对视窗中心的屏幕像素偏移 (屏幕 X 向右, Y 向下)
// 参数: ptWorld - 世界点; ptViewCenter - 视窗中心世界坐标; dPxPerMm - 比例; bYDown - 世界 Y 轴是否朝下显示
QPointF screenOffsetPx(const QPointF& ptWorld, const QPointF& ptViewCenter, double dPxPerMm, bool bYDown);
