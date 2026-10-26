// 20261024 ZJH PickPage 实现
#include "gui/PickPage.h"

#include "gui/AlignView.h"
#include "gui/PickView.h"
#include "motion/Stage.h"
#include "sim/PickScene.h"

#include <QApplication>
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
#include <QLineEdit>
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
QDoubleSpinBox* makeSpin(double dMin, double dMax, int nDecimals, QWidget* pParent)
{
    auto* pSpin = new QDoubleSpinBox(pParent);
    pSpin->setRange(dMin, dMax);
    pSpin->setDecimals(nDecimals);
    pSpin->setKeyboardTracking(false);
    return pSpin;  // 返回输入框
}
}  // namespace

PickPage::PickPage(PickScene* pScene, QWidget* pParent)
    : QWidget(pParent)
    , m_pScene(pScene)
{
    auto* pMain = new QHBoxLayout(this);
    pMain->setContentsMargins(8, 8, 8, 8);
    auto* pSplitter = new QSplitter(Qt::Horizontal, this);
    pSplitter->setHandleWidth(6);
    pMain->addWidget(pSplitter);

    // ====================================================================
    // 左侧: 顶部画布快捷控制栏 + 大视野高对比度视图
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

    // 第一行: 固定比例 + 随机上料/步进 + 弹出/存图
    auto* pTopRow1 = new QHBoxLayout();
    pTopRow1->setSpacing(8);
    m_pChkFixed = new QCheckBox(QStringLiteral("固定比例"), pViewCard);
    m_pSpinPxPerMm = makeSpin(0.01, 1000, 3, pViewCard);
    m_pSpinPxPerMm->setSuffix(QStringLiteral(" px/mm"));
    m_pSpinPxPerMm->setToolTip(QStringLiteral("标定前必须开启固定比例, 标定后勿改; 开启固定比例时自动隐藏编号文字以防黑白相机误抓"));

    auto* pBtnSpawn = new QPushButton(QStringLiteral("随机摆放产品"), pViewCard);
    pBtnSpawn->setProperty("primary", true);
    auto* pBtnConveyor = new QPushButton(QStringLiteral("传送带步进上料"), pViewCard);
    pBtnConveyor->setToolTip(QStringLiteral("模拟皮带向 +X 方向步进 140mm, 并在左侧入料区补充新来料产品"));
    auto* pBtnPop = new QPushButton(QStringLiteral("弹出视图 (F11)"), pViewCard);
    auto* pBtnSaveSnap = new QPushButton(QStringLiteral("保存拍屏图"), pViewCard);
    pBtnSaveSnap->setToolTip(QStringLiteral("把当前大视野渲染保存为高对比度 PNG 图像到 logs/ 目录"));

    pTopRow1->addWidget(m_pChkFixed);
    pTopRow1->addWidget(m_pSpinPxPerMm);
    pTopRow1->addStretch();
    pTopRow1->addWidget(pBtnSpawn);
    pTopRow1->addWidget(pBtnConveyor);
    pTopRow1->addWidget(pBtnPop);
    pTopRow1->addWidget(pBtnSaveSnap);
    pViewCardLayout->addLayout(pTopRow1);

    // 第二行: 黑白相机图层开关 + 一键标定居中
    auto* pTopRow2 = new QHBoxLayout();
    pTopRow2->setSpacing(10);
    m_pChkProducts = new QCheckBox(QStringLiteral("显示产品(黑)"), pViewCard);
    m_pChkProducts->setChecked(true);
    m_pChkCalibMark = new QCheckBox(QStringLiteral("显示标定Mark(黑)"), pViewCard);
    m_pChkCalibMark->setToolTip(QStringLiteral("勾选进入手眼标定模式: 自动隐藏产品、拾取头与编号, 仅显示纯白底+纯黑标定 Mark 并移到视野中央"));
    m_pChkShowStage = new QCheckBox(QStringLiteral("显示平台(拾取头)"), pViewCard);
    m_pChkShowStage->setChecked(true);
    m_pChkShowStage->setToolTip(QStringLiteral("勾选显示拾取头吸嘴十字与双爪横梁; 黑白相机拍屏识别时建议取消勾选"));
    m_pChkHead = new QCheckBox(QStringLiteral("显示拾取头"), pViewCard);
    m_pChkHead->setChecked(true);
    m_pChkHead->setVisible(false);  // 与 m_pChkShowStage 联动保持兼容
    m_pChkLabels = new QCheckBox(QStringLiteral("显示编号与料盘区"), pViewCard);
    m_pChkLabels->setChecked(true);
    m_pChkLabels->setToolTip(QStringLiteral("人眼观察模式下显示产品编号与右侧 OK/NG 料盘区; 开启固定比例拍屏时自动隐藏文字"));
    auto* pBtnCenterMark = new QPushButton(QStringLiteral("Mark移到视野中央"), pViewCard);
    pBtnCenterMark->setProperty("primary", true);
    pBtnCenterMark->setToolTip(QStringLiteral("移动拾取头使标定 Mark 正好位于视野正中央 (W/2, H/2)"));

    pTopRow2->addWidget(m_pChkProducts);
    pTopRow2->addWidget(m_pChkCalibMark);
    pTopRow2->addWidget(m_pChkShowStage);
    pTopRow2->addWidget(m_pChkLabels);
    pTopRow2->addStretch();
    pTopRow2->addWidget(pBtnCenterMark);
    pViewCardLayout->addLayout(pTopRow2);
    pLeftLayout->addWidget(pViewCard);

    pLeftLayout->addWidget(createView(pLeftHost), 1);

    // ====================================================================
    // 右侧: 实时看板 + 手动拾取操作栏 + 分页配置面板 (无需上下长滚动)
    // ====================================================================
    auto* pRightPanel = new QWidget(pSplitter);
    auto* pRightLayout = new QVBoxLayout(pRightPanel);
    pRightLayout->setContentsMargins(2, 0, 0, 0);
    pRightLayout->setSpacing(8);
    pRightPanel->setMinimumWidth(480);

    // 1. 实时拾取统计 & 标定真值看板
    auto* pGrpTelemetry = new QGroupBox(QStringLiteral("吸嘴位姿、拾取精度统计 & 标定真值看板"), pRightPanel);
    auto* pTeleLayout = new QVBoxLayout(pGrpTelemetry);
    pTeleLayout->setSpacing(6);

    auto* pResCard = new QFrame(pGrpTelemetry);
    pResCard->setObjectName(QStringLiteral("telemetryCard"));
    auto* pResCardLayout = new QHBoxLayout(pResCard);
    pResCardLayout->setContentsMargins(8, 6, 8, 6);
    m_pLabelResult = new QLabel(pResCard);
    m_pLabelResult->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 12px; color: #0F172A;"));
    m_pLabelResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* pBtnReset = new QPushButton(QStringLiteral("重置统计"), pResCard);
    pResCardLayout->addWidget(m_pLabelResult, 1);
    pResCardLayout->addWidget(pBtnReset, 0, Qt::AlignTop);
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

    // 2. 手动测试操作栏
    auto* pGrpManual = new QGroupBox(QStringLiteral("手动操作与双爪测试 (右键点击视野可直接移动当前吸嘴)"), pRightPanel);
    auto* pManualLayout = new QHBoxLayout(pGrpManual);
    pManualLayout->setSpacing(6);
    m_pComboActiveNozzle = new QComboBox(pGrpManual);
    m_pComboActiveNozzle->addItems({ QStringLiteral("1号爪"), QStringLiteral("2号爪") });
    m_pComboActiveNozzle->setToolTip(QStringLiteral("选择当前对准/拾取使用的爪号 (双爪模式下可切换 1号爪 / 2号爪)"));
    auto* pBtnMoveTo = new QPushButton(QStringLiteral("移到选中产品"), pGrpManual);
    pBtnMoveTo->setToolTip(QStringLiteral("按产品真值把当前选定爪移到选中产品处 (完美对准, 用于验证)"));
    auto* pBtnFlipSel = new QPushButton(QStringLiteral("翻转正反"), pGrpManual);
    pBtnFlipSel->setToolTip(QStringLiteral("把选中产品在正面(带TOP字) / 反面(纯黑无字)之间切换"));
    auto* pBtnPick = new QPushButton(QStringLiteral("吸取"), pGrpManual);
    pBtnPick->setProperty("primary", true);
    auto* pBtnPlace = new QPushButton(QStringLiteral("全部放下"), pGrpManual);
    auto* pBtnHome = new QPushButton(QStringLiteral("回零"), pGrpManual);
    pManualLayout->addWidget(m_pComboActiveNozzle);
    pManualLayout->addWidget(pBtnMoveTo);
    pManualLayout->addWidget(pBtnFlipSel);
    pManualLayout->addWidget(pBtnPick);
    pManualLayout->addWidget(pBtnPlace);
    pManualLayout->addWidget(pBtnHome);
    pRightLayout->addWidget(pGrpManual);

    // 3. 分页配置区 (Tab 1: 产品实时表; Tab 2: 视野/产品/双爪配置; Tab 3: 手眼标定与光学)
    auto* pCfgTabs = new QTabWidget(pRightPanel);

    // ---- Tab 1: 产品列表 ----
    auto* pTabProd = new QWidget(pCfgTabs);
    auto* pProdLayout = new QVBoxLayout(pTabProd);
    pProdLayout->setContentsMargins(8, 8, 8, 8);
    pProdLayout->setSpacing(6);
    pProdLayout->addWidget(new QLabel(QStringLiteral("视野内产品列表 (X/Y/θ° 可直接编辑, 勾选「正反」切换正反面, 左键画面可拖拽)"), pTabProd));
    m_pTableProducts = new QTableWidget(0, 6, pTabProd);
    m_pTableProducts->setAlternatingRowColors(true);
    m_pTableProducts->setHorizontalHeaderLabels({ QStringLiteral("编号"), QStringLiteral("X (mm)"), QStringLiteral("Y (mm)"),
                                                  QStringLiteral("θ (°)"), QStringLiteral("正反面"), QStringLiteral("状态") });
    m_pTableProducts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pTableProducts->verticalHeader()->setVisible(false);
    m_pTableProducts->setSelectionBehavior(QAbstractItemView::SelectRows);
    pProdLayout->addWidget(m_pTableProducts, 1);
    pCfgTabs->addTab(pTabProd, QStringLiteral("产品列表"));

    // ---- Tab 2: 视野、产品与双爪配置 ----
    auto* pTabCfg = new QWidget(pCfgTabs);
    auto* pForm = new QFormLayout(pTabCfg);
    pForm->setContentsMargins(10, 10, 10, 10);
    pForm->setSpacing(7);
    m_pSpinFieldW = makeSpin(1, 100000, 1, pTabCfg);
    m_pSpinFieldH = makeSpin(1, 100000, 1, pTabCfg);
    auto* pFieldRow = new QHBoxLayout();
    pFieldRow->addWidget(m_pSpinFieldW);
    pFieldRow->addWidget(m_pSpinFieldH);
    pForm->addRow(QStringLiteral("视野 宽/高 (mm)"), pFieldRow);

    auto* pShapeCountRow = new QHBoxLayout();
    m_pComboShape = new QComboBox(pTabCfg);
    m_pComboShape->addItems({ QStringLiteral("矩形"), QStringLiteral("圆形 (直径=宽)"), QStringLiteral("L 形") });
    m_pSpinCount = new QSpinBox(pTabCfg);
    m_pSpinCount->setRange(0, 100);
    m_pSpinCount->setSuffix(QStringLiteral(" 个"));
    pShapeCountRow->addWidget(m_pComboShape, 1);
    pShapeCountRow->addWidget(new QLabel(QStringLiteral("数量:"), pTabCfg));
    pShapeCountRow->addWidget(m_pSpinCount);
    pForm->addRow(QStringLiteral("产品外形与数量"), pShapeCountRow);

    m_pSpinProdW = makeSpin(1, 10000, 2, pTabCfg);
    m_pSpinProdH = makeSpin(1, 10000, 2, pTabCfg);
    m_pSpinMaxAngle = makeSpin(0, 180, 1, pTabCfg);
    m_pSpinMaxAngle->setSuffix(QStringLiteral(" °"));
    auto* pProdRow = new QHBoxLayout();
    pProdRow->addWidget(m_pSpinProdW);
    pProdRow->addWidget(m_pSpinProdH);
    pProdRow->addWidget(new QLabel(QStringLiteral("随机±角:"), pTabCfg));
    pProdRow->addWidget(m_pSpinMaxAngle);
    pForm->addRow(QStringLiteral("尺寸 宽/高 (mm)"), pProdRow);

    m_pSpinTolMm = makeSpin(0, 1000, 3, pTabCfg);
    m_pSpinTolMm->setSuffix(QStringLiteral(" mm"));
    m_pSpinTolDeg = makeSpin(0, 180, 3, pTabCfg);
    m_pSpinTolDeg->setSuffix(QStringLiteral(" °"));
    auto* pTolRow = new QHBoxLayout();
    pTolRow->addWidget(m_pSpinTolMm);
    pTolRow->addWidget(m_pSpinTolDeg);
    pForm->addRow(QStringLiteral("合格容差 mm/°"), pTolRow);

    auto* pMarkTextRow = new QHBoxLayout();
    m_pEditMarkText = new QLineEdit(pTabCfg);
    m_pEditMarkText->setPlaceholderText(QStringLiteral("如 TOP (空则无字)"));
    m_pEditMarkText->setToolTip(QStringLiteral("产品正面纯白丝印字符; 反面时无字符, 供黑白相机正反识别"));
    m_pSpinMarkTextH = makeSpin(0.5, 50.0, 1, pTabCfg);
    m_pSpinMarkTextH->setSingleStep(0.5);
    m_pSpinMarkTextH->setSuffix(QStringLiteral(" mm"));
    m_pSpinMarkTextH->setKeyboardTracking(true);
    m_pChkRandomFlip = new QCheckBox(QStringLiteral("随机含反面"), pTabCfg);
    pMarkTextRow->addWidget(m_pEditMarkText, 1);
    pMarkTextRow->addWidget(m_pSpinMarkTextH);
    pMarkTextRow->addWidget(m_pChkRandomFlip);
    pForm->addRow(QStringLiteral("丝印字符/字高"), pMarkTextRow);

    m_pChkDualGripper = new QCheckBox(QStringLiteral("启用双爪模式 (1号爪、2号爪先后抓取)"), pTabCfg);
    pForm->addRow(QStringLiteral("抓手模式"), m_pChkDualGripper);

    auto* pNozzle1Row = new QHBoxLayout();
    m_pSpinNozzle1X = makeSpin(-500, 500, 3, pTabCfg);
    m_pSpinNozzle1Y = makeSpin(-500, 500, 3, pTabCfg);
    m_pSpinNozzle1X->setSuffix(QStringLiteral(" mm"));
    m_pSpinNozzle1Y->setSuffix(QStringLiteral(" mm"));
    pNozzle1Row->addWidget(m_pSpinNozzle1X);
    pNozzle1Row->addWidget(m_pSpinNozzle1Y);
    pForm->addRow(QStringLiteral("1号爪偏移 X/Y"), pNozzle1Row);

    auto* pNozzle2Row = new QHBoxLayout();
    m_pSpinNozzle2X = makeSpin(-500, 500, 3, pTabCfg);
    m_pSpinNozzle2Y = makeSpin(-500, 500, 3, pTabCfg);
    m_pSpinNozzle2X->setSuffix(QStringLiteral(" mm"));
    m_pSpinNozzle2Y->setSuffix(QStringLiteral(" mm"));
    pNozzle2Row->addWidget(m_pSpinNozzle2X);
    pNozzle2Row->addWidget(m_pSpinNozzle2Y);
    pForm->addRow(QStringLiteral("2号爪偏移 X/Y"), pNozzle2Row);

    auto* pBtnApply = new QPushButton(QStringLiteral("应用视野与产品配置"), pTabCfg);
    pBtnApply->setProperty("primary", true);
    pForm->addRow(pBtnApply);
    pCfgTabs->addTab(pTabCfg, QStringLiteral("视野与双爪配置"));

    // ---- Tab 3: 手眼标定与光学退化模拟 ----
    auto* pTabCalib = new QWidget(pCfgTabs);
    auto* pCalibLayout = new QVBoxLayout(pTabCalib);
    pCalibLayout->setContentsMargins(8, 8, 8, 8);
    pCalibLayout->setSpacing(8);

    auto* pGrpCalibMark = new QGroupBox(QStringLiteral("拾取头标定 Mark 参数 (纯黑高对比度)"), pTabCalib);
    auto* pCalibForm = new QFormLayout(pGrpCalibMark);
    auto* pMarkRow = new QHBoxLayout();
    m_pComboCalibShape = new QComboBox(pGrpCalibMark);
    m_pComboCalibShape->addItems({ QStringLiteral("十字"), QStringLiteral("实心圆"), QStringLiteral("L 角"),
                                   QStringLiteral("实心方块"), QStringLiteral("圆环十字") });
    m_pSpinCalibSize = makeSpin(0.1, 1000, 2, pGrpCalibMark);
    m_pSpinCalibSize->setSingleStep(1.0);
    m_pSpinCalibSize->setSuffix(QStringLiteral(" mm"));
    m_pSpinCalibLineWidth = makeSpin(0.01, 50, 2, pGrpCalibMark);
    m_pSpinCalibLineWidth->setSingleStep(0.2);
    m_pSpinCalibLineWidth->setSuffix(QStringLiteral(" 线宽"));
    pMarkRow->addWidget(m_pComboCalibShape);
    pMarkRow->addWidget(m_pSpinCalibSize);
    pMarkRow->addWidget(m_pSpinCalibLineWidth);
    pCalibForm->addRow(QStringLiteral("图形/尺寸/线宽"), pMarkRow);

    auto* pOffRow = new QHBoxLayout();
    m_pComboCalibMount = new QComboBox(pGrpCalibMark);
    m_pComboCalibMount->addItems({ QStringLiteral("挂旋转中心"), QStringLiteral("挂1号爪"),
                                   QStringLiteral("挂2号爪"), QStringLiteral("挂双爪(1+2同时)") });
    m_pSpinCalibOffX = makeSpin(-200, 200, 3, pGrpCalibMark);
    m_pSpinCalibOffY = makeSpin(-200, 200, 3, pGrpCalibMark);
    m_pSpinCalibOffX->setSuffix(QStringLiteral(" mm"));
    m_pSpinCalibOffY->setSuffix(QStringLiteral(" mm"));
    pOffRow->addWidget(m_pComboCalibMount);
    pOffRow->addWidget(m_pSpinCalibOffX);
    pOffRow->addWidget(m_pSpinCalibOffY);
    pCalibForm->addRow(QStringLiteral("挂载与偏移 X/Y"), pOffRow);
    pCalibLayout->addWidget(pGrpCalibMark);

    auto* pGrpOptic = new QGroupBox(QStringLiteral("光学退化模拟 (测试黑白相机抗干扰能力)"), pTabCalib);
    auto* pOpticRow = new QHBoxLayout(pGrpOptic);
    m_pSpinNoise = new QSpinBox(pGrpOptic);
    m_pSpinNoise->setRange(0, 100);
    m_pSpinNoise->setSuffix(QStringLiteral(" %噪点"));
    m_pSpinBlur = new QSpinBox(pGrpOptic);
    m_pSpinBlur->setRange(0, 10);
    m_pSpinBlur->setSuffix(QStringLiteral(" 级失焦"));
    m_pChkDefect = new QCheckBox(QStringLiteral("模拟边缘瑕疵"), pGrpOptic);
    pOpticRow->addWidget(m_pSpinNoise);
    pOpticRow->addWidget(m_pSpinBlur);
    pOpticRow->addWidget(m_pChkDefect);
    pCalibLayout->addWidget(pGrpOptic);
    pCalibLayout->addStretch();
    pCfgTabs->addTab(pTabCalib, QStringLiteral("手眼标定与光学"));

    pRightLayout->addWidget(pCfgTabs, 1);

    pSplitter->addWidget(pLeftHost);
    pSplitter->addWidget(pRightPanel);
    pSplitter->setStretchFactor(0, 3);
    pSplitter->setStretchFactor(1, 2);
    pSplitter->setSizes({ 880, 500 });

    m_pRefreshTimer = new QTimer(this);
    m_pRefreshTimer->setSingleShot(true);
    m_pRefreshTimer->setInterval(50);

    // ---------------- 信号 ----------------
    connect(pBtnApply, &QPushButton::clicked, this, &PickPage::onApplyConfig);
    connect(m_pChkFixed, &QCheckBox::toggled, this, &PickPage::onDisplayChanged);
    connect(m_pChkShowStage, &QCheckBox::toggled, this, [this](bool bOn) {
        if (m_pChkHead && m_pChkHead->isChecked() != bOn) {
            m_pChkHead->setChecked(bOn);
        }
        onDisplayChanged();
    });
    connect(m_pChkCalibMark, &QCheckBox::toggled, this, [this](bool bOn) {
        // 20261026 ZJH 勾选「显示标定Mark」时自动隐藏产品与拾取头, 保证黑白相机只看到纯白底+纯黑标定 Mark; 取消时恢复产品显示
        {
            const QSignalBlocker b1(m_pChkProducts), b2(m_pChkShowStage);
            m_pChkProducts->setChecked(!bOn);
            m_pChkShowStage->setChecked(!bOn);
        }
        if (m_pChkHead) {
            m_pChkHead->setChecked(!bOn);
        }
        onDisplayChanged();
        if (bOn) {
            m_pScene->moveCalibMarkToFieldCenter();
        }
    });
    connect(pBtnCenterMark, &QPushButton::clicked, this, [this] {
        if (!m_pChkCalibMark->isChecked()) {
            m_pChkCalibMark->setChecked(true);  // 内部已触发 moveCalibMarkToFieldCenter
        } else {
            m_pScene->moveCalibMarkToFieldCenter();
        }
    });
    connect(m_pChkProducts, &QCheckBox::toggled, this, &PickPage::onDisplayChanged);
    connect(m_pComboCalibShape, qOverload<int>(&QComboBox::currentIndexChanged), this, &PickPage::onDisplayChanged);
    connect(m_pComboCalibMount, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        onDisplayChanged();
        if (m_pChkCalibMark->isChecked()) {
            m_pScene->moveCalibMarkToFieldCenter();
        }
    });
    connect(m_pSpinNoise, qOverload<int>(&QSpinBox::valueChanged), this, &PickPage::onDisplayChanged);
    connect(m_pSpinBlur, qOverload<int>(&QSpinBox::valueChanged), this, &PickPage::onDisplayChanged);
    connect(m_pChkDefect, &QCheckBox::toggled, this, &PickPage::onDisplayChanged);
    for (QDoubleSpinBox* pSpin : { m_pSpinPxPerMm, m_pSpinCalibSize, m_pSpinCalibLineWidth, m_pSpinCalibOffX, m_pSpinCalibOffY }) {
        connect(pSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PickPage::onDisplayChanged);
    }
    // 20261026 ZJH 修改产品丝印字符、字高、随机正反或双爪配置时即时写入场景
    connect(m_pEditMarkText, &QLineEdit::textChanged, this, &PickPage::onApplyConfig);
    connect(m_pSpinMarkTextH, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PickPage::onApplyConfig);
    connect(m_pChkRandomFlip, &QCheckBox::toggled, this, &PickPage::onApplyConfig);
    connect(m_pChkDualGripper, &QCheckBox::toggled, this, &PickPage::onApplyConfig);
    for (QDoubleSpinBox* pSpin : { m_pSpinNozzle1X, m_pSpinNozzle1Y, m_pSpinNozzle2X, m_pSpinNozzle2Y }) {
        connect(pSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PickPage::onApplyConfig);
    }
    connect(pBtnSpawn, &QPushButton::clicked, this, &PickPage::onSpawn);
    connect(pBtnConveyor, &QPushButton::clicked, this, [this] { m_pScene->stepConveyor(140.0, 2); });
    connect(m_pTableProducts, &QTableWidget::itemChanged, this, &PickPage::onProductEdited);
    connect(pBtnMoveTo, &QPushButton::clicked, this, &PickPage::onMoveToSelected);
    connect(pBtnFlipSel, &QPushButton::clicked, this, [this] {
        const int nRow = m_pTableProducts->currentRow();
        const std::vector<Product> vecProd = m_pScene->products();
        if (nRow >= 0 && nRow < static_cast<int>(vecProd.size())) {
            m_pScene->setProductFlipped(nRow, !vecProd[nRow].bFlipped);
        }
    });
    connect(pBtnPick, &QPushButton::clicked, this, &PickPage::onPick);
    connect(pBtnPlace, &QPushButton::clicked, this, &PickPage::onPlace);
    connect(pBtnHome, &QPushButton::clicked, m_pScene->head(), &Stage::home);
    connect(pBtnPop, &QPushButton::clicked, this, &PickPage::onPopout);
    connect(pBtnSaveSnap, &QPushButton::clicked, this, [this] {
        if (!m_lstViews.isEmpty() && m_lstViews.first()) {
            const QString strDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
            QDir().mkpath(strDir);
            const QString strPath = QStringLiteral("%1/拍屏_拾取_%2.png")
                .arg(strDir, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
            m_lstViews.first()->grab().save(strPath);
            m_pLabelTruth->setText(m_pLabelTruth->text() + QStringLiteral("\n[已保存拍屏图] ") + strPath);
        }
    });
    connect(m_pComboActiveNozzle, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int nIdx) {
        for (const QPointer<PickView>& p : m_lstViews) { if (p) p->setActiveNozzle(nIdx + 1); }
    });
    connect(pBtnReset, &QPushButton::clicked, m_pScene, &PickScene::resetStats);
    connect(m_pChkLabels, &QCheckBox::toggled, this, [this](bool bOn) {
        for (const QPointer<PickView>& p : m_lstViews) { if (p) p->setShowLabels(bOn); }
    });
    connect(m_pChkHead, &QCheckBox::toggled, this, [this](bool bOn) {
        for (const QPointer<PickView>& p : m_lstViews) { if (p) p->setShowHead(bOn); }
        if (m_pChkShowStage && m_pChkShowStage->isChecked() != bOn) {
            m_pChkShowStage->setChecked(bOn);
        }
    });
    connect(m_pRefreshTimer, &QTimer::timeout, this, &PickPage::refresh);
    // 20261024 ZJH 场景变化 (含拾取头运动) → 节流刷新
    // 20261026 ZJH 页签不可见时只记脏标记, 切回来再刷新
    connect(m_pScene, &PickScene::changed, this, [this] {
        if (!isVisible()) {
            m_bDirty = true;
        } else if (!m_pRefreshTimer->isActive()) {
            m_pRefreshTimer->start();
        }
    }, Qt::QueuedConnection);

    loadFromScene();
}

