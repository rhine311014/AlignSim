// 20261024 ZJH AlignScene —— 对位模拟场景 (支持 单相机多Mark / 多相机多Mark)
//
// 组成:
//   相机视窗 AlignCamera: 世界坐标中的一块矩形区域 (中心 + 视野宽高), 界面上每个相机一个视图, 可弹出到独立窗口给真实相机拍屏
//   Mark 对 MarkPair     : 目标 Mark (固定不动) + 对象 Mark (放在对位平台上, 随平台运动), 归属某个相机
//   来料偏差 objectOffset : 产品放到平台上时的随机偏差, 绕对象 Mark 质心作用, 用来模拟每次来料位置不同
//
// 残差: 对象 Mark 相对目标 Mark 的偏差
//   单个 Mark: Δx/Δy 为位置差, Δθ 为 Mark 角度差
//   多个 Mark: 最小二乘刚体拟合 (对象点集 ↔ 目标点集), Δx/Δy 为质心差, Δθ 为拟合转角
#pragma once

#include "motion/StageKinematics.h"

#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QString>
#include <vector>

class Stage;

// 20261024 ZJH Mark 图形 (数值写入配置)
enum class MarkShape {
    Cross = 0,        // 十字
    Circle = 1,       // 实心圆
    LCorner = 2,      // L 形角
    Rect = 3,         // 实心矩形
    CrossCircle = 4,  // 圆环 + 十字
};

// 20261024 ZJH 相机视窗
struct AlignCamera {
    QString strName;        // 名称, 如 "相机1"
    double dCenterX = 0.0;  // 视野中心世界坐标 X (mm)
    double dCenterY = 0.0;  // 视野中心世界坐标 Y (mm)
    double dFovW = 40.0;    // 视野宽 (mm)
    double dFovH = 30.0;    // 视野高 (mm)
};

// 20261024 ZJH 一对 Mark
// 20261026 ZJH dSize/dLineWidth ≤ 0 表示跟随全局 MarkStyle, > 0 时单独指定该对 Mark 的大小/线宽
struct MarkPair {
    QString strName;            // 名称, 如 "Mark1"
    int nCamera = 0;            // 归属相机下标
    double dTargetX = 0.0;      // 目标 Mark 世界坐标 X
    double dTargetY = 0.0;      // 目标 Mark 世界坐标 Y
    double dTargetAngle = 0.0;  // 目标 Mark 角度 (度)
    double dObjectX = 0.0;      // 对象 Mark 在平台原点、无来料偏差时的世界坐标 X
    double dObjectY = 0.0;      // 对象 Mark 原点世界坐标 Y
    double dObjectAngle = 0.0;  // 对象 Mark 原点角度 (度)
    double dSize = 0.0;         // 单独尺寸 (mm, ≤0 用全局 Mark 外观尺寸)
    double dLineWidth = 0.0;    // 单独线宽 (mm, ≤0 用全局 Mark 外观线宽)
};

// 20261024 ZJH Mark 外观
struct MarkStyle {
    MarkShape eTargetShape = MarkShape::Cross;   // 目标 Mark 图形
    MarkShape eObjectShape = MarkShape::LCorner; // 对象 Mark 图形 (与目标不同, 方便视觉区分)
    double dSize = 4.0;                          // Mark 外形尺寸 (mm)
    double dLineWidth = 0.4;                     // 线宽 (mm)
};

// 20261025 ZJH 对位显示配置 (放在场景里而不是界面里: 标定记录需要按同一比例计算 Mark 的屏幕像素位置)
struct AlignDisplay {
    bool bFixedScale = false;       // 相机视图固定比例 (拍屏标定时必须开启, 否则窗口缩放会让标定失效)
    double dPxPerMm = 20.0;         // 固定比例: 屏幕像素/mm
    bool bYDown = false;            // 世界 Y 轴朝下显示
    bool bShowStage = true;         // 20261026 ZJH 是否显示平台 (台面外框、旋转中心 ⊕ 与 UVW 轴承支点)
    bool bShowTarget = true;        // 20261026 ZJH 是否显示目标 Mark (黑); 取消勾选可只看对象 Mark
    bool bShowObject = true;        // 20261026 ZJH 是否显示对象 Mark (蓝); 取消勾选可只看目标 Mark
    bool bShowCalibMark = false;    // 20261026 ZJH 是否显示对位平台专用标定 Mark (开启时在每个相机视野内绘制随平台运动的标定 Mark)
    int nCalibShape = 4;            // 20261026 ZJH 标定 Mark 图形 (MarkShape 数值, 默认圆环十字)
    double dCalibSize = 4.0;        // 20261026 ZJH 标定 Mark 尺寸 (mm)
    double dCalibLineWidth = 0.4;   // 20261026 ZJH 标定 Mark 线宽 (mm)
    double dCalibOffsetX = 10.0;    // 20261026 ZJH 标定 Mark 相对相机中心安装偏移 X (mm, 平台原点时; 非零偏移供单相机旋转标定求力臂)
    double dCalibOffsetY = 0.0;     // 20261026 ZJH 标定 Mark 相对相机中心安装偏移 Y (mm)
    int nNoiseLevel = 0;            // 20261026 ZJH 光学噪点/光照不均模拟强度 (0=理想无噪点, 1~100=噪点与渐变强度)
    int nBlurLevel = 0;             // 20261026 ZJH 镜头失焦模糊等级 (0=清晰, 1~10=边缘柔化模糊)
    bool bSimulateDefect = false;   // 20261026 ZJH 是否在 Mark 边缘模拟微小缺口/污渍 (测试视觉抗残缺鲁棒性)
};

