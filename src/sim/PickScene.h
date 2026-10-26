// 20261024 ZJH PickScene —— 大视野拾取模拟场景
//
// 世界坐标: 视野区域 [0, W] × [0, H] (mm), X 向右, Y 向上, 角度逆时针为正
// 产品: 若干个, 位姿随机或手动设定; 每个产品带一个方向缺口/圆点, 使角度可被视觉识别
// 拾取头: 一个 Stage (XYD 或 UVW), 吸嘴位于 Stage 旋转中心; 吸嘴世界坐标 = 旋转中心 + 平移
// 拾取: 吸嘴落在哪个产品轮廓内 (或距中心 ≤ 容差) 就吸起它, 记录拾取误差 = 吸嘴位姿 − 产品真实位姿
//       吸起后产品随拾取头运动, PlaceAt 放下
#pragma once

#include "motion/StageKinematics.h"

#include <QMutex>
#include <QObject>
#include <QPointF>
#include <vector>

class Stage;

// 20261024 ZJH 产品外形 (数值写入配置)
enum class ProductShape {
    Rect = 0,    // 矩形
    Circle = 1,  // 圆形 (直径取 dProductW)
    LShape = 2,  // L 形
};

// 20261024 ZJH 场景配置
struct PickSceneConfig {
    double dFieldW = 600.0;             // 视野宽 (mm)
    double dFieldH = 400.0;             // 视野高 (mm)
    ProductShape eShape = ProductShape::Rect;
    double dProductW = 60.0;            // 产品宽 (mm)
    double dProductH = 40.0;            // 产品高 (mm)
    int nCount = 4;                     // 产品数量
    double dMaxAngleDeg = 180.0;        // 随机摆放角度范围 ±
    double dPickToleranceMm = 5.0;      // 拾取成功判据: 吸嘴到产品中心距离 ≤ 容差
    double dPickToleranceDeg = 2.0;     // 拾取成功判据: 角度误差 ≤ 容差
    QString strMarkText = QStringLiteral("TOP");  // 20261026 ZJH 产品正面丝印字符 (反面看不到字符, 空则不画字符)
    double dMarkTextHeightMm = 3.5;     // 20261026 ZJH 丝印字符高度 (mm, 默认 3.5mm 小字标识)
    bool bRandomFlip = true;            // 20261026 ZJH 随机摆放时是否随机生成反面产品 (反面看不到字符)
    bool bDualGripper = false;          // 20261026 ZJH 是否双爪模式 (false=单爪在旋转中心, true=同模组 1号爪+2号爪先后抓)
    double dNozzle1OffX = -30.0;        // 20261026 ZJH 双爪模式下 1号爪相对旋转中心偏移 X (mm, 拾取头坐标系)
    double dNozzle1OffY = 0.0;          // 1号爪偏移 Y (mm)
    double dNozzle2OffX = 30.0;         // 20261026 ZJH 双爪模式下 2号爪相对旋转中心偏移 X (mm, 拾取头坐标系)
    double dNozzle2OffY = 0.0;          // 2号爪偏移 Y (mm)
};