void PickPage::showEvent(QShowEvent* pEvent)
{
    QWidget::showEvent(pEvent);
    // 20261026 ZJH 隐藏期间场景有变化 → 显示时补一次刷新
    if (m_bDirty) {
        refresh();
    }
}

PickView* PickPage::createView(QWidget* pParent)
{
    auto* pView = new PickView(m_pScene, pParent);
    // 20261024 ZJH 新建视图继承当前显示选项 (首次创建时复选框尚未构造)
    if (m_pChkLabels) {
        pView->setShowLabels(m_pChkLabels->isChecked());
        pView->setShowHead(m_pChkHead->isChecked());
    }
    if (m_pComboActiveNozzle) {
        pView->setActiveNozzle(m_pComboActiveNozzle->currentIndex() + 1);
    }
    connect(pView, &PickView::productSelected, this, [this](int nIdx) {
        if (m_pTableProducts && nIdx >= 0 && nIdx < m_pTableProducts->rowCount()) {
            m_pTableProducts->selectRow(nIdx);
        }
    });
    m_lstViews.append(pView);
    return pView;  // 返回视图
}

void PickPage::setYDown(bool bYDown)
{
    PickDisplay disp = m_pScene->display();
    disp.bYDown = bYDown;
    m_pScene->setDisplay(disp);  // 视图从场景读取并重绘
}