// 20261024 ZJH 对位残差
struct AlignResidual {
    bool bValid = false;              // 是否有 Mark
    double dX = 0.0;                  // 整体 Δx (mm)
    double dY = 0.0;                  // 整体 Δy (mm)
    double dThetaDeg = 0.0;           // 整体 Δθ (度)
    std::vector<QPointF> vecMarkDelta;  // 每个 Mark 的位置差 (对象 − 目标)
};

// 20261024 ZJH 某个 Mark 在世界中的实时位姿
struct MarkWorldPose {
    QPointF ptPos;          // 世界坐标
    double dAngleDeg = 0.0; // 角度
};

class AlignScene : public QObject
{
    Q_OBJECT
public:
    explicit AlignScene(Stage* pStage, QObject* pParent = nullptr);

    Stage* stage() const { return m_pStage; }

    void setCameras(const std::vector<AlignCamera>& vecCameras);
    std::vector<AlignCamera> cameras() const;
    void setMarks(const std::vector<MarkPair>& vecMarks);
    std::vector<MarkPair> marks() const;
    void setStyle(const MarkStyle& style);
    MarkStyle style() const;

    void setDisplay(const AlignDisplay& display);
    AlignDisplay display() const;
    // 20261025 ZJH 第 i 个对象 Mark 相对其所属相机视窗中心的屏幕像素偏移
    // 返回 false: 未开启固定比例 (像素位置随窗口变化, 无意义) 或下标/相机越界
    bool objectMarkScreenOffset(int nIndex, QPointF& ptOffsetPx) const;

    // 20261024 ZJH 来料偏差 (绕对象 Mark 质心)
    void setObjectOffset(const Pose& offset);
    Pose objectOffset() const;
    // 20261024 ZJH 随机来料偏差: |Δx|,|Δy| ≤ dMaxXY, |Δθ| ≤ dMaxThetaDeg
    void randomizeObjectOffset(double dMaxXY, double dMaxThetaDeg);

    // 20261024 ZJH 第 i 个对象 Mark 的实时世界位姿 (来料偏差 + 平台运动)
    MarkWorldPose objectMarkWorld(int nIndex) const;
    // 20261026 ZJH 第 nCam 个相机的对位标定 Mark 实时世界位姿 (随对位平台运动)
    MarkWorldPose calibMarkWorld(int nCamIndex) const;
    // 20261026 ZJH 第 nCam 个相机的标定 Mark 相对该相机视野中心的屏幕像素偏移
    bool calibMarkScreenOffset(int nCamIndex, QPointF& ptOffsetPx) const;
    // 20261026 ZJH 移动对位平台使标定 Mark 正好位于各相机视野正中央, 并清零来料偏差
    void centerCalibMark();
    // 20261024 ZJH 当前残差
    AlignResidual residual() const;

    // 20261024 ZJH 预设: 单相机双 Mark / 双相机各一 Mark / 三相机品字形 / 四相机四角各一 Mark
    void applyPresetSingleCamera();
    void applyPresetDualCamera();
    void applyPresetTripleCamera();
    void applyPresetQuadCamera();

signals:
    // 20261024 ZJH 场景配置或来料偏差改变 (平台运动由 Stage::changed 通知)
    void changed();

private:
    QPointF objectCentroidLocked() const;  // 对象 Mark 原点质心 (调用方须持锁)

    Stage* m_pStage = nullptr;              // 对位平台 (不持有)
    mutable QMutex m_mutex;                 // 保护下列成员
    std::vector<AlignCamera> m_vecCameras;  // 相机视窗
    std::vector<MarkPair> m_vecMarks;       // Mark 对
    MarkStyle m_style;                      // Mark 外观
    Pose m_objectOffset;                    // 来料偏差
    AlignDisplay m_display;                 // 显示配置
};
