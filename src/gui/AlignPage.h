// 20261024 ZJH AlignPage —— 对位模拟页面
// 左: 全局视图 + 每个相机一个视图 (网格排列, 相机数量变化时重建)
// 右: 预设 / 相机表 / Mark 表 / 外观 / 来料偏差 / 残差 / 弹出相机窗口
#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class AlignScene;
class AlignView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QTableWidget;
class QTimer;

class AlignPage : public QWidget
{
    Q_OBJECT
public:
    explicit AlignPage(AlignScene* pScene, QWidget* pParent = nullptr);

    void setYDown(bool bYDown);       // 所有视图 Y 轴方向 (写入场景显示配置)
    void loadFromScene();             // 场景 → 表格/控件

protected:
    void showEvent(QShowEvent* pEvent) override;  // 20261026 ZJH 切回本页签时补刷新残差

private slots:
    void onApplyTables();             // 表格 → 场景
    void onStyleChanged();            // 外观控件 → 场景
    void onSetOffset();               // 设置来料偏差
    void onRandomOffset();            // 随机来料偏差
    void onPopoutCamera();            // 弹出选中相机窗口
    void onPopoutGlobal();            // 弹出全局视图窗口
    void refreshResidual();           // 刷新残差显示与标定真值
    void onDisplayChanged();          // 固定比例控件 → 场景显示配置
    void rebuildCameraViews();        // 按相机数量重建视图网格

private:
    AlignView* createView(int nCameraIndex, QWidget* pParent);  // 创建视图并登记 (统一 Y 轴设置)

    AlignScene* m_pScene = nullptr;            // 场景 (不持有)
    AlignView* m_pMainView = nullptr;          // 20261026 ZJH 左侧单画面主视图
    QComboBox* m_pComboViewMode = nullptr;     // 20261026 ZJH 主视图显示模式 (0=全局视图, >=1=对应相机纯净画面)
    QList<QPointer<AlignView>> m_lstViews;     // 所有视图 (含弹出窗口里的)
    QCheckBox* m_pChkFixed = nullptr;          // 固定比例
    QDoubleSpinBox* m_pSpinPxPerMm = nullptr;  // 固定比例 px/mm
    QCheckBox* m_pChkShowStage = nullptr;      // 20261026 ZJH 显示对位平台 (台面外框、旋转中心与 UVW 支点)
    QCheckBox* m_pChkShowTarget = nullptr;     // 20261026 ZJH 显示目标 Mark (黑)
    QCheckBox* m_pChkShowObject = nullptr;     // 20261026 ZJH 显示对象 Mark (蓝)
    QCheckBox* m_pChkCalibMark = nullptr;      // 20261026 ZJH 显示对位平台专用标定 Mark (紫红)
    QComboBox* m_pComboCalibShape = nullptr;   // 20261026 ZJH 标定 Mark 图形
    QDoubleSpinBox* m_pSpinCalibSize = nullptr;// 20261026 ZJH 标定 Mark 尺寸
    QDoubleSpinBox* m_pSpinCalibLw = nullptr;  // 20261026 ZJH 标定 Mark 线宽
    QDoubleSpinBox* m_pSpinCalibOffX = nullptr;// 20261026 ZJH 标定 Mark 偏移 X
    QDoubleSpinBox* m_pSpinCalibOffY = nullptr;// 20261026 ZJH 标定 Mark 偏移 Y
    QSpinBox* m_pSpinNoise = nullptr;          // 20261026 ZJH 光学噪点/光照不均强度 (0~100)
    QSpinBox* m_pSpinBlur = nullptr;           // 20261026 ZJH 镜头失焦模糊等级 (0~10)
    QCheckBox* m_pChkDefect = nullptr;         // 20261026 ZJH 模拟 Mark 边缘微小缺口/污渍
    QLabel* m_pLabelTruth = nullptr;           // 标定真值
    QTableWidget* m_pTableCams = nullptr;      // 相机表
    QTableWidget* m_pTableMarks = nullptr;     // Mark 表
    QComboBox* m_pComboTargetShape = nullptr;  // 目标 Mark 图形
    QComboBox* m_pComboObjectShape = nullptr;  // 对象 Mark 图形
    QDoubleSpinBox* m_pSpinMarkSize = nullptr; // Mark 尺寸
    QDoubleSpinBox* m_pSpinLineWidth = nullptr;// 线宽
    QDoubleSpinBox* m_arrSpinOffset[3] = {};   // 来料偏差 x/y/θ
    QDoubleSpinBox* m_pSpinRandXY = nullptr;   // 随机偏差 XY 上限
    QDoubleSpinBox* m_pSpinRandTheta = nullptr;// 随机偏差 θ 上限
    QComboBox* m_pComboPopCam = nullptr;       // 弹出窗口选择的相机
    QLabel* m_pLabelResidual = nullptr;        // 残差显示
    QTimer* m_pResidualTimer = nullptr;        // 残差刷新节流
    bool m_bResidualDirty = false;             // 20261026 ZJH 页签不可见期间场景有变化, 待显示时刷新
};