void PickPage::onDisplayChanged()
{
    PickDisplay disp = m_pScene->display();  // 保留 Y 轴等其他字段
    disp.bFixedScale = m_pChkFixed->isChecked();
    disp.dPxPerMm = m_pSpinPxPerMm->value();
    disp.bShowStage = m_pChkShowStage->isChecked();
    disp.bShowCalibMark = m_pChkCalibMark->isChecked();
    disp.bShowProducts = m_pChkProducts->isChecked();
    disp.nCalibShape = m_pComboCalibShape->currentIndex();
    disp.dCalibSize = m_pSpinCalibSize->value();
    disp.dCalibLineWidth = m_pSpinCalibLineWidth->value();
    disp.nCalibMount = m_pComboCalibMount->currentIndex();
    disp.dCalibOffsetX = m_pSpinCalibOffX->value();
    disp.dCalibOffsetY = m_pSpinCalibOffY->value();
    disp.nNoiseLevel = m_pSpinNoise->value();
    disp.nBlurLevel = m_pSpinBlur->value();
    disp.bSimulateDefect = m_pChkDefect->isChecked();
    m_pScene->setDisplay(disp);
}

void PickPage::loadFromScene()
{
    const PickSceneConfig cfg = m_pScene->config();
    const QSignalBlocker bText(m_pEditMarkText), bTextH(m_pSpinMarkTextH), bFlip(m_pChkRandomFlip),
        bDual(m_pChkDualGripper), bN1X(m_pSpinNozzle1X), bN1Y(m_pSpinNozzle1Y), bN2X(m_pSpinNozzle2X), bN2Y(m_pSpinNozzle2Y);
    m_pSpinFieldW->setValue(cfg.dFieldW);
    m_pSpinFieldH->setValue(cfg.dFieldH);
    m_pComboShape->setCurrentIndex(static_cast<int>(cfg.eShape));
    m_pSpinProdW->setValue(cfg.dProductW);
    m_pSpinProdH->setValue(cfg.dProductH);
    m_pSpinCount->setValue(cfg.nCount);
    m_pSpinMaxAngle->setValue(cfg.dMaxAngleDeg);
    m_pSpinTolMm->setValue(cfg.dPickToleranceMm);
    m_pSpinTolDeg->setValue(cfg.dPickToleranceDeg);
    m_pEditMarkText->setText(cfg.strMarkText);
    m_pSpinMarkTextH->setValue(cfg.dMarkTextHeightMm);
    m_pChkRandomFlip->setChecked(cfg.bRandomFlip);
    m_pChkDualGripper->setChecked(cfg.bDualGripper);
    m_pSpinNozzle1X->setValue(cfg.dNozzle1OffX);
    m_pSpinNozzle1Y->setValue(cfg.dNozzle1OffY);
    m_pSpinNozzle2X->setValue(cfg.dNozzle2OffX);
    m_pSpinNozzle2Y->setValue(cfg.dNozzle2OffY);
    for (QWidget* pW : { static_cast<QWidget*>(m_pSpinNozzle1X), static_cast<QWidget*>(m_pSpinNozzle1Y),
                         static_cast<QWidget*>(m_pSpinNozzle2X), static_cast<QWidget*>(m_pSpinNozzle2Y),
                         static_cast<QWidget*>(m_pComboActiveNozzle) }) {
        pW->setEnabled(cfg.bDualGripper);
    }
    if (!cfg.bDualGripper) {
        m_pComboActiveNozzle->setCurrentIndex(0);
    }
    refresh();
}