// 20261025 ZJH 拾取显示配置 (含拾取头标定 Mark)
// 标定 Mark 安装在拾取头上, 相对吸嘴偏移 (dCalibOffsetX/Y, 拾取头坐标系);
// 偏移不为 0 时, 视觉做旋转标定才能求出旋转中心 (Mark 在旋转轴上转动不会移动)
struct PickDisplay {
    bool bFixedScale = false;     // 固定比例 (拍屏标定必须开启)
    double dPxPerMm = 1.5;        // 固定比例: 屏幕像素/mm
    bool bYDown = false;          // 世界 Y 轴朝下显示
    bool bShowStage = true;       // 20261026 ZJH 是否显示拾取头(平台/吸嘴十字与双爪横梁)
    bool bShowCalibMark = false;  // 显示拾取头标定 Mark
    bool bShowProducts = true;    // 显示产品 (标定时隐藏, 避免干扰视觉)
    int nCalibShape = 4;          // 标定 Mark 图形 (MarkShape 数值, 默认圆环十字)
    double dCalibSize = 12.0;     // 标定 Mark 尺寸 (mm)
    double dCalibLineWidth = 1.2; // 标定 Mark 线宽 (mm)
    int nCalibMount = 0;          // 20261026 ZJH 标定 Mark 挂载基准: 0=旋转中心, 1=1号爪, 2=2号爪, 3=双爪(1+2同时显示)
    double dCalibOffsetX = 20.0;  // 标定 Mark 相对挂载基准偏移 X (mm, 拾取头坐标系)
    double dCalibOffsetY = 0.0;   // 标定 Mark 相对挂载基准偏移 Y
    int nNoiseLevel = 0;          // 20261026 ZJH 光学噪点/光照不均模拟强度 (0~100)
    int nBlurLevel = 0;           // 20261026 ZJH 镜头失焦模糊等级 (0~10)
    bool bSimulateDefect = false; // 20261026 ZJH 是否在产品/标定 Mark 上模拟微小边缘缺口/污渍
};

// 20261024 ZJH 一个产品
struct Product {
    int nId = 0;            // 编号 (从 1 开始)
    Pose pose;              // 世界位姿 (中心 + 角度); 被吸起时随拾取头更新
    bool bFlipped = false;  // 20261026 ZJH 是否反面朝上 (反面看不到丝印字符, 供视觉测试正反检测)
    bool bPicked = false;   // 是否被吸在拾取头上
    int nPickedByNozzle = 0;// 20261026 ZJH 被哪个爪吸着 (0=未吸, 1=1号爪, 2=2号爪)
    bool bPlaced = false;   // 是否已被放下过 (统计用)
};

// 20261024 ZJH 一次拾取结果
struct PickResult {
    bool bHit = false;          // 是否吸到产品
    bool bInTolerance = false;  // 误差是否在容差内 (反面产品拾取判为不合格)
    bool bFlipped = false;      // 20261026 ZJH 吸到的产品是否为反面
    int nNozzle = 1;            // 20261026 ZJH 本次动作的爪号 (1 或 2)
    int nProductId = 0;         // 吸到的产品编号
    double dX = 0.0;            // Δx = 吸嘴 − 产品 (mm)
    double dY = 0.0;            // Δy (mm)
    double dDist = 0.0;         // 距离误差 (mm)
    double dThetaDeg = 0.0;     // Δθ (度)
};

// 20261024 ZJH 拾取统计
struct PickStats {
    int nAttempts = 0;          // 拾取次数
    int nHits = 0;              // 吸到产品次数
    int nInTolerance = 0;       // 误差在容差内且正面次数
    int nFlippedPicked = 0;     // 20261026 ZJH 误吸反面产品次数
    double dMaxDist = 0.0;      // 最大距离误差
    double dSumDist = 0.0;      // 距离误差累计 (算平均)
    double dMaxAbsTheta = 0.0;  // 最大角度误差绝对值
    double dSumAbsTheta = 0.0;  // 角度误差绝对值累计
};

class PickScene : public QObject
{
    Q_OBJECT
public:
    explicit PickScene(Stage* pHead, QObject* pParent = nullptr);

    Stage* head() const { return m_pHead; }

    void setConfig(const PickSceneConfig& cfg);
    PickSceneConfig config() const;

