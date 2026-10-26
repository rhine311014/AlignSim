// 20261024 ZJH AlignPage 实现
#include "gui/AlignPage.h"

#include "gui/AlignView.h"
#include "motion/Stage.h"
#include "sim/AlignScene.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {
// 20261024 ZJH Mark 图形下拉项, 顺序与 MarkShape 数值一致
const QStringList s_lstShapes = { QStringLiteral("十字"), QStringLiteral("实心圆"), QStringLiteral("L 角"),
                                  QStringLiteral("实心方块"), QStringLiteral("圆环十字") };

QDoubleSpinBox* makeSpin(double dMin, double dMax, int nDecimals, double dValue, QWidget* pParent)
{
    auto* pSpin = new QDoubleSpinBox(pParent);
    pSpin->setRange(dMin, dMax);
    pSpin->setDecimals(nDecimals);
    pSpin->setValue(dValue);
    pSpin->setKeyboardTracking(false);
    return pSpin;  // 返回输入框
}

// 20261024 ZJH 读表格单元格 (空格按默认值)
QString cellText(const QTableWidget* pTable, int nRow, int nCol)
{
    const QTableWidgetItem* pItem = pTable->item(nRow, nCol);
    return pItem ? pItem->text().trimmed() : QString();
}
double cellDouble(const QTableWidget* pTable, int nRow, int nCol, double dDefault = 0.0)
{
    bool bOk = false;
    const double dValue = cellText(pTable, nRow, nCol).toDouble(&bOk);
    return bOk ? dValue : dDefault;
}

// 20261024 ZJH 表格增删行按钮
QWidget* makeRowButtons(QTableWidget* pTable, QWidget* pParent)
{
    auto* pHost = new QWidget(pParent);
    auto* pLayout = new QHBoxLayout(pHost);
    pLayout->setContentsMargins(0, 0, 0, 0);
    auto* pBtnAdd = new QPushButton(QStringLiteral("+ 行"), pHost);
    auto* pBtnDel = new QPushButton(QStringLiteral("- 行"), pHost);
    pLayout->addWidget(pBtnAdd);
    pLayout->addWidget(pBtnDel);
    pLayout->addStretch();
    QObject::connect(pBtnAdd, &QPushButton::clicked, pTable, [pTable] { pTable->insertRow(pTable->rowCount()); });
    QObject::connect(pBtnDel, &QPushButton::clicked, pTable, [pTable] {
        // 20261024 ZJH 删除选中行, 未选中则删最后一行
        const int nRow = pTable->currentRow() >= 0 ? pTable->currentRow() : pTable->rowCount() - 1;
        if (nRow >= 0) {
            pTable->removeRow(nRow);
        }
    });
    return pHost;  // 返回按钮条
}
}  // namespace