void PickPage::onApplyConfig()
{
    PickSceneConfig cfg;
    cfg.dFieldW = m_pSpinFieldW->value();
    cfg.dFieldH = m_pSpinFieldH->value();
    cfg.eShape = static_cast<ProductShape>(m_pComboShape->currentIndex());
    cfg.dProductW = m_pSpinProdW->value();
    cfg.dProductH = m_pSpinProdH->value();
    cfg.nCount = m_pSpinCount->value();
    cfg.dMaxAngleDeg = m_pSpinMaxAngle->value();
    cfg.dPickToleranceMm = m_pSpinTolMm->value();
    cfg.dPickToleranceDeg = m_pSpinTolDeg->value();
    cfg.strMarkText = m_pEditMarkText->text();
    cfg.dMarkTextHeightMm = m_pSpinMarkTextH->value();
    cfg.bRandomFlip = m_pChkRandomFlip->isChecked();
    cfg.bDualGripper = m_pChkDualGripper->isChecked();
    cfg.dNozzle1OffX = m_pSpinNozzle1X->value();
    cfg.dNozzle1OffY = m_pSpinNozzle1Y->value();
    cfg.dNozzle2OffX = m_pSpinNozzle2X->value();
    cfg.dNozzle2OffY = m_pSpinNozzle2Y->value();
    for (QWidget* pW : { static_cast<QWidget*>(m_pSpinNozzle1X), static_cast<QWidget*>(m_pSpinNozzle1Y),
                         static_cast<QWidget*>(m_pSpinNozzle2X), static_cast<QWidget*>(m_pSpinNozzle2Y),
                         static_cast<QWidget*>(m_pComboActiveNozzle) }) {
        pW->setEnabled(cfg.bDualGripper);
    }
    if (!cfg.bDualGripper) {
        m_pComboActiveNozzle->setCurrentIndex(0);
    }
    m_pScene->setConfig(cfg);
}

