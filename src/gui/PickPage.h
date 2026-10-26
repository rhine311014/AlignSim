// 20261024 ZJH PickPage —— 大视野拾取模拟页面
// 左: 拾取视图; 右: 场景配置 / 产品表 / 手动拾取 / 显示选项 / 拾取误差与统计 / 弹出视图
#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

class PickScene;
class PickView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

class PickPage : public QWidget
{
    Q_OBJECT
public:
    explicit PickPage(PickScene* pScene, QWidget* pParent = nullptr);

    void setYDown(bool bYDown);  // 写入场景显示配置
    void loadFromScene();  // 场景配置 → 控件

protected:
    void showEvent(QShowEvent* pEvent) override;  // 20261026 ZJH 切回本页签时补刷新

private slots:
    void onApplyConfig();                          // 控件 → 场景配置
    void onSpawn();                                // 随机摆放
    void onProductEdited(QTableWidgetItem* pItem); // 产品表编辑 → 手动设位姿
    void onMoveToSelected();                       // 拾取头移到选中产品 (调试用, 完美拾取)
    void onPick();                                 // 手动拾取
    void onPlace();                                // 手动放下
    void onPopout();                               // 弹出视图窗口
    void refresh();                                // 刷新产品表与结果
    void onDisplayChanged();                       // 显示控件 → 场景显示配置

private:
    PickView* createView(QWidget* pParent);  // 创建视图并登记

    PickScene* m_pScene = nullptr;            // 场景 (不持有)
    QList<QPointer<PickView>> m_lstViews;     // 所有视图
    QCheckBox* m_pChkFixed = nullptr;         // 固定比例
    QDoubleSpinBox* m_pSpinPxPerMm = nullptr; // 固定比例 px/mm
    QCheckBox* m_pChkShowStage = nullptr;     // 20261026 ZJH 显示拾取头(平台/吸嘴十字)
    QCheckBox* m_pChkCalibMark = nullptr;     // 显示标定 Mark
    QCheckBox* m_pChkProducts = nullptr;      // 显示产品
    QComboBox* m_pComboCalibShape = nullptr;  // 标定 Mark 图形
    QDoubleSpinBox* m_pSpinCalibSize = nullptr;   // 标定 Mark 尺寸
    QDoubleSpinBox* m_pSpinCalibLineWidth = nullptr;  // 20261026 ZJH 标定 Mark 线宽
    QComboBox* m_pComboCalibMount = nullptr;  // 20261026 ZJH 标定 Mark 挂载位置 (0=旋转中心, 1=1号爪, 2=2号爪)
    QDoubleSpinBox* m_pSpinCalibOffX = nullptr;   // 标定 Mark 偏移 X
    QDoubleSpinBox* m_pSpinCalibOffY = nullptr;   // 标定 Mark 偏移 Y
    QSpinBox* m_pSpinNoise = nullptr;         // 20261026 ZJH 光学噪点/光照不均强度 (0~100)
    QSpinBox* m_pSpinBlur = nullptr;          // 20261026 ZJH 镜头失焦模糊等级 (0~10)
    QCheckBox* m_pChkDefect = nullptr;        // 20261026 ZJH 模拟产品/Mark边缘瑕疵
    QLabel* m_pLabelTruth = nullptr;          // 标定真值
    QDoubleSpinBox* m_pSpinFieldW = nullptr;  // 视野宽
    QDoubleSpinBox* m_pSpinFieldH = nullptr;  // 视野高
    QComboBox* m_pComboShape = nullptr;       // 产品外形
    QDoubleSpinBox* m_pSpinProdW = nullptr;   // 产品宽
    QDoubleSpinBox* m_pSpinProdH = nullptr;   // 产品高
    QSpinBox* m_pSpinCount = nullptr;         // 数量
    QDoubleSpinBox* m_pSpinMaxAngle = nullptr;// 随机角度范围
    QDoubleSpinBox* m_pSpinTolMm = nullptr;   // 距离容差
    QDoubleSpinBox* m_pSpinTolDeg = nullptr;  // 角度容差
    QLineEdit* m_pEditMarkText = nullptr;     // 20261026 ZJH 产品丝印字符
    QDoubleSpinBox* m_pSpinMarkTextH = nullptr;  // 20261026 ZJH 产品丝印字符字高 (mm)
    QCheckBox* m_pChkRandomFlip = nullptr;    // 20261026 ZJH 随机摆放含反面产品
    QCheckBox* m_pChkDualGripper = nullptr;   // 20261026 ZJH 双爪模式开关 (单头双吸嘴先后抓取)
    QDoubleSpinBox* m_pSpinNozzle1X = nullptr;// 20261026 ZJH 1号爪相对旋转中心偏移 X (mm)
    QDoubleSpinBox* m_pSpinNozzle1Y = nullptr;// 20261026 ZJH 1号爪相对旋转中心偏移 Y (mm)
    QDoubleSpinBox* m_pSpinNozzle2X = nullptr;// 20261026 ZJH 2号爪相对旋转中心偏移 X (mm)
    QDoubleSpinBox* m_pSpinNozzle2Y = nullptr;// 20261026 ZJH 2号爪相对旋转中心偏移 Y (mm)
    QComboBox* m_pComboActiveNozzle = nullptr;// 20261026 ZJH 手动操作当前选用的爪号 (1号爪 / 2号爪)
    QTableWidget* m_pTableProducts = nullptr; // 产品表
    QCheckBox* m_pChkLabels = nullptr;        // 显示编号
    QCheckBox* m_pChkHead = nullptr;          // 显示拾取头
    QLabel* m_pLabelResult = nullptr;         // 结果与统计
    QTimer* m_pRefreshTimer = nullptr;        // 刷新节流
    bool m_bFilling = false;                  // 程序填表中
    bool m_bDirty = false;                    // 20261026 ZJH 页签不可见期间场景有变化, 待显示时刷新
};