    // 20261024 ZJH 随机摆放 nCount 个产品 (nCount ≤ 0 时用配置数量); nSeed ≥ 0 时可复现
    void spawnProducts(int nCount = -1, int nSeed = -1);
    // 20261026 ZJH 模拟来料传送带步进: 视野内待拾取产品整体沿 +X 平移 dStepX (mm), 移出视野的产品清除并在左侧补入新来料
    void stepConveyor(double dStepX = 140.0, int nNewCount = 2);
    // 20261024 ZJH 手动设置产品位姿 (nIndex 从 0 开始); 越界返回 false
    bool setProductPose(int nIndex, const Pose& pose);
    // 20261026 ZJH 手动设置产品正反面 (nIndex 从 0 开始); 越界或正被吸着返回 false
    bool setProductFlipped(int nIndex, bool bFlipped);
    std::vector<Product> products() const;  // 实时产品列表 (被吸起的产品位姿已更新)

    // 20261024 ZJH 吸嘴实时世界位姿 (nNozzle: 0=旋转中心基准, 1=1号爪, 2=2号爪; 单爪模式下 1号爪即在旋转中心)
    Pose nozzlePose(int nNozzle = 1) const;
    // 20261026 ZJH 当前有效爪数量 (单爪=1, 双爪=2) 及第 nNozzle (1或2) 相对旋转中心的局部偏移 (mm)
    int nozzleCount() const;
    QPointF nozzleLocalOffset(int nNozzle) const;
    // 20261026 ZJH 计算要让第 nNozzle (1或2) 对准世界目标位姿 targetPose 时, 拾取头应移动到的指令位姿
    Pose headPoseForNozzleTarget(int nNozzle, const Pose& targetPose) const;

    void setDisplay(const PickDisplay& display);
    PickDisplay display() const;
    // 20261025 ZJH 标定 Mark 实时世界位姿 (nOverrideMount < 0 用 display().nCalibMount; 1/2 指定爪)
    Pose calibMarkPose(int nOverrideMount = -1) const;
    // 20261026 ZJH 移动拾取头使标定 Mark 正好落在视野正中央 (W/2, H/2), θ=0
    void moveCalibMarkToFieldCenter();
    // 20261025 ZJH 标定 Mark 相对视野中心的屏幕像素偏移; 未开启固定比例返回 false
    bool calibMarkScreenOffset(QPointF& ptOffsetPx) const;

    // 20261024 ZJH 用第 nNozzle (1或2, 默认1) 在当前位置拾取; 该爪已吸着产品时返回 bHit=false
    PickResult pick(int nNozzle = 1);
    // 20261024 ZJH 放下吸着的产品 (nNozzle ≤ 0 表示双爪全部放下; 1 或 2 表示只放下对应爪)
    bool place(int nNozzle = 0);
    // 20261024 ZJH 当前是否吸着产品 (nNozzle ≤ 0 表示任一爪吸着即返回 true; 1 或 2 查询指定爪)
    bool isHolding(int nNozzle = 0) const;

    PickResult lastResult() const;
    PickStats stats() const;
    void resetStats();

signals:
    // 20261024 ZJH 产品/统计改变 (拾取头运动由 Stage::changed 通知)
    void changed();

private:
    Pose headCenterPoseFromStage() const;           // 拾取头旋转中心实时世界位姿 (不持本锁)
    QPointF nozzleOffsetLocked(int nNozzle) const;  // 第 nNozzle (1或2) 局部偏移 (须持锁)
    // 20261024 ZJH 点是否在产品轮廓内 (产品局部坐标判断)
    bool isInsideProduct(const Product& prod, const QPointF& ptWorld) const;

    Stage* m_pHead = nullptr;          // 拾取头平台 (不持有)
    mutable QMutex m_mutex;            // 保护下列成员
    PickSceneConfig m_cfg;             // 配置
    std::vector<Product> m_vecProducts;  // 产品
    int m_arrHeldIndex[2] = { -1, -1 };  // 20261026 ZJH 1号爪/2号爪各自吸着的产品下标, -1 表示空
    Pose m_arrHeldRelative[2];         // 被吸产品相对所属吸嘴的位姿 (该吸嘴局部坐标系)
    PickResult m_lastResult;           // 最近一次拾取结果
    PickStats m_stats;                 // 统计
    PickDisplay m_display;             // 显示配置
};