void PickPage::onSpawn()
{
    onApplyConfig();             // 先应用配置, 确保数量/尺寸是界面上的值
    m_pScene->spawnProducts();
}

void PickPage::onProductEdited(QTableWidgetItem* pItem)
{
    // 20261024 ZJH 程序填表时忽略
    if (m_bFilling || !pItem) {
        return;
    }
    const int nRow = pItem->row();
    // 20261026 ZJH 第 4 列为正反复选框: 勾选=反面, 未勾选=正面
    if (pItem->column() == 4) {
        const bool bFlipped = (pItem->checkState() == Qt::Checked);
        if (!m_pScene->setProductFlipped(nRow, bFlipped)) {
            m_pRefreshTimer->start();
        }
        return;
    }
    if (pItem->column() < 1 || pItem->column() > 3) {
        return;
    }
    auto cell = [this, nRow](int nCol) {
        const QTableWidgetItem* p = m_pTableProducts->item(nRow, nCol);
        return p ? p->text().toDouble() : 0.0;
    };
    // 20261024 ZJH 被吸着的产品不能编辑, setProductPose 返回 false 后刷新还原
    if (!m_pScene->setProductPose(nRow, Pose{ cell(1), cell(2), cell(3) })) {
        m_pRefreshTimer->start();
    }
}

