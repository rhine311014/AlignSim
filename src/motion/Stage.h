// 20261024 ZJH Stage —— 一个可运动的平台 (对位平台 / 拾取头各一个实例)
// 以"轴值"为状态 (与真实 PLC 一致: PLC 记录的是电机位置), 位姿由运动学正解得到
// 线程安全: Lua 脚本线程调用 move*, 界面线程读取位姿绘图
//
// 20261025 ZJH 误差注入: 维护两套位置
//   指令 (commanded): axes() / commandedPose() —— PLC 认为的位置, 回写视觉用; 按标称几何计算
//   实际 (actual)   : actualAxes() / pose()    —— 叠加误差模型后的真实位置, Mark/产品/残差用; 按实际几何计算
//   误差模型关闭时两者完全一致
#pragma once

#include "motion/StageErrorModel.h"
#include "motion/StageKinematics.h"

#include <QMutex>
#include <QObject>
#include <memory>
#include <random>

class Stage : public QObject
{
    Q_OBJECT
public:
    explicit Stage(const QString& strName, QObject* pParent = nullptr);

    QString name() const { return m_strName; }

    // 20261024 ZJH 几何参数 (切换 XYD/UVW、旋转中心、UVW 安装); 修改后轴值回零, 误差模型保留
    void setGeometry(const StageGeometry& geo);
    StageGeometry geometry() const;

    // 20261025 ZJH 误差模型; 修改后按当前指令位置重算实际位置 (随机误差重新抽样, 间隙状态清零)
    void setErrorModel(const StageErrorModel& err);
    StageErrorModel errorModel() const;

    // 20261024 ZJH Int32 寄存器单位: 实际值 = 寄存器值 / 10^n (直线轴用 nLinearPower, 角度轴用 nAnglePower)
    void setUnitPowers(int nLinearPower, int nAnglePower);
    int linearPower() const;
    int anglePower() const;
    // 20261024 ZJH 第 i 个轴的 Int32 换算除数
    double int32Divisor(int nIndex) const;
    // 20261025 ZJH 第 i 个轴是否角度轴
    bool isAngleAxis(int nIndex) const;

    // 20261024 ZJH 轴空间运动 (XYD: X/Y/D; UVW: U/V/W), 参数为指令值
    void moveAxesAbs(const AxisValues& axes);
    void moveAxesRel(const AxisValues& delta);
    // 20261024 ZJH 位姿空间运动 (按标称运动学逆解成指令轴值, 与 PLC 的做法一致)
    void movePoseAbs(const Pose& pose);
    void movePoseRel(const Pose& delta);
    // 20261024 ZJH 回原点 (指令轴值全 0)
    void home();
    // 20261025 ZJH 位姿 → 指令轴值 (标称运动学), 供脚本插补运动用
    AxisValues axesForPose(const Pose& pose) const;

    AxisValues axes() const;          // 指令轴值
    AxisValues actualAxes() const;    // 实际轴值 (含比例/间隙/随机误差)
    Pose commandedPose() const;       // 指令位姿 (标称几何正解)
    Pose pose() const;                // 实际位姿 (实际几何正解, 真值)
    bool isPoseValid() const;         // 最近一次正解是否成功
    QStringList axisNames() const;
    QPointF rotCenter() const;        // 标称旋转中心 (原点状态世界坐标)
    QPointF actualRotCenter() const;  // 实际旋转中心 (原点状态世界坐标) = 标称 + 偏差

    // 20261024 ZJH 把原点状态下的世界点/角度按实际运动变换到当前位置 (平台携带物体用)
    QPointF mapFromHome(const QPointF& ptHome) const;
    double mapAngleFromHome(double dAngleDeg) const;

signals:
    // 20261024 ZJH 轴值、几何或误差模型改变 (可能来自脚本线程, 界面用排队连接)
    void changed();

private:
    void rebuildActualLocked();               // 由标称几何 + 误差模型重建实际运动学 (调用方须持锁)
    void applyMoveLocked(const AxisValues& axesNew);  // 设新指令轴值, 更新间隙状态/随机误差/实际位置 (须持锁)
    void updatePosesLocked();                 // 由指令/实际轴值刷新两套位姿缓存 (须持锁)

    QString m_strName;                               // 平台名称 (align / pick)
    mutable QMutex m_mutex;                          // 保护下列成员
    StageGeometry m_geo;                             // 标称几何
    StageGeometry m_geoActual;                       // 实际几何 (旋转中心/安装点含偏差)
    StageErrorModel m_err;                           // 误差模型
    std::unique_ptr<IStageKinematics> m_pKin;        // 标称运动学
    std::unique_ptr<IStageKinematics> m_pKinActual;  // 实际运动学
    AxisValues m_axes;                               // 指令轴值
    AxisValues m_actualAxes;                         // 实际轴值
    double m_arrBacklash[3] = {};                    // 每轴当前间隙偏移 (mm 或 °)
    double m_arrNoise[3] = {};                       // 每轴当前随机误差 (本次移动抽样)
    Pose m_poseCmd;                                  // 指令位姿缓存
    Pose m_pose;                                     // 实际位姿缓存
    bool m_bPoseValid = true;                        // 正解是否成功
    std::mt19937 m_rng;                              // 随机数发生器 (按种子初始化)
    int m_nLinearPower = 3;                          // 直线轴 Int32 小数位 (默认 0.001mm)
    int m_nAnglePower = 3;                           // 角度轴 Int32 小数位 (默认 0.001°)
};
