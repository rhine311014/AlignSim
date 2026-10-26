// 20261024 ZJH StageConfigWidget —— 单个平台的配置与手动操作面板
// 配置: 轴型 (XYD/UVW)、旋转中心、UVW 三轴安装点与方向、Int32 单位倍率
// 操作: 三轴数值 绝对/相对 移动、位姿移动、回零; 实时显示轴值与位姿
#pragma once

#include "motion/StageKinematics.h"
#include <QGroupBox>

class Stage;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTableWidget;
class QLabel;
class QPushButton;
class QGroupBox;

class StageConfigWidget : public QGroupBox
{
    Q_OBJECT
public:
    StageConfigWidget(Stage* pStage, const QString& strTitle, QWidget* pParent = nullptr);

    // 20261024 ZJH 从 Stage 当前状态刷新所有配置控件
    void loadFromStage();

private slots:
    void onApplyGeometry();   // 应用几何配置
    void onTypeChanged();     // 轴型切换: 启用/禁用 UVW 表
    void onMoveAbs();         // 轴绝对移动
    void onMoveRel();         // 轴相对移动
    void onMovePose();        // 位姿绝对移动
    void onMovePoseRel();     // 20261026 ZJH 位姿相对移动 (对应吉嘉说明书 X'/Y'/θ' 步进)
    void refreshStatus();     // 刷新实时显示
    void onApplyError();      // 应用误差注入配置

private:
    Stage* m_pStage = nullptr;           // 平台 (不持有)
    QComboBox* m_pComboType = nullptr;   // 轴型
    QComboBox* m_pComboUvwPreset = nullptr; // 20261026 ZJH UVW 机构预设 (吉嘉 G+ 等)
    QDoubleSpinBox* m_pSpinRcX = nullptr;  // 旋转中心 X
    QDoubleSpinBox* m_pSpinRcY = nullptr;  // 旋转中心 Y
    QTableWidget* m_pTableUvw = nullptr;   // UVW 安装表 (3 行: X, Y, 方向)
    QSpinBox* m_pSpinLinPower = nullptr;   // 直线轴小数位
    QSpinBox* m_pSpinAngPower = nullptr;   // 角度轴小数位
    QLabel* m_pLabelGeoState = nullptr;    // 几何有效性提示
    QDoubleSpinBox* m_arrSpinAxis[3] = {}; // 三轴输入
    QLabel* m_arrLabelAxis[3] = {};        // 三轴名称
    QLabel* m_pLabelStatus = nullptr;      // 实时状态
    AxisValues m_lastAxes;                 // 20261026 ZJH 上一次轴值 (用于显示单步马达位移量 ΔU/ΔV/ΔW)

    // 20261025 ZJH 误差注入控件
    QGroupBox* m_pGrpError = nullptr;          // 可勾选分组 = 总开关
    QDoubleSpinBox* m_pSpinRepeatUm = nullptr; // 重复性 σ µm
    QDoubleSpinBox* m_pSpinRepeatDeg = nullptr;// 重复性 σ °
    QDoubleSpinBox* m_pSpinBacklashUm = nullptr;   // 反向间隙 µm
    QDoubleSpinBox* m_pSpinBacklashDeg = nullptr;  // 反向间隙 °
    QDoubleSpinBox* m_arrSpinScale[3] = {};    // 三轴比例误差 ppm
    QDoubleSpinBox* m_pSpinSkew = nullptr;     // 垂直度 °
    QDoubleSpinBox* m_pSpinRcErrX = nullptr;   // 旋转中心偏差 X mm
    QDoubleSpinBox* m_pSpinRcErrY = nullptr;   // 旋转中心偏差 Y mm
    QDoubleSpinBox* m_arrSpinUvwErr[6] = {};   // UVW 安装偏差 µm
    QDoubleSpinBox* m_pSpinSpeedMm = nullptr;  // 速度 mm/s
    QDoubleSpinBox* m_pSpinSpeedDeg = nullptr; // 速度 °/s
    QSpinBox* m_pSpinSettle = nullptr;         // 整定 ms
    QSpinBox* m_pSpinSeed = nullptr;           // 随机种子
};