void PickPage::onMoveToSelected()
{
    const int nRow = m_pTableProducts->currentRow();
    const std::vector<Product> vecProd = m_pScene->products();
    // 20261024 ZJH 未选中产品
    if (nRow < 0 || nRow >= static_cast<int>(vecProd.size())) {
        return;
    }
    // 20261026 ZJH 计算使当前选中爪 (1号爪或2号爪) 对准目标产品时拾取头应走的位姿
    const int nNozzle = m_pScene->config().bDualGripper ? (m_pComboActiveNozzle->currentIndex() + 1) : 1;
    const Pose poseCmd = m_pScene->headPoseForNozzleTarget(nNozzle, vecProd[nRow].pose);
    m_pScene->head()->movePoseAbs(poseCmd);
}

void PickPage::onPick()
{
    const int nNozzle = m_pScene->config().bDualGripper ? (m_pComboActiveNozzle->currentIndex() + 1) : 1;
    m_pScene->pick(nNozzle);
    // 20261026 ZJH 双爪模式下: 1号爪吸起成功后自动切换到 2号爪, 方便用户连续点击抓取第 2 个产品
    if (m_pScene->config().bDualGripper && nNozzle == 1 && m_pScene->isHolding(1) && !m_pScene->isHolding(2)) {
        m_pComboActiveNozzle->setCurrentIndex(1);
    }
}