AlignPage::AlignPage(AlignScene* pScene, QWidget* pParent)
    : QWidget(pParent)
    , m_pScene(pScene)
{
    auto* pMain = new QHBoxLayout(this);
    pMain->setContentsMargins(8, 8, 8, 8);
    auto* pSplitter = new QSplitter(Qt::Horizontal, this);
    pSplitter->setHandleWidth(6);
    pMain->addWidget(pSplitter);

    // ====================================================================
    // 左侧: 顶部画布快捷控制栏 + 单画面高对比度视图
    // ====================================================================
    auto* pLeftHost = new QWidget(pSplitter);
    auto* pLeftLayout = new QVBoxLayout(pLeftHost);
    pLeftLayout->setContentsMargins(0, 0, 0, 0);
    pLeftLayout->setSpacing(6);

    auto* pViewCard = new QFrame(pLeftHost);
    pViewCard->setObjectName(QStringLiteral("viewToolbarCard"));
    auto* pViewCardLayout = new QVBoxLayout(pViewCard);
    pViewCardLayout->setContentsMargins(10, 6, 10, 6);
    pViewCardLayout->setSpacing(6);

    // 第一行: 画面模式 + 固定比例 + 弹出/存图
    auto* pTopRow1 = new QHBoxLayout();
    pTopRow1->setSpacing(8);
    m_pComboViewMode = new QComboBox(pViewCard);
    m_pComboViewMode->setMinimumWidth(165);
    m_pComboViewMode->setToolTip(QStringLiteral("切换下方单画面的显示模式: 全局视图 或 某台相机的纯净黑白拍屏视图"));
    m_pChkFixed = new QCheckBox(QStringLiteral("固定比例"), pViewCard);
    m_pSpinPxPerMm = makeSpin(0.01, 1000, 3, 20.0, pViewCard);
    m_pSpinPxPerMm->setSuffix(QStringLiteral(" px/mm"));
    m_pSpinPxPerMm->setToolTip(QStringLiteral("屏幕上 1mm 显示多少像素; 标定前请开启固定比例, 标定后勿改"));

    m_pComboPopCam = new QComboBox(pViewCard);
    auto* pBtnPopCam = new QPushButton(QStringLiteral("弹出相机"), pViewCard);
    auto* pBtnPopGlobal = new QPushButton(QStringLiteral("弹出全局"), pViewCard);
    auto* pBtnSaveSnap = new QPushButton(QStringLiteral("保存拍屏图"), pViewCard);
    pBtnSaveSnap->setToolTip(QStringLiteral("将当前选中的相机视图渲染保存为高对比度 PNG 图像到 logs/ 目录"));

    pTopRow1->addWidget(new QLabel(QStringLiteral("画面"), pViewCard));
    pTopRow1->addWidget(m_pComboViewMode);
    pTopRow1->addWidget(m_pChkFixed);
    pTopRow1->addWidget(m_pSpinPxPerMm);
    pTopRow1->addStretch();
    pTopRow1->addWidget(m_pComboPopCam);
    pTopRow1->addWidget(pBtnPopCam);
    pTopRow1->addWidget(pBtnPopGlobal);
    pTopRow1->addWidget(pBtnSaveSnap);
    pViewCardLayout->addLayout(pTopRow1);

    // 第二行: 黑白相机图层显示开关 + 一键标定居中
    auto* pTopRow2 = new QHBoxLayout();
    pTopRow2->setSpacing(10);
    m_pChkShowTarget = new QCheckBox(QStringLiteral("显示目标Mark(黑)"), pViewCard);
    m_pChkShowTarget->setChecked(true);
    m_pChkShowTarget->setToolTip(QStringLiteral("显示固定目标 Mark (纯黑 #000000); 相机抓取对象 Mark 时建议取消勾选以防重合干扰"));
    m_pChkShowObject = new QCheckBox(QStringLiteral("显示对象Mark(黑)"), pViewCard);
    m_pChkShowObject->setChecked(true);
    m_pChkShowObject->setToolTip(QStringLiteral("显示随对位平台运动的对象 Mark (高对比黑标)"));
    m_pChkCalibMark = new QCheckBox(QStringLiteral("显示标定Mark(黑)"), pViewCard);
    m_pChkCalibMark->setToolTip(QStringLiteral("勾选进入标定模式: 在各相机视野内显示纯黑标定 Mark, 并自动隐藏目标/对象/平台辅助线"));
    m_pChkShowStage = new QCheckBox(QStringLiteral("显示平台外框"), pViewCard);
    m_pChkShowStage->setChecked(true);
    m_pChkShowStage->setToolTip(QStringLiteral("在相机视野外围显示对位平台外框、UVW 支点及旋转中心 (相机视野内部始终保持100%纯白)"));
    auto* pBtnCenterMark = new QPushButton(QStringLiteral("Mark移到视野中央"), pViewCard);
    pBtnCenterMark->setProperty("primary", true);
    pBtnCenterMark->setToolTip(QStringLiteral("移动对位平台使标定 Mark 正好位于各相机视野正中央, 并清零来料偏差"));

    pTopRow2->addWidget(m_pChkShowTarget);
    pTopRow2->addWidget(m_pChkShowObject);
    pTopRow2->addWidget(m_pChkCalibMark);
    pTopRow2->addWidget(m_pChkShowStage);
    pTopRow2->addStretch();
    pTopRow2->addWidget(pBtnCenterMark);
    pViewCardLayout->addLayout(pTopRow2);
    pLeftLayout->addWidget(pViewCard);

    m_pMainView = createView(-1, pLeftHost);
    pLeftLayout->addWidget(m_pMainView, 1);

    // ====================================================================
    // 右侧: 实时看板 + 来料偏差快捷栏 + 分页配置面板 (无需上下长滚动)
    // ====================================================================
    auto* pRightPanel = new QWidget(pSplitter);
    auto* pRightLayout = new QVBoxLayout(pRightPanel);
    pRightLayout->setContentsMargins(2, 0, 0, 0);
    pRightLayout->setSpacing(8);
    pRightPanel->setMinimumWidth(480);

    // 1. 实时对位残差 & 标定真值看板
    auto* pGrpTelemetry = new QGroupBox(QStringLiteral("实时对位残差 & 标定真值看板"), pRightPanel);
    auto* pTeleLayout = new QVBoxLayout(pGrpTelemetry);
    pTeleLayout->setSpacing(6);

    auto* pResCard = new QFrame(pGrpTelemetry);
    pResCard->setObjectName(QStringLiteral("telemetryCard"));
    auto* pResCardLayout = new QVBoxLayout(pResCard);
    pResCardLayout->setContentsMargins(8, 6, 8, 6);
    m_pLabelResidual = new QLabel(pResCard);
    m_pLabelResidual->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 12px; color: #0F172A;"));
    m_pLabelResidual->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pResCardLayout->addWidget(m_pLabelResidual);
    pTeleLayout->addWidget(pResCard);

    auto* pTruthCard = new QFrame(pGrpTelemetry);
    pTruthCard->setObjectName(QStringLiteral("telemetryCard"));
    auto* pTruthCardLayout = new QVBoxLayout(pTruthCard);
    pTruthCardLayout->setContentsMargins(8, 6, 8, 6);
    m_pLabelTruth = new QLabel(pTruthCard);
    m_pLabelTruth->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 12px; color: #334155;"));
    m_pLabelTruth->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pLabelTruth->setWordWrap(true);
    pTruthCardLayout->addWidget(m_pLabelTruth);
    pTeleLayout->addWidget(pTruthCard);
    pRightLayout->addWidget(pGrpTelemetry);

    // 2. 来料偏差快捷操作卡片
    auto* pGrpOffset = new QGroupBox(QStringLiteral("来料偏差模拟 (产品放置误差)"), pRightPanel);
    auto* pOffsetLayout = new QGridLayout(pGrpOffset);
    pOffsetLayout->setHorizontalSpacing(6);
    pOffsetLayout->setVerticalSpacing(6);
    const QStringList lstOffsetNames = { QStringLiteral("ΔX"), QStringLiteral("ΔY"), QStringLiteral("Δθ°") };
    for (int i = 0; i < 3; ++i) {
        m_arrSpinOffset[i] = makeSpin(-1000, 1000, 4, 0.0, pGrpOffset);
        pOffsetLayout->addWidget(new QLabel(lstOffsetNames[i], pGrpOffset), 0, i * 2);
        pOffsetLayout->addWidget(m_arrSpinOffset[i], 0, i * 2 + 1);
    }
    auto* pBtnSetOffset = new QPushButton(QStringLiteral("设置偏差"), pGrpOffset);
    m_pSpinRandXY = makeSpin(0, 100, 3, 2.0, pGrpOffset);
    m_pSpinRandTheta = makeSpin(0, 45, 3, 1.0, pGrpOffset);
    auto* pBtnRandom = new QPushButton(QStringLiteral("随机生成来料偏差"), pGrpOffset);
    pBtnRandom->setProperty("primary", true);
    pOffsetLayout->addWidget(pBtnSetOffset, 1, 0, 1, 2);
    pOffsetLayout->addWidget(new QLabel(QStringLiteral("随机 ±XY"), pGrpOffset), 1, 2);
    pOffsetLayout->addWidget(m_pSpinRandXY, 1, 3);
    pOffsetLayout->addWidget(new QLabel(QStringLiteral("±θ°"), pGrpOffset), 1, 4);
    pOffsetLayout->addWidget(m_pSpinRandTheta, 1, 5);
    pOffsetLayout->addWidget(pBtnRandom, 2, 0, 1, 6);
    pRightLayout->addWidget(pGrpOffset);

    // 3. 分页配置区 (Tab 1: 相机与Mark表; Tab 2: Mark外观、标定与光学模拟)
    auto* pCfgTabs = new QTabWidget(pRightPanel);

    // ---- Tab 1: 相机与 Mark 表 ----
    auto* pTabTables = new QWidget(pCfgTabs);
    auto* pTablesLayout = new QVBoxLayout(pTabTables);
    pTablesLayout->setContentsMargins(8, 8, 8, 8);
    pTablesLayout->setSpacing(6);

    auto* pPresetRow = new QHBoxLayout();
    pPresetRow->addWidget(new QLabel(QStringLiteral("快速预设:"), pTabTables));
    auto* pBtnSingle = new QPushButton(QStringLiteral("单相机双Mark"), pTabTables);
    auto* pBtnDual = new QPushButton(QStringLiteral("双相机各一"), pTabTables);
    auto* pBtnTriple = new QPushButton(QStringLiteral("三相机品字形"), pTabTables);
    auto* pBtnQuad = new QPushButton(QStringLiteral("四相机四角"), pTabTables);
    pPresetRow->addWidget(pBtnSingle, 1);
    pPresetRow->addWidget(pBtnDual, 1);
    pPresetRow->addWidget(pBtnTriple, 1);
    pPresetRow->addWidget(pBtnQuad, 1);
    pTablesLayout->addLayout(pPresetRow);

    auto* pCamHeader = new QHBoxLayout();
    pCamHeader->addWidget(new QLabel(QStringLiteral("相机视窗 (世界坐标 mm)"), pTabTables));
    m_pTableCams = new QTableWidget(0, 5, pTabTables);
    m_pTableCams->setAlternatingRowColors(true);
    m_pTableCams->setHorizontalHeaderLabels({ QStringLiteral("名称"), QStringLiteral("中心X"), QStringLiteral("中心Y"),
                                              QStringLiteral("视野宽"), QStringLiteral("视野高") });
    m_pTableCams->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pTableCams->setMinimumHeight(90);
    pCamHeader->addStretch();
    pCamHeader->addWidget(makeRowButtons(m_pTableCams, pTabTables));
    pTablesLayout->addLayout(pCamHeader);
    pTablesLayout->addWidget(m_pTableCams, 2);

    auto* pMarkHeader = new QHBoxLayout();
    pMarkHeader->addWidget(new QLabel(QStringLiteral("Mark 坐标表 (相机从0开始; 尺寸≤0使用全局尺寸)"), pTabTables));
    m_pTableMarks = new QTableWidget(0, 9, pTabTables);
    m_pTableMarks->setAlternatingRowColors(true);
    m_pTableMarks->setHorizontalHeaderLabels({ QStringLiteral("名称"), QStringLiteral("相机"),
                                               QStringLiteral("目标X"), QStringLiteral("目标Y"), QStringLiteral("目标角"),
                                               QStringLiteral("对象X"), QStringLiteral("对象Y"), QStringLiteral("对象角"),
                                               QStringLiteral("尺寸") });
    m_pTableMarks->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pTableMarks->setMinimumHeight(105);
    pMarkHeader->addStretch();
    pMarkHeader->addWidget(makeRowButtons(m_pTableMarks, pTabTables));
    pTablesLayout->addLayout(pMarkHeader);
    pTablesLayout->addWidget(m_pTableMarks, 3);

    auto* pBtnApply = new QPushButton(QStringLiteral("应用相机与 Mark 表修改"), pTabTables);
    pBtnApply->setProperty("primary", true);
    pTablesLayout->addWidget(pBtnApply);
    pCfgTabs->addTab(pTabTables, QStringLiteral("相机与 Mark 表"));

    // ---- Tab 2: Mark 样式、标定参数与光学退化 ----
    auto* pTabStyle = new QWidget(pCfgTabs);
    auto* pStyleLayout = new QVBoxLayout(pTabStyle);
    pStyleLayout->setContentsMargins(8, 8, 8, 8);
    pStyleLayout->setSpacing(8);

    auto* pGrpStyle = new QGroupBox(QStringLiteral("对位 Mark 样式与尺寸 (即时生效)"), pTabStyle);
    auto* pStyleGrid = new QGridLayout(pGrpStyle);
    m_pComboTargetShape = new QComboBox(pGrpStyle);
    m_pComboTargetShape->addItems(s_lstShapes);
    m_pComboObjectShape = new QComboBox(pGrpStyle);
    m_pComboObjectShape->addItems(s_lstShapes);
    m_pSpinMarkSize = makeSpin(0.1, 200, 2, 4.0, pGrpStyle);
    m_pSpinMarkSize->setSingleStep(0.5);
    m_pSpinMarkSize->setSuffix(QStringLiteral(" mm"));
    m_pSpinMarkSize->setKeyboardTracking(true);
    m_pSpinLineWidth = makeSpin(0.01, 20, 2, 0.4, pGrpStyle);
    m_pSpinLineWidth->setSingleStep(0.1);
    m_pSpinLineWidth->setSuffix(QStringLiteral(" mm"));
    m_pSpinLineWidth->setKeyboardTracking(true);
    pStyleGrid->addWidget(new QLabel(QStringLiteral("目标图形"), pGrpStyle), 0, 0);
    pStyleGrid->addWidget(m_pComboTargetShape, 0, 1);
    pStyleGrid->addWidget(new QLabel(QStringLiteral("对象图形"), pGrpStyle), 0, 2);
    pStyleGrid->addWidget(m_pComboObjectShape, 0, 3);
    pStyleGrid->addWidget(new QLabel(QStringLiteral("全局尺寸"), pGrpStyle), 1, 0);
    pStyleGrid->addWidget(m_pSpinMarkSize, 1, 1);
    pStyleGrid->addWidget(new QLabel(QStringLiteral("全局线宽"), pGrpStyle), 1, 2);
    pStyleGrid->addWidget(m_pSpinLineWidth, 1, 3);
    pStyleLayout->addWidget(pGrpStyle);

    auto* pGrpCalibCfg = new QGroupBox(QStringLiteral("标定专用 Mark 参数 (纯黑高对比度)"), pTabStyle);
    auto* pCalibGrid = new QGridLayout(pGrpCalibCfg);
    m_pComboCalibShape = new QComboBox(pGrpCalibCfg);
    m_pComboCalibShape->addItems(s_lstShapes);
    m_pSpinCalibSize = makeSpin(0.1, 200, 2, 4.0, pGrpCalibCfg);
    m_pSpinCalibSize->setSingleStep(0.5);
    m_pSpinCalibSize->setSuffix(QStringLiteral(" mm"));
    m_pSpinCalibSize->setKeyboardTracking(true);
    m_pSpinCalibLw = makeSpin(0.01, 20, 2, 0.4, pGrpCalibCfg);
    m_pSpinCalibLw->setSingleStep(0.1);
    m_pSpinCalibLw->setSuffix(QStringLiteral(" mm"));
    m_pSpinCalibLw->setKeyboardTracking(true);
    m_pSpinCalibOffX = makeSpin(-200, 200, 3, 10.0, pGrpCalibCfg);
    m_pSpinCalibOffY = makeSpin(-200, 200, 3, 0.0, pGrpCalibCfg);
    m_pSpinCalibOffX->setSuffix(QStringLiteral(" mm"));
    m_pSpinCalibOffY->setSuffix(QStringLiteral(" mm"));
    m_pSpinCalibOffX->setToolTip(QStringLiteral("标定 Mark 相对相机中心安装偏移 (mm); 单相机旋转标定时建议设非0(如 10,0)以保证旋转力臂"));
    m_pSpinCalibOffY->setToolTip(m_pSpinCalibOffX->toolTip());
    pCalibGrid->addWidget(new QLabel(QStringLiteral("标定图形"), pGrpCalibCfg), 0, 0);
    pCalibGrid->addWidget(m_pComboCalibShape, 0, 1);
    pCalibGrid->addWidget(new QLabel(QStringLiteral("标定尺寸"), pGrpCalibCfg), 0, 2);
    pCalibGrid->addWidget(m_pSpinCalibSize, 0, 3);
    pCalibGrid->addWidget(new QLabel(QStringLiteral("标定线宽"), pGrpCalibCfg), 1, 0);
    pCalibGrid->addWidget(m_pSpinCalibLw, 1, 1);
    pCalibGrid->addWidget(new QLabel(QStringLiteral("安装偏移 X"), pGrpCalibCfg), 1, 2);
    pCalibGrid->addWidget(m_pSpinCalibOffX, 1, 3);
    pCalibGrid->addWidget(new QLabel(QStringLiteral("安装偏移 Y"), pGrpCalibCfg), 2, 2);
    pCalibGrid->addWidget(m_pSpinCalibOffY, 2, 3);
    pStyleLayout->addWidget(pGrpCalibCfg);

    auto* pGrpOptic = new QGroupBox(QStringLiteral("光学退化与干扰模拟 (测试黑白相机抗干扰鲁棒性)"), pTabStyle);
    auto* pOpticGrid = new QHBoxLayout(pGrpOptic);
    m_pSpinNoise = new QSpinBox(pGrpOptic);
    m_pSpinNoise->setRange(0, 100);
    m_pSpinNoise->setSuffix(QStringLiteral(" %噪点"));
    m_pSpinNoise->setToolTip(QStringLiteral("模拟背景光照渐变与传感器散斑噪点强度 (0%=理想纯白底)"));
    m_pSpinBlur = new QSpinBox(pGrpOptic);
    m_pSpinBlur->setRange(0, 10);
    m_pSpinBlur->setSuffix(QStringLiteral(" 级失焦"));
    m_pSpinBlur->setToolTip(QStringLiteral("模拟镜头轻微失焦造成的边缘柔化光晕 (0=理想锐利边缘)"));
    m_pChkDefect = new QCheckBox(QStringLiteral("模拟边缘瑕疵"), pGrpOptic);
    m_pChkDefect->setToolTip(QStringLiteral("在 Mark 边缘叠加微小缺口与污点, 测试视觉模板匹配/边缘拟合抗干扰能力"));
    pOpticGrid->addWidget(m_pSpinNoise);
    pOpticGrid->addWidget(m_pSpinBlur);
    pOpticGrid->addWidget(m_pChkDefect);
    pStyleLayout->addWidget(pGrpOptic);
    pStyleLayout->addStretch();
    pCfgTabs->addTab(pTabStyle, QStringLiteral("标定与样式/光学"));

    pRightLayout->addWidget(pCfgTabs, 1);

    pSplitter->addWidget(pLeftHost);
    pSplitter->addWidget(pRightPanel);
    pSplitter->setStretchFactor(0, 3);
    pSplitter->setStretchFactor(1, 2);
    pSplitter->setSizes({ 880, 500 });

    m_pResidualTimer = new QTimer(this);
    m_pResidualTimer->setSingleShot(true);
    m_pResidualTimer->setInterval(50);

    // ---------------- 信号 ----------------
    connect(pBtnSingle, &QPushButton::clicked, this, [this] { m_pScene->applyPresetSingleCamera(); loadFromScene(); });
    connect(pBtnDual, &QPushButton::clicked, this, [this] { m_pScene->applyPresetDualCamera(); loadFromScene(); });
    connect(pBtnTriple, &QPushButton::clicked, this, [this] { m_pScene->applyPresetTripleCamera(); loadFromScene(); });
    connect(pBtnQuad, &QPushButton::clicked, this, [this] { m_pScene->applyPresetQuadCamera(); loadFromScene(); });
    connect(pBtnApply, &QPushButton::clicked, this, &AlignPage::onApplyTables);
    connect(m_pComboViewMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int nIdx) {
        if (m_pMainView) {
            // 20261026 ZJH 第 0 项为全局视图 (-1), 第 1..N 项对应相机 0..N-1 纯净画面
            m_pMainView->setCameraIndex(nIdx - 1);
        }
    });
    connect(m_pChkFixed, &QCheckBox::toggled, this, &AlignPage::onDisplayChanged);
    connect(m_pSpinPxPerMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pChkShowStage, &QCheckBox::toggled, this, &AlignPage::onDisplayChanged);
    connect(m_pChkCalibMark, &QCheckBox::toggled, this, [this](bool bChecked) {
        // 20261026 ZJH 勾选进入标定模式时自动隐藏目标/对象 Mark 与平台辅助框, 并把标定 Mark 移到视野中央; 取消勾选时恢复
        {
            const QSignalBlocker b1(m_pChkShowTarget), b2(m_pChkShowObject), b3(m_pChkShowStage);
            m_pChkShowTarget->setChecked(!bChecked);
            m_pChkShowObject->setChecked(!bChecked);
            m_pChkShowStage->setChecked(!bChecked);
        }
        onDisplayChanged();
        if (bChecked) {
            m_pScene->centerCalibMark();
        }
    });
    connect(pBtnCenterMark, &QPushButton::clicked, this, [this] {
        m_pScene->centerCalibMark();
        const Pose offset = m_pScene->objectOffset();
        m_arrSpinOffset[0]->setValue(offset.dX);
        m_arrSpinOffset[1]->setValue(offset.dY);
        m_arrSpinOffset[2]->setValue(offset.dThetaDeg);
    });
    connect(m_pComboCalibShape, qOverload<int>(&QComboBox::currentIndexChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pSpinCalibSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pSpinCalibLw, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pSpinCalibOffX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pSpinCalibOffY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pChkShowTarget, &QCheckBox::toggled, this, &AlignPage::onDisplayChanged);
    connect(m_pChkShowObject, &QCheckBox::toggled, this, &AlignPage::onDisplayChanged);
    connect(m_pSpinNoise, qOverload<int>(&QSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pSpinBlur, qOverload<int>(&QSpinBox::valueChanged), this, &AlignPage::onDisplayChanged);
    connect(m_pChkDefect, &QCheckBox::toggled, this, &AlignPage::onDisplayChanged);
    connect(pBtnSaveSnap, &QPushButton::clicked, this, [this] {
        const int nCam = m_pComboPopCam->currentIndex();
        const QString strDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
        QDir().mkpath(strDir);
        const QString strPath = QStringLiteral("%1/拍屏_对位_%2_%3.png")
            .arg(strDir, m_pComboPopCam->currentText(),
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
        renderAlignSnapshot(m_pScene, nCam).save(strPath);
        m_pLabelTruth->setText(m_pLabelTruth->text() + QStringLiteral("\n[已保存拍屏图] ") + strPath);
    });
    connect(m_pComboTargetShape, qOverload<int>(&QComboBox::currentIndexChanged), this, &AlignPage::onStyleChanged);
    connect(m_pComboObjectShape, qOverload<int>(&QComboBox::currentIndexChanged), this, &AlignPage::onStyleChanged);
    connect(m_pSpinMarkSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onStyleChanged);
    connect(m_pSpinLineWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &AlignPage::onStyleChanged);
    connect(pBtnSetOffset, &QPushButton::clicked, this, &AlignPage::onSetOffset);
    connect(pBtnRandom, &QPushButton::clicked, this, &AlignPage::onRandomOffset);
    connect(pBtnPopCam, &QPushButton::clicked, this, &AlignPage::onPopoutCamera);
    connect(pBtnPopGlobal, &QPushButton::clicked, this, &AlignPage::onPopoutGlobal);
    connect(m_pResidualTimer, &QTimer::timeout, this, &AlignPage::refreshResidual);
    // 20261024 ZJH 场景/平台变化 → 节流刷新残差; 相机数量变化 → 重建视图网格
    // 20261026 ZJH 页签不可见时只记脏标记 (脚本跑验证时平台每 20ms 动一次, 不必为看不见的文本反复格式化)
    auto scheduleResidual = [this] {
        if (!isVisible()) {
            m_bResidualDirty = true;
        } else if (!m_pResidualTimer->isActive()) {
            m_pResidualTimer->start();
        }
    };
    connect(m_pScene, &AlignScene::changed, this, scheduleResidual, Qt::QueuedConnection);
    connect(m_pScene->stage(), &Stage::changed, this, scheduleResidual, Qt::QueuedConnection);
    connect(m_pScene, &AlignScene::changed, this, &AlignPage::rebuildCameraViews, Qt::QueuedConnection);

    loadFromScene();
}

AlignView* AlignPage::createView(int nCameraIndex, QWidget* pParent)
{
    auto* pView = new AlignView(m_pScene, nCameraIndex, pParent);
    m_lstViews.append(pView);
    return pView;  // 返回新视图
}

void AlignPage::setYDown(bool bYDown)
{
    AlignDisplay display = m_pScene->display();
    display.bYDown = bYDown;
    m_pScene->setDisplay(display);  // 视图从场景读取, 场景 changed 信号触发重绘
}

void AlignPage::onDisplayChanged()
{
    AlignDisplay display = m_pScene->display();
    display.bFixedScale = m_pChkFixed->isChecked();
    display.dPxPerMm = m_pSpinPxPerMm->value();
    display.bShowStage = m_pChkShowStage->isChecked();
    display.bShowTarget = m_pChkShowTarget->isChecked();
    display.bShowObject = m_pChkShowObject->isChecked();
    display.bShowCalibMark = m_pChkCalibMark->isChecked();
    display.nCalibShape = m_pComboCalibShape->currentIndex();
    display.dCalibSize = m_pSpinCalibSize->value();
    display.dCalibLineWidth = m_pSpinCalibLw->value();
    display.dCalibOffsetX = m_pSpinCalibOffX->value();
    display.dCalibOffsetY = m_pSpinCalibOffY->value();
    display.nNoiseLevel = m_pSpinNoise->value();
    display.nBlurLevel = m_pSpinBlur->value();
    display.bSimulateDefect = m_pChkDefect->isChecked();
    m_pScene->setDisplay(display);
}

void AlignPage::rebuildCameraViews()
{
    const std::vector<AlignCamera> vecCams = m_pScene->cameras();
    const int nCams = static_cast<int>(vecCams.size());
    // 20261026 ZJH 同步弹出窗口的相机下拉与主视图画面模式下拉
    QStringList lstNames;
    QStringList lstViewModes = { QStringLiteral("全局视图 (带标注)") };
    for (const AlignCamera& cam : vecCams) {
        lstNames << cam.strName;
        lstViewModes << QStringLiteral("%1 (纯净拍屏)").arg(cam.strName);
    }
    QStringList lstOld;
    for (int i = 0; i < m_pComboPopCam->count(); ++i) {
        lstOld << m_pComboPopCam->itemText(i);
    }
    if (lstNames != lstOld) {
        const int nKeep = m_pComboPopCam->currentIndex();
        m_pComboPopCam->clear();
        m_pComboPopCam->addItems(lstNames);
        m_pComboPopCam->setCurrentIndex(std::clamp(nKeep, 0, std::max(0, nCams - 1)));
    }
    if (m_pComboViewMode) {
        QStringList lstOldModes;
        for (int i = 0; i < m_pComboViewMode->count(); ++i) {
            lstOldModes << m_pComboViewMode->itemText(i);
        }
        if (lstViewModes != lstOldModes) {
            const int nKeepMode = m_pComboViewMode->currentIndex();
            const QSignalBlocker blocker(m_pComboViewMode);
            m_pComboViewMode->clear();
            m_pComboViewMode->addItems(lstViewModes);
            const int nNewMode = std::clamp(nKeepMode, 0, nCams);
            m_pComboViewMode->setCurrentIndex(nNewMode);
            if (m_pMainView) {
                m_pMainView->setCameraIndex(nNewMode - 1);
            }
        }
    }
}

void AlignPage::showEvent(QShowEvent* pEvent)
{
    QWidget::showEvent(pEvent);
    // 20261026 ZJH 隐藏期间场景有变化 → 显示时补一次刷新
    if (m_bResidualDirty) {
        refreshResidual();
    }
}

void AlignPage::loadFromScene()
{
    const std::vector<AlignCamera> vecCams = m_pScene->cameras();
    m_pTableCams->setRowCount(static_cast<int>(vecCams.size()));
    for (int i = 0; i < static_cast<int>(vecCams.size()); ++i) {  // 填相机表
        const AlignCamera& c = vecCams[i];
        const QStringList lst = { c.strName, QString::number(c.dCenterX), QString::number(c.dCenterY),
                                  QString::number(c.dFovW), QString::number(c.dFovH) };
        for (int nCol = 0; nCol < lst.size(); ++nCol) {
            m_pTableCams->setItem(i, nCol, new QTableWidgetItem(lst[nCol]));
        }
    }
    const std::vector<MarkPair> vecMarks = m_pScene->marks();
    m_pTableMarks->setRowCount(static_cast<int>(vecMarks.size()));
    for (int i = 0; i < static_cast<int>(vecMarks.size()); ++i) {  // 填 Mark 表
        const MarkPair& m = vecMarks[i];
        const QStringList lst = { m.strName, QString::number(m.nCamera),
                                  QString::number(m.dTargetX), QString::number(m.dTargetY), QString::number(m.dTargetAngle),
                                  QString::number(m.dObjectX), QString::number(m.dObjectY), QString::number(m.dObjectAngle),
                                  QString::number(m.dSize) };
        for (int nCol = 0; nCol < lst.size(); ++nCol) {
            m_pTableMarks->setItem(i, nCol, new QTableWidgetItem(lst[nCol]));
        }
    }
    // 20261024 ZJH 外观控件 (先屏蔽信号, 避免回写触发 onStyleChanged)
    const MarkStyle style = m_pScene->style();
    const QSignalBlocker b1(m_pComboTargetShape), b2(m_pComboObjectShape), b3(m_pSpinMarkSize), b4(m_pSpinLineWidth);
    m_pComboTargetShape->setCurrentIndex(static_cast<int>(style.eTargetShape));
    m_pComboObjectShape->setCurrentIndex(static_cast<int>(style.eObjectShape));
    m_pSpinMarkSize->setValue(style.dSize);
    m_pSpinLineWidth->setValue(style.dLineWidth);
    const Pose offset = m_pScene->objectOffset();
    m_arrSpinOffset[0]->setValue(offset.dX);
    m_arrSpinOffset[1]->setValue(offset.dY);
    m_arrSpinOffset[2]->setValue(offset.dThetaDeg);
    const AlignDisplay display = m_pScene->display();
    const QSignalBlocker b5(m_pChkFixed), b6(m_pSpinPxPerMm), b7(m_pChkShowTarget), b8(m_pChkShowObject),
        b9(m_pSpinNoise), b10(m_pChkDefect), b11(m_pSpinBlur), b12(m_pChkCalibMark),
        b13(m_pComboCalibShape), b14(m_pSpinCalibSize), b15(m_pSpinCalibLw),
        b16(m_pSpinCalibOffX), b17(m_pSpinCalibOffY), b18(m_pChkShowStage);
    m_pChkFixed->setChecked(display.bFixedScale);
    m_pSpinPxPerMm->setValue(display.dPxPerMm);
    m_pChkShowStage->setChecked(display.bShowStage);
    m_pChkShowTarget->setChecked(display.bShowTarget);
    m_pChkShowObject->setChecked(display.bShowObject);
    m_pChkCalibMark->setChecked(display.bShowCalibMark);
    m_pComboCalibShape->setCurrentIndex(std::clamp(display.nCalibShape, 0, 4));
    m_pSpinCalibSize->setValue(display.dCalibSize);
    m_pSpinCalibLw->setValue(display.dCalibLineWidth);
    m_pSpinCalibOffX->setValue(display.dCalibOffsetX);
    m_pSpinCalibOffY->setValue(display.dCalibOffsetY);
    m_pSpinNoise->setValue(display.nNoiseLevel);
    m_pSpinBlur->setValue(display.nBlurLevel);
    m_pChkDefect->setChecked(display.bSimulateDefect);
    rebuildCameraViews();
    refreshResidual();
}

void AlignPage::onApplyTables()
{
    std::vector<AlignCamera> vecCams;
    for (int i = 0; i < m_pTableCams->rowCount(); ++i) {  // 读相机表
        AlignCamera cam;
        cam.strName = cellText(m_pTableCams, i, 0);
        if (cam.strName.isEmpty()) {
            cam.strName = QStringLiteral("相机%1").arg(i + 1);  // 名称为空时自动命名
        }
        cam.dCenterX = cellDouble(m_pTableCams, i, 1);
        cam.dCenterY = cellDouble(m_pTableCams, i, 2);
        cam.dFovW = std::max(0.1, cellDouble(m_pTableCams, i, 3, 40.0));  // 视野至少 0.1mm
        cam.dFovH = std::max(0.1, cellDouble(m_pTableCams, i, 4, 30.0));
        vecCams.push_back(cam);
    }
    std::vector<MarkPair> vecMarks;
    for (int i = 0; i < m_pTableMarks->rowCount(); ++i) {  // 读 Mark 表
        MarkPair m;
        m.strName = cellText(m_pTableMarks, i, 0);
        if (m.strName.isEmpty()) {
            m.strName = QStringLiteral("Mark%1").arg(i + 1);
        }
        // 20261024 ZJH 相机号限制在有效范围
        m.nCamera = std::clamp(static_cast<int>(cellDouble(m_pTableMarks, i, 1)), 0, std::max(0, static_cast<int>(vecCams.size()) - 1));
        m.dTargetX = cellDouble(m_pTableMarks, i, 2);
        m.dTargetY = cellDouble(m_pTableMarks, i, 3);
        m.dTargetAngle = cellDouble(m_pTableMarks, i, 4);
        m.dObjectX = cellDouble(m_pTableMarks, i, 5);
        m.dObjectY = cellDouble(m_pTableMarks, i, 6);
        m.dObjectAngle = cellDouble(m_pTableMarks, i, 7);
        m.dSize = std::max(0.0, cellDouble(m_pTableMarks, i, 8));
        vecMarks.push_back(m);
    }
    m_pScene->setCameras(vecCams);
    m_pScene->setMarks(vecMarks);
    loadFromScene();  // 回显规范化后的值
}

void AlignPage::onStyleChanged()
{
    MarkStyle style;
    style.eTargetShape = static_cast<MarkShape>(m_pComboTargetShape->currentIndex());
    style.eObjectShape = static_cast<MarkShape>(m_pComboObjectShape->currentIndex());
    style.dSize = m_pSpinMarkSize->value();
    style.dLineWidth = m_pSpinLineWidth->value();
    m_pScene->setStyle(style);
}

void AlignPage::onSetOffset()
{
    m_pScene->setObjectOffset(Pose{ m_arrSpinOffset[0]->value(), m_arrSpinOffset[1]->value(), m_arrSpinOffset[2]->value() });
}

void AlignPage::onRandomOffset()
{
    m_pScene->randomizeObjectOffset(m_pSpinRandXY->value(), m_pSpinRandTheta->value());
    const Pose offset = m_pScene->objectOffset();  // 回显随机结果
    m_arrSpinOffset[0]->setValue(offset.dX);
    m_arrSpinOffset[1]->setValue(offset.dY);
    m_arrSpinOffset[2]->setValue(offset.dThetaDeg);
}

void AlignPage::onPopoutCamera()
{
    const int nIndex = m_pComboPopCam->currentIndex();
    // 20261024 ZJH 没有相机时不弹
    if (nIndex < 0) {
        return;
    }
    auto* pWin = new ViewWindow(createView(nIndex, nullptr), m_pComboPopCam->currentText(), nullptr);
    pWin->show();
}

void AlignPage::onPopoutGlobal()
{
    auto* pWin = new ViewWindow(createView(-1, nullptr), QStringLiteral("对位全局视图"), nullptr);
    pWin->show();
}

void AlignPage::refreshResidual()
{
    m_bResidualDirty = false;
    const AlignResidual res = m_pScene->residual();
    // 20261024 ZJH 没有 Mark
    if (!res.bValid) {
        m_pLabelResidual->setText(QStringLiteral("无 Mark"));
        return;
    }
    const std::vector<MarkPair> vecMarks = m_pScene->marks();
    QString strText = QStringLiteral("整体  ΔX=%1  ΔY=%2  Δθ=%3°\n")
                          .arg(res.dX, 9, 'f', 4).arg(res.dY, 9, 'f', 4).arg(res.dThetaDeg, 9, 'f', 4);
    for (int i = 0; i < static_cast<int>(res.vecMarkDelta.size()); ++i) {  // 每个 Mark 的位置差
        const QPointF pt = res.vecMarkDelta[i];
        strText += QStringLiteral("%1  ΔX=%2  ΔY=%3  |Δ|=%4\n")
                       .arg(i < static_cast<int>(vecMarks.size()) ? vecMarks[i].strName : QString::number(i), -6)
                       .arg(pt.x(), 9, 'f', 4).arg(pt.y(), 9, 'f', 4).arg(std::hypot(pt.x(), pt.y()), 8, 'f', 4);
    }
    m_pLabelResidual->setText(strText.trimmed());

    // 20261025 ZJH 标定真值 (同步显示复选框, 脚本 ShowAlignTarget/ShowAlignObject/ShowAlignCalibMark 可能改了场景)
    const AlignDisplay disp = m_pScene->display();
    {
        const QSignalBlocker bT(m_pChkShowTarget), bO(m_pChkShowObject), bC(m_pChkCalibMark), bS(m_pChkShowStage);
        m_pChkShowStage->setChecked(disp.bShowStage);
        m_pChkShowTarget->setChecked(disp.bShowTarget);
        m_pChkShowObject->setChecked(disp.bShowObject);
        m_pChkCalibMark->setChecked(disp.bShowCalibMark);
    }
    Stage* pStage = m_pScene->stage();
    const StageGeometry geo = pStage->geometry();
    const QPointF ptRc = pStage->mapFromHome(pStage->actualRotCenter());  // 实际旋转中心当前世界坐标 (真值)
    const StageErrorModel err = pStage->errorModel();
    QString strTruth = QStringLiteral("显示  %1  Y轴%2\n")
        .arg(disp.bFixedScale ? QStringLiteral("固定比例 %1 px/mm (%2 mm/px)").arg(disp.dPxPerMm, 0, 'f', 3).arg(1.0 / disp.dPxPerMm, 0, 'f', 5)
                              : QStringLiteral("<自适应窗口, 不可标定>"))
        .arg(disp.bYDown ? QStringLiteral("朝下(屏幕图像与世界坐标镜像)") : QStringLiteral("朝上"));
    strTruth += QStringLiteral("平台  %1  实际旋转中心(当前) X=%2 Y=%3 mm\n")
        .arg(geo.eType == StageType::UVW ? QStringLiteral("UVW") : QStringLiteral("XYD"))
        .arg(ptRc.x(), 0, 'f', 4).arg(ptRc.y(), 0, 'f', 4);
    // 20261025 ZJH 误差注入时额外给出 标称 vs 实际 与 指令 vs 实际 的差
    if (err.bEnabled) {
        const Pose poseCmd = pStage->commandedPose();
        const Pose poseAct = pStage->pose();
        strTruth += QStringLiteral("误差注入 开  旋转中心偏差 (%1, %2) mm  实际−指令 ΔX=%3 ΔY=%4 mm Δθ=%5°\n")
            .arg(err.dRotCenterErrX, 0, 'f', 4).arg(err.dRotCenterErrY, 0, 'f', 4)
            .arg(poseAct.dX - poseCmd.dX, 0, 'f', 5).arg(poseAct.dY - poseCmd.dY, 0, 'f', 5)
            .arg(poseAct.dThetaDeg - poseCmd.dThetaDeg, 0, 'f', 5);
    }
    const std::vector<AlignCamera> vecCams = m_pScene->cameras();
    for (int i = 0; i < static_cast<int>(vecCams.size()); ++i) {  // 每个相机: 旋转中心相对视窗中心
        const AlignCamera& cam = vecCams[i];
        const QPointF ptRel = ptRc - QPointF(cam.dCenterX, cam.dCenterY);
        strTruth += QStringLiteral("%1  旋转中心相对视窗中心 ΔX=%2 ΔY=%3 mm").arg(cam.strName).arg(ptRel.x(), 0, 'f', 4).arg(ptRel.y(), 0, 'f', 4);
        // 20261025 ZJH 固定比例下同时给出屏幕像素 (相机像素 = 屏幕像素 × 镜头放大倍率, 由视觉标定得出)
        if (disp.bFixedScale) {
            const QPointF ptPx = screenOffsetPx(ptRc, QPointF(cam.dCenterX, cam.dCenterY), disp.dPxPerMm, disp.bYDown);
            strTruth += QStringLiteral("  屏幕 (%1, %2) px  视野 %3×%4 px")
                .arg(ptPx.x(), 0, 'f', 1).arg(ptPx.y(), 0, 'f', 1)
                .arg(cam.dFovW * disp.dPxPerMm, 0, 'f', 0).arg(cam.dFovH * disp.dPxPerMm, 0, 'f', 0);
        }
        strTruth += QLatin1Char('\n');
        if (disp.bShowCalibMark) {
            const MarkWorldPose calibPose = m_pScene->calibMarkWorld(i);
            const double dArm = std::hypot(calibPose.ptPos.x() - ptRc.x(), calibPose.ptPos.y() - ptRc.y());
            strTruth += QStringLiteral("  ↳ 标定Mark(%1) 世界(%2, %3) mm  到旋转中心力臂=%4 mm")
                .arg(cam.strName).arg(calibPose.ptPos.x(), 0, 'f', 4)
                .arg(calibPose.ptPos.y(), 0, 'f', 4).arg(dArm, 0, 'f', 4);
            QPointF ptCalibPx;
            if (m_pScene->calibMarkScreenOffset(i, ptCalibPx)) {
                strTruth += QStringLiteral("  屏幕(%1, %2) px").arg(ptCalibPx.x(), 0, 'f', 2).arg(ptCalibPx.y(), 0, 'f', 2);
            }
            strTruth += QLatin1Char('\n');
        }
    }
    for (int i = 0; i < static_cast<int>(vecMarks.size()); ++i) {  // 每个对象 Mark 到旋转中心的距离
        const QPointF ptMark = m_pScene->objectMarkWorld(i).ptPos;
        strTruth += QStringLiteral("%1  对象Mark到旋转中心 %2 mm\n").arg(vecMarks[i].strName)
                        .arg(std::hypot(ptMark.x() - ptRc.x(), ptMark.y() - ptRc.y()), 0, 'f', 4);
    }
    m_pLabelTruth->setText(strTruth.trimmed());
}