void PickPage::onPlace()
{
    m_pScene->place(0);
    if (m_pScene->config().bDualGripper) {
        m_pComboActiveNozzle->setCurrentIndex(0);
    }
}

void PickPage::onPopout()
{
    auto* pWin = new ViewWindow(createView(nullptr), QStringLiteral("大视野拾取"), nullptr);
    pWin->show();
}

void PickPage::refresh()
{
    m_bDirty = false;
    const std::vector<Product> vecProd = m_pScene->products();
    // 20261024 ZJH 用户正在编辑单元格: 焦点落在表格内部的编辑器上 (state() 为 protected, 用焦点判断)
    const QWidget* pFocus = QApplication::focusWidget();
    const bool bEditing = pFocus && pFocus != m_pTableProducts && pFocus != m_pTableProducts->viewport()
                       && m_pTableProducts->isAncestorOf(pFocus);
    m_bFilling = true;  // 屏蔽 itemChanged
    const int nOldRow = m_pTableProducts->currentRow();
    // 20261024 ZJH 编辑中不改行数 (改行数会关闭编辑器)
    if (!bEditing) {
        m_pTableProducts->setRowCount(static_cast<int>(vecProd.size()));
    }
    const int nRows = std::min(m_pTableProducts->rowCount(), static_cast<int>(vecProd.size()));  // 编辑中行数可能未同步
    for (int i = 0; i < nRows; ++i) {  // 逐行填产品
        const Product& p = vecProd[i];
        QString strState;
        if (p.bPicked) {
            strState = m_pScene->config().bDualGripper
                           ? QStringLiteral("%1号爪已吸").arg(p.nPickedByNozzle > 0 ? p.nPickedByNozzle : 1)
                           : QStringLiteral("已吸起");
        } else {
            strState = p.bPlaced ? QStringLiteral("已放下") : QStringLiteral("待拾取");
        }
        const QString strSide = p.bFlipped ? QStringLiteral("反面") : QStringLiteral("正面");
        const QStringList lst = { QString::number(p.nId), QString::number(p.pose.dX, 'f', 3),
                                  QString::number(p.pose.dY, 'f', 3), QString::number(p.pose.dThetaDeg, 'f', 3),
                                  strSide, strState };
        for (int nCol = 0; nCol < lst.size(); ++nCol) {
            QTableWidgetItem* pItem = m_pTableProducts->item(i, nCol);
            if (!pItem) {
                pItem = new QTableWidgetItem();
                // 20261024 ZJH 编号与状态列只读; 正反列用复选框切换正/反面
                if (nCol == 0 || nCol == 5) {
                    pItem->setFlags(pItem->flags() & ~Qt::ItemIsEditable);
                } else if (nCol == 4) {
                    pItem->setFlags((pItem->flags() & ~Qt::ItemIsEditable) | Qt::ItemIsUserCheckable);
                }
                m_pTableProducts->setItem(i, nCol, pItem);
            }
            if (nCol == 4) {
                pItem->setCheckState(p.bFlipped ? Qt::Checked : Qt::Unchecked);
                pItem->setForeground(p.bFlipped ? QColor(200, 40, 40) : palette().color(QPalette::Text));
            }
            // 20261024 ZJH 用户正在编辑表格时不覆盖文本, 避免冲掉输入
            if (!bEditing && pItem->text() != lst[nCol]) {
                pItem->setText(lst[nCol]);
            }
        }
    }
    if (nOldRow >= 0 && nOldRow < m_pTableProducts->rowCount()) {
        m_pTableProducts->selectRow(nOldRow);  // 保持选中行
    }
    m_bFilling = false;

    const PickResult res = m_pScene->lastResult();
    const PickStats st = m_pScene->stats();
    const PickSceneConfig cfg = m_pScene->config();
    const Pose poseN1 = m_pScene->nozzlePose(1);
    QString strText;
    if (!cfg.bDualGripper) {
        strText = QStringLiteral("吸嘴  X=%1  Y=%2  θ=%3°\n")
                      .arg(poseN1.dX, 0, 'f', 3).arg(poseN1.dY, 0, 'f', 3).arg(poseN1.dThetaDeg, 0, 'f', 3);
    } else {
        const Pose poseN2 = m_pScene->nozzlePose(2);
        strText = QStringLiteral("1号爪 X=%1  Y=%2  θ=%3°  [%4]\n2号爪 X=%5  Y=%6  θ=%7°  [%8]\n")
                      .arg(poseN1.dX, 0, 'f', 3).arg(poseN1.dY, 0, 'f', 3).arg(poseN1.dThetaDeg, 0, 'f', 3)
                      .arg(m_pScene->isHolding(1) ? QStringLiteral("已吸") : QStringLiteral("空"))
                      .arg(poseN2.dX, 0, 'f', 3).arg(poseN2.dY, 0, 'f', 3).arg(poseN2.dThetaDeg, 0, 'f', 3)
                      .arg(m_pScene->isHolding(2) ? QStringLiteral("已吸") : QStringLiteral("空"));
    }
    // 20261024 ZJH 最近一次结果
    if (res.bHit) {
        const QString strSideTag = res.bFlipped ? QStringLiteral("反面(镜像)") : QStringLiteral("正面");
        const QString strNozzleTag = cfg.bDualGripper ? QStringLiteral("%1号爪 ").arg(res.nNozzle) : QString();
        strText += QStringLiteral("最近  %1产品#%2 [%3]  %4\n      ΔX=%5  ΔY=%6  |Δ|=%7  Δθ=%8°\n")
                       .arg(strNozzleTag).arg(res.nProductId).arg(strSideTag)
                       .arg(res.bInTolerance ? QStringLiteral("合格") : (res.bFlipped ? QStringLiteral("NG(反面)") : QStringLiteral("超差")))
                       .arg(res.dX, 0, 'f', 4).arg(res.dY, 0, 'f', 4).arg(res.dDist, 0, 'f', 4).arg(res.dThetaDeg, 0, 'f', 4);
    } else if (st.nAttempts > 0) {
        strText += QStringLiteral("最近  %1未吸到产品\n")
                       .arg(cfg.bDualGripper ? QStringLiteral("%1号爪 ").arg(res.nNozzle) : QString());
    }
    // 20261024 ZJH 统计 (除零保护)
    const double dAvgDist = st.nHits > 0 ? st.dSumDist / st.nHits : 0.0;
    const double dAvgTheta = st.nHits > 0 ? st.dSumAbsTheta / st.nHits : 0.0;
    strText += QStringLiteral("统计  拾取 %1 次  吸到 %2  合格 %3  误吸反面 %4\n      距离 平均 %5 最大 %6 mm\n      角度 平均 %7 最大 %8 °")
                   .arg(st.nAttempts).arg(st.nHits).arg(st.nInTolerance).arg(st.nFlippedPicked)
                   .arg(dAvgDist, 0, 'f', 4).arg(st.dMaxDist, 0, 'f', 4).arg(dAvgTheta, 0, 'f', 4).arg(st.dMaxAbsTheta, 0, 'f', 4);
    m_pLabelResult->setText(strText);

    // 20261025 ZJH 显示控件与场景同步 (脚本 ShowPickCalibMark/ShowProducts 会改场景配置)
    const PickDisplay disp = m_pScene->display();
    {
        const QSignalBlocker b1(m_pChkFixed), b2(m_pSpinPxPerMm), b3(m_pChkCalibMark), b4(m_pChkProducts),
            b5(m_pComboCalibShape), b6(m_pSpinCalibSize), b7(m_pSpinCalibLineWidth), b8(m_pSpinCalibOffX), b9(m_pSpinCalibOffY),
            b10(m_pComboCalibMount), b11(m_pSpinNoise), b12(m_pChkDefect), b13(m_pSpinBlur), b14(m_pChkShowStage);
        m_pChkFixed->setChecked(disp.bFixedScale);
        m_pChkShowStage->setChecked(disp.bShowStage);
        m_pChkCalibMark->setChecked(disp.bShowCalibMark);
        m_pChkProducts->setChecked(disp.bShowProducts);
        m_pComboCalibShape->setCurrentIndex(disp.nCalibShape);
        m_pComboCalibMount->setCurrentIndex(std::clamp(disp.nCalibMount, 0, 3));
        m_pSpinNoise->setValue(disp.nNoiseLevel);
        m_pSpinBlur->setValue(disp.nBlurLevel);
        m_pChkDefect->setChecked(disp.bSimulateDefect);
        // 20261025 ZJH 输入框有焦点时不回写, 避免打断用户输入
        auto syncSpin = [](QDoubleSpinBox* pSpin, double dValue) { if (!pSpin->hasFocus()) pSpin->setValue(dValue); };
        syncSpin(m_pSpinPxPerMm, disp.dPxPerMm);
        syncSpin(m_pSpinCalibSize, disp.dCalibSize);
        syncSpin(m_pSpinCalibLineWidth, disp.dCalibLineWidth);
        syncSpin(m_pSpinCalibOffX, disp.dCalibOffsetX);
        syncSpin(m_pSpinCalibOffY, disp.dCalibOffsetY);
    }

    // 20261025 ZJH 标定真值: 拾取头旋转中心与标定 Mark (视野坐标)
    Stage* pHead = m_pScene->head();
    const Pose poseCenter = m_pScene->nozzlePose(0);
    const Pose poseMark = m_pScene->calibMarkPose();
    QString strTruth = QStringLiteral("真值  显示 %1  Y轴%2\n")
        .arg(disp.bFixedScale ? QStringLiteral("固定 %1 px/mm").arg(disp.dPxPerMm, 0, 'f', 3) : QStringLiteral("<自适应, 不可标定>"))
        .arg(disp.bYDown ? QStringLiteral("朝下") : QStringLiteral("朝上"));
    strTruth += QStringLiteral("      拾取头 %1  旋转中心(当前) X=%2 Y=%3\n")
        .arg(pHead->geometry().eType == StageType::UVW ? QStringLiteral("UVW") : QStringLiteral("XYD"))
        .arg(poseCenter.dX, 0, 'f', 3).arg(poseCenter.dY, 0, 'f', 3);
    strTruth += QStringLiteral("      标定Mark X=%1 Y=%2  到旋转中心 %3 mm")
        .arg(poseMark.dX, 0, 'f', 3).arg(poseMark.dY, 0, 'f', 3)
        .arg(std::hypot(disp.dCalibOffsetX, disp.dCalibOffsetY), 0, 'f', 3);
    QPointF ptPx;
    // 20261025 ZJH 固定比例时给出标定 Mark 相对视野中心的屏幕像素
    if (m_pScene->calibMarkScreenOffset(ptPx)) {
        strTruth += QStringLiteral("\n      标定Mark 屏幕 (%1, %2) px 相对视野中心  视野 %3×%4 px")
            .arg(ptPx.x(), 0, 'f', 1).arg(ptPx.y(), 0, 'f', 1)
            .arg(cfg.dFieldW * disp.dPxPerMm, 0, 'f', 0).arg(cfg.dFieldH * disp.dPxPerMm, 0, 'f', 0);
    }
    m_pLabelTruth->setText(strTruth);
}
