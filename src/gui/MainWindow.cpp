// 20261024 ZJH MainWindow 实现
#include "gui/MainWindow.h"

#include "comm/PlcServer.h"
#include "core/RegisterBank.h"
#include "gui/AlignPage.h"
#include "gui/AlignView.h"
#include "gui/PickPage.h"
#include "gui/PickView.h"
#include "gui/RegisterTableWidget.h"
#include "gui/ScriptPage.h"
#include "gui/StageConfigWidget.h"
#include "motion/Stage.h"
#include "script/LuaEngine.h"
#include "sim/AlignScene.h"
#include "sim/PickScene.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int s_nLogMaxLines = 3000;  // 20261026 ZJH 日志框最大行数

// 20261026 ZJH 全局现代工业浅色主题样式表 (清爽卡片、圆角控件、高辨识度页签与表格)
const QString s_strAppStyle = QStringLiteral(R"(
QMainWindow, QWidget#centralHost {
    background-color: #F1F5F9;
    color: #0F172A;
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 13px;
}
QMenuBar {
    background-color: #FFFFFF;
    border-bottom: 1px solid #E2E8F0;
    padding: 2px 6px;
}
QMenuBar::item:selected {
    background-color: #EFF6FF;
    color: #1D4ED8;
    border-radius: 4px;
}
QFrame#topCommCard, QFrame#viewToolbarCard, QFrame#logCard {
    background-color: #FFFFFF;
    border: 1px solid #CBD5E1;
    border-radius: 8px;
}
QFrame#telemetryCard {
    background-color: #F8FAFC;
    border: 1px solid #CBD5E1;
    border-radius: 6px;
}
QGroupBox {
    background-color: #FFFFFF;
    border: 1px solid #DCE3EC;
    border-radius: 8px;
    margin-top: 9px;
    padding: 10px 8px 8px 8px;
    font-weight: 600;
    color: #1E293B;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    color: #0F172A;
}
QTabWidget::pane {
    background-color: #FFFFFF;
    border: 1px solid #CBD5E1;
    border-radius: 8px;
    top: -1px;
}
QTabBar::tab {
    background-color: #E2E8F0;
    color: #475569;
    border: 1px solid #CBD5E1;
    border-bottom: none;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
    padding: 7px 18px;
    margin-right: 4px;
    font-weight: 600;
}
QTabBar::tab:hover {
    background-color: #F8FAFC;
    color: #1E293B;
}
QTabBar::tab:selected {
    background-color: #FFFFFF;
    color: #2563EB;
    border-top: 3px solid #2563EB;
    padding-top: 5px;
}
QPushButton {
    background-color: #F8FAFC;
    color: #1E293B;
    border: 1px solid #CBD5E1;
    border-radius: 6px;
    padding: 5px 12px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #EFF6FF;
    border-color: #93C5FD;
    color: #1D4ED8;
}
QPushButton:pressed {
    background-color: #DBEAFE;
}
QPushButton[primary="true"] {
    background-color: #2563EB;
    color: #FFFFFF;
    border: 1px solid #1D4ED8;
    font-weight: 600;
}
QPushButton[primary="true"]:hover {
    background-color: #1D4ED8;
    border-color: #1E40AF;
    color: #FFFFFF;
}
QPushButton[danger="true"] {
    background-color: #FEF2F2;
    color: #DC2626;
    border: 1px solid #FECACA;
    font-weight: 600;
}
QPushButton[danger="true"]:hover {
    background-color: #FEE2E2;
    border-color: #FCA5A5;
}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #FFFFFF;
    color: #0F172A;
    border: 1px solid #CBD5E1;
    border-radius: 5px;
    padding: 4px 7px;
    min-height: 20px;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border: 1px solid #2563EB;
    background-color: #FFFFFF;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QTableWidget {
    background-color: #FFFFFF;
    alternate-background-color: #F8FAFC;
    gridline-color: #E2E8F0;
    border: 1px solid #CBD5E1;
    border-radius: 6px;
    selection-background-color: #DBEAFE;
    selection-color: #1E3A8A;
}
QHeaderView::section {
    background-color: #F1F5F9;
    color: #334155;
    font-weight: 600;
    border: none;
    border-right: 1px solid #E2E8F0;
    border-bottom: 1px solid #CBD5E1;
    padding: 5px 6px;
}
QScrollArea {
    border: none;
    background: transparent;
}
QSplitter::handle {
    background-color: #E2E8F0;
}
QSplitter::handle:hover {
    background-color: #93C5FD;
}
)");
}

MainWindow::MainWindow(QWidget* pParent)
    : QMainWindow(pParent)
{
    setWindowTitle(QStringLiteral("AlignSim 大视野拾取 / 对位模拟工具 v1.0"));
    resize(1400, 900);

    // 20261024 ZJH 先读配置 (失败用默认值), 再创建核心对象
    QString strErr;
    const bool bLoaded = AppConfig::load(AppConfig::defaultPath(), m_settings, strErr);

    m_pBank = new RegisterBank(this);
    m_pAlignStage = new Stage(QStringLiteral("align"), this);
    m_pPickStage = new Stage(QStringLiteral("pick"), this);
    m_pAlignScene = new AlignScene(m_pAlignStage, this);
    m_pPickScene = new PickScene(m_pPickStage, this);
    m_pServer = new PlcServer(m_pBank, this);
    // 20261025 ZJH 脚本可 require scripts 下的模块; 标定记录写到 <exe目录>/logs
    m_pEngine = new LuaEngine(LuaContext{ m_pBank, m_pAlignStage, m_pPickStage, m_pAlignScene, m_pPickScene,
                                          ScriptPage::scriptDir(),
                                          QCoreApplication::applicationDirPath() + QStringLiteral("/logs") }, this);

    applySettingsToCore();
    m_pPickScene->spawnProducts();  // 启动即摆放一批产品
    buildUi();

    // 20261024 ZJH 配置读取结果写日志
    if (bLoaded) {
        appendLog(QStringLiteral("已加载配置 %1").arg(AppConfig::defaultPath()), false);
    } else if (!strErr.isEmpty()) {
        appendLog(strErr + QStringLiteral(" —— 已使用默认配置"), true);
    }
    // 20261024 ZJH 自动开始监听
    if (m_settings.bAutoStart) {
        onToggleServer();
    }
}

MainWindow::~MainWindow()
{
    // 20261024 ZJH 先停脚本并等待, 防止工作线程访问已析构的场景/平台
    m_pEngine->stopAll();
    m_pEngine->waitForDone(3000);
    // 20261024 ZJH 本析构函数结束后子对象才被删除; PlcServer 析构时 stop() 会发信号,
    // 若仍连着本窗口的槽会调用到已析构的 MainWindow (Qt 断言). 所以趁窗口完整时先停止并断开
    m_pServer->stop();
    m_pServer->disconnect(this);
    m_pEngine->disconnect(this);
}

bool MainWindow::runScriptFile(const QString& strName)
{
    QFile file(ScriptPage::scriptDir() + QStringLiteral("/%1.lua").arg(strName));
    // 20261024 ZJH 脚本文件不存在或无法读取
    if (!file.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("找不到脚本 %1").arg(file.fileName()), true);
        return false;
    }
    QString strErr;
    if (!m_pEngine->start(strName, QString::fromUtf8(file.readAll()), strErr)) {
        appendLog(QStringLiteral("启动脚本失败: %1").arg(strErr), true);
        return false;
    }
    return true;  // 返回已启动
}

void MainWindow::grabAllTabs(const QString& strDir)
{
    QDir().mkpath(strDir);
    flushLog();  // 20261026 ZJH 截图前把排队日志写进日志框
    for (int i = 0; i < m_pTabs->count(); ++i) {  // 逐页签截图
        m_pTabs->setCurrentIndex(i);
        QApplication::processEvents();  // 让排队的重绘/布局先执行
        grab().save(QStringLiteral("%1/tab%2.png").arg(strDir).arg(i));
    }
}

void MainWindow::applySettingsToCore()
{
    m_pAlignStage->setGeometry(m_settings.alignStage.geo);
    m_pAlignStage->setUnitPowers(m_settings.alignStage.nLinearPower, m_settings.alignStage.nAnglePower);
    m_pPickStage->setGeometry(m_settings.pickStage.geo);
    m_pPickStage->setUnitPowers(m_settings.pickStage.nLinearPower, m_settings.pickStage.nAnglePower);
    m_pAlignStage->setErrorModel(m_settings.alignStage.err);  // 20261025 ZJH 误差注入
    m_pPickStage->setErrorModel(m_settings.pickStage.err);
    // 20261024 ZJH 配置里有相机才覆盖默认预设
    if (!m_settings.vecCameras.empty()) {
        m_pAlignScene->setCameras(m_settings.vecCameras);
        m_pAlignScene->setMarks(m_settings.vecMarks);
    }
    m_pAlignScene->setStyle(m_settings.markStyle);
    m_pPickScene->setConfig(m_settings.pickConfig);
    // 20261025 ZJH 拍屏显示: Y 轴方向统一取界面开关, 运行态开关 (显示目标/对象/产品/标定Mark) 每次启动复位
    AlignDisplay alignDisp = m_settings.alignDisplay;
    alignDisp.bYDown = m_settings.bViewYDown;
    alignDisp.bShowTarget = true;
    alignDisp.bShowObject = true;
    m_pAlignScene->setDisplay(alignDisp);
    PickDisplay pickDisp = m_settings.pickDisplay;
    pickDisp.bYDown = m_settings.bViewYDown;
    pickDisp.bShowProducts = true;
    pickDisp.bShowCalibMark = false;
    m_pPickScene->setDisplay(pickDisp);
}

void MainWindow::collectSettingsFromCore()
{
    m_settings.eProtocol = static_cast<ProtocolType>(m_pComboProtocol->currentIndex());
    m_settings.bClientMode = (m_pComboRole && m_pComboRole->currentIndex() == 1);
    m_settings.bClientMasterPoll = (m_pChkMasterPoll && m_pChkMasterPoll->isChecked());
    m_settings.strIp = m_pEditIp->text().trimmed();
    m_settings.nPort = m_pSpinPort->value();
    m_settings.alignStage = StageSettings{ m_pAlignStage->geometry(), m_pAlignStage->linearPower(), m_pAlignStage->anglePower(),
                                           m_pAlignStage->errorModel() };
    m_settings.pickStage = StageSettings{ m_pPickStage->geometry(), m_pPickStage->linearPower(), m_pPickStage->anglePower(),
                                          m_pPickStage->errorModel() };
    m_settings.vecCameras = m_pAlignScene->cameras();
    m_settings.vecMarks = m_pAlignScene->marks();
    m_settings.markStyle = m_pAlignScene->style();
    m_settings.pickConfig = m_pPickScene->config();
    m_settings.alignDisplay = m_pAlignScene->display();
    m_settings.pickDisplay = m_pPickScene->display();
    m_settings.bViewYDown = m_pChkYDown->isChecked();
    m_settings.nRegisterViewStart = m_pRegTable->startAddress();
}

void MainWindow::buildUi()
{
    setStyleSheet(s_strAppStyle);

    auto* pCentral = new QWidget(this);
    pCentral->setObjectName(QStringLiteral("centralHost"));
    auto* pMain = new QVBoxLayout(pCentral);
    pMain->setContentsMargins(10, 8, 10, 8);
    pMain->setSpacing(8);
    setCentralWidget(pCentral);

    // ---------------- 菜单 ----------------
    QMenu* pMenuFile = menuBar()->addMenu(QStringLiteral("文件"));
    pMenuFile->addAction(QStringLiteral("保存配置"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &MainWindow::onSaveConfig);
    pMenuFile->addSeparator();
    pMenuFile->addAction(QStringLiteral("退出"), this, &QWidget::close);

    // ---------------- 顶部通信与全局状态栏 (卡片化) ----------------
    auto* pCommCard = new QFrame(pCentral);
    pCommCard->setObjectName(QStringLiteral("topCommCard"));
    auto* pCommBar = new QHBoxLayout(pCommCard);
    pCommBar->setContentsMargins(12, 6, 12, 6);
    pCommBar->setSpacing(8);

    m_pComboRole = new QComboBox(pCommCard);
    m_pComboRole->addItems({ QStringLiteral("服务端 (被连监听)"), QStringLiteral("客户端 (主动连接)") });
    m_pComboRole->setCurrentIndex(m_settings.bClientMode ? 1 : 0);
    m_pComboRole->setToolTip(QStringLiteral("选择本软件作为 TCP 服务端监听等待外部连接, 或作为 TCP 客户端主动连接外部服务端"));
    m_pComboProtocol = new QComboBox(pCommCard);
    m_pComboProtocol->addItems(ProtocolFactory::displayNames());
    m_pComboProtocol->setCurrentIndex(static_cast<int>(m_settings.eProtocol));
    m_pEditIp = new QLineEdit(m_settings.strIp, pCommCard);
    m_pEditIp->setMaximumWidth(135);
    m_pEditIp->setToolTip(QStringLiteral("服务端模式: 0.0.0.0 监听所有网卡; 客户端模式: 填写远端服务端 IP (如 127.0.0.1 或 192.168.1.10)"));
    m_pSpinPort = new QSpinBox(pCommCard);
    m_pSpinPort->setRange(1, 65535);
    m_pSpinPort->setValue(m_settings.nPort);
    m_pBtnServer = new QPushButton(pCommCard);
    m_pBtnServer->setProperty("primary", true);
    m_pChkMasterPoll = new QCheckBox(QStringLiteral("改值自动写远端"), pCommCard);
    m_pChkMasterPoll->setChecked(m_settings.bClientMasterPoll);
    m_pChkMasterPoll->setVisible(m_settings.bClientMode);
    m_pChkMasterPoll->setToolTip(QStringLiteral("客户端模式下勾选后: 仅当本机寄存器值被修改时单次写入远端(无后台轮询心跳); 同时始终支持响应远端主动发来的读写指令"));
    m_pLabelServer = new QLabel(pCommCard);
    m_pChkYDown = new QCheckBox(QStringLiteral("视图 Y 轴朝下"), pCommCard);
    m_pChkYDown->setChecked(m_settings.bViewYDown);
    m_pChkAutoSnap = new QCheckBox(QStringLiteral("触发(D100=1)自动存图"), pCommCard);
    m_pChkAutoSnap->setToolTip(QStringLiteral("勾选后每当 D100 从 0 变为 1 (触发拍照) 时, 自动把当前对位/拾取图像保存到 logs/触发存图/ 目录"));
    auto* pBtnSaveCfg = new QPushButton(QStringLiteral("保存配置"), pCommCard);
    pBtnSaveCfg->setToolTip(QStringLiteral("立即保存当前所有通信、相机、Mark与平台配置到 config/app_config.json (Ctrl+Shift+S)"));

    pCommBar->addWidget(new QLabel(QStringLiteral("通信角色"), pCommCard));
    pCommBar->addWidget(m_pComboRole);
    pCommBar->addWidget(new QLabel(QStringLiteral("协议"), pCommCard));
    pCommBar->addWidget(m_pComboProtocol);
    pCommBar->addWidget(new QLabel(QStringLiteral("IP"), pCommCard));
    pCommBar->addWidget(m_pEditIp);
    pCommBar->addWidget(new QLabel(QStringLiteral("端口"), pCommCard));
    pCommBar->addWidget(m_pSpinPort);
    pCommBar->addWidget(m_pBtnServer);
    pCommBar->addWidget(m_pChkMasterPoll);
    pCommBar->addWidget(m_pLabelServer);
    pCommBar->addStretch();
    pCommBar->addWidget(m_pChkAutoSnap);
    pCommBar->addWidget(m_pChkYDown);
    pCommBar->addWidget(pBtnSaveCfg);
    pMain->addWidget(pCommCard);

    // ---------------- 主工作区页签 + 底部日志卡片 ----------------
    auto* pSplitter = new QSplitter(Qt::Vertical, pCentral);
    pSplitter->setHandleWidth(6);
    auto* pTabs = new QTabWidget(pSplitter);
    m_pTabs = pTabs;
    m_pRegTable = new RegisterTableWidget(m_pBank, pTabs);
    m_pRegTable->setStartAddress(m_settings.nRegisterViewStart);
    m_pAlignPage = new AlignPage(m_pAlignScene, pTabs);
    m_pPickPage = new PickPage(m_pPickScene, pTabs);
    auto* pScriptPage = new ScriptPage(m_pEngine, pTabs);
    auto* pStagePage = new QWidget(pTabs);
    auto* pStageLayout = new QHBoxLayout(pStagePage);
    pStageLayout->setContentsMargins(10, 10, 10, 10);
    pStageLayout->setSpacing(12);
    m_pAlignStageWidget = new StageConfigWidget(m_pAlignStage, QStringLiteral("对位平台 (脚本 SelectStage(\"align\"))"), pStagePage);
    m_pPickStageWidget = new StageConfigWidget(m_pPickStage, QStringLiteral("拾取头 (脚本 SelectStage(\"pick\"))"), pStagePage);
    pStageLayout->addWidget(m_pAlignStageWidget);
    pStageLayout->addWidget(m_pPickStageWidget);
    pTabs->addTab(m_pRegTable, QStringLiteral("通信寄存器"));
    pTabs->addTab(m_pAlignPage, QStringLiteral("对位模拟"));
    pTabs->addTab(m_pPickPage, QStringLiteral("大视野拾取"));
    pTabs->addTab(pScriptPage, QStringLiteral("脚本与自动化"));
    // 20261025 ZJH 平台配置加了误差注入后较高, 放进滚动区
    auto* pStageScroll = new QScrollArea(pTabs);
    pStageScroll->setWidgetResizable(true);
    pStageScroll->setWidget(pStagePage);
    pTabs->addTab(pStageScroll, QStringLiteral("运动平台配置"));

    auto* pLogHost = new QFrame(pSplitter);
    pLogHost->setObjectName(QStringLiteral("logCard"));
    auto* pLogLayout = new QVBoxLayout(pLogHost);
    pLogLayout->setContentsMargins(10, 6, 10, 8);
    pLogLayout->setSpacing(4);
    auto* pLogBar = new QHBoxLayout();
    auto* pLblLogTitle = new QLabel(QStringLiteral("运行与通信报文日志"), pLogHost);
    pLblLogTitle->setStyleSheet(QStringLiteral("font-weight: 600; color: #334155;"));
    m_pChkCommLog = new QCheckBox(QStringLiteral("显示通信报文"), pLogHost);
    m_pChkCommLog->setChecked(true);
    auto* pBtnClearLog = new QPushButton(QStringLiteral("清空日志"), pLogHost);
    pLogBar->addWidget(pLblLogTitle);
    pLogBar->addStretch();
    pLogBar->addWidget(m_pChkCommLog);
    pLogBar->addWidget(pBtnClearLog);
    m_pLog = new QPlainTextEdit(pLogHost);
    m_pLog->setReadOnly(true);
    m_pLog->setMaximumBlockCount(s_nLogMaxLines);  // 限制行数, 长时间运行不涨内存
    m_pLog->setUndoRedoEnabled(false);             // 20261026 ZJH 只读日志不需要撤销栈
    m_pLog->setStyleSheet(QStringLiteral("background-color: #F8FAFC; border: 1px solid #E2E8F0; border-radius: 5px; font-family: Consolas, monospace; font-size: 12px; padding: 4px;"));
    m_pLogTimer = new QTimer(this);
    m_pLogTimer->setSingleShot(true);
    m_pLogTimer->setInterval(50);
    connect(m_pLogTimer, &QTimer::timeout, this, &MainWindow::flushLog);
    pLogLayout->addLayout(pLogBar);
    pLogLayout->addWidget(m_pLog);
    pSplitter->addWidget(pTabs);
    pSplitter->addWidget(pLogHost);
    pSplitter->setStretchFactor(0, 5);
    pSplitter->setStretchFactor(1, 1);
    pSplitter->setSizes({ 740, 145 });
    pMain->addWidget(pSplitter);

    // ---------------- 信号 ----------------
    connect(pBtnSaveCfg, &QPushButton::clicked, this, &MainWindow::onSaveConfig);
    connect(m_pBtnServer, &QPushButton::clicked, this, &MainWindow::onToggleServer);
    connect(m_pComboRole, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int nRoleIdx) {
        const bool bClient = (nRoleIdx == 1);
        m_pChkMasterPoll->setVisible(bClient);
        if (bClient && m_pEditIp->text().trimmed() == QStringLiteral("0.0.0.0")) {
            m_pEditIp->setText(QStringLiteral("127.0.0.1"));
        } else if (!bClient && m_pEditIp->text().trimmed() == QStringLiteral("127.0.0.1")) {
            m_pEditIp->setText(QStringLiteral("0.0.0.0"));
        }
        if (m_pServer->isActive()) {
            m_pServer->stop();
            onToggleServer();
        } else {
            updateServerUi();
        }
    });
    connect(m_pChkMasterPoll, &QCheckBox::toggled, m_pServer, &PlcServer::setClientMasterPoll);
    m_pServer->setClientMasterPoll(m_pChkMasterPoll->isChecked());
    connect(m_pComboProtocol, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onProtocolChanged);
    connect(m_pServer, &PlcServer::logMessage, this, &MainWindow::onCommLog);
    connect(m_pServer, &PlcServer::clientCountChanged, this, &MainWindow::updateServerUi);
    connect(m_pEngine, &LuaEngine::logMessage, this, [this](const QString& strMsg) { appendLog(strMsg, false); },
            Qt::QueuedConnection);
    connect(pScriptPage, &ScriptPage::logMessage, this, &MainWindow::appendLog);
    connect(pBtnClearLog, &QPushButton::clicked, this, [this] {
        m_vecPendingLog.clear();  // 20261026 ZJH 连同尚未写入的一起清
        m_pLog->clear();
    });
    // 20261026 ZJH 关闭报文显示时通知服务端不再生成逐帧日志
    connect(m_pChkCommLog, &QCheckBox::toggled, m_pServer, &PlcServer::setFrameLogEnabled);
    m_pServer->setFrameLogEnabled(m_pChkCommLog->isChecked());
    connect(m_pChkYDown, &QCheckBox::toggled, this, [this](bool bOn) {
        m_pAlignPage->setYDown(bOn);
        m_pPickPage->setYDown(bOn);
    });
    // 20261026 ZJH 监听 D100 上升沿 (0→1): 若勾选了「触发(D100=1)自动存图」, 自动渲染保存对位或拾取拍屏图
    connect(m_pBank, &RegisterBank::registersChanged, this, [this](int nAddr, int nCount) {
        if (nAddr <= 100 && 100 < nAddr + nCount) {
            const int16_t nNow = m_pBank->getInt16(100);
            if (m_nLastD100 == 0 && nNow == 1 && m_pChkAutoSnap && m_pChkAutoSnap->isChecked()) {
                const int nCmd = m_pBank->getInt32(102);
                const QString strDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs/触发存图");
                QDir().mkpath(strDir);
                const QString strStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
                if (nCmd == 20 || m_pTabs->currentWidget() == m_pPickPage) {
                    const QString strPath = QStringLiteral("%1/拾取_cmd%2_%3.png").arg(strDir).arg(nCmd).arg(strStamp);
                    renderPickSnapshot(m_pPickScene).save(strPath);
                    appendLog(QStringLiteral("触发自动存图: %1").arg(strPath), false);
                } else {
                    const int nCams = std::max(1, static_cast<int>(m_pAlignScene->cameras().size()));
                    for (int c = 0; c < nCams; ++c) {
                        const QString strPath = QStringLiteral("%1/对位_相机%2_cmd%3_%4.png").arg(strDir).arg(c + 1).arg(nCmd).arg(strStamp);
                        renderAlignSnapshot(m_pAlignScene, c).save(strPath);
                    }
                    appendLog(QStringLiteral("触发自动存图: 已保存 %1 个相机图像到 %2").arg(nCams).arg(strDir), false);
                }
            }
            m_nLastD100 = nNow;
        }
    }, Qt::QueuedConnection);
    m_pAlignPage->setYDown(m_settings.bViewYDown);
    m_pPickPage->setYDown(m_settings.bViewYDown);
    updateServerUi();
}

void MainWindow::onToggleServer()
{
    // 20261026 ZJH 正在监听或客户端连接中 → 停止; 否则按界面所选角色(服务端/客户端)启动
    if (m_pServer->isActive()) {
        m_pServer->stop();
    } else {
        const auto eType = static_cast<ProtocolType>(m_pComboProtocol->currentIndex());
        const QString strIp = m_pEditIp->text().trimmed();
        const int nPort = m_pSpinPort->value();
        if (m_pComboRole && m_pComboRole->currentIndex() == 1) {
            m_pServer->startClient(eType, strIp, nPort);
        } else {
            m_pServer->start(eType, strIp, nPort);
        }
    }
    updateServerUi();
}

void MainWindow::onProtocolChanged(int nIndex)
{
    const auto eType = static_cast<ProtocolType>(nIndex);
    // 20261024 ZJH 端口还是另一协议的默认值时, 自动换成新协议的推荐端口
    if (m_pSpinPort->value() == 8501 || m_pSpinPort->value() == 7110 || m_pSpinPort->value() == 502) {
        m_pSpinPort->setValue(ProtocolFactory::defaultPort(eType));
    }
    // 20261026 ZJH 运行中切换协议: 立即按当前角色与新协议重启
    if (m_pServer->isActive()) {
        if (m_pComboRole && m_pComboRole->currentIndex() == 1) {
            m_pServer->startClient(eType, m_pEditIp->text().trimmed(), m_pSpinPort->value());
        } else {
            m_pServer->start(eType, m_pEditIp->text().trimmed(), m_pSpinPort->value());
        }
        updateServerUi();
    }
}

void MainWindow::updateServerUi()
{
    const bool bClientRole = (m_pComboRole && m_pComboRole->currentIndex() == 1);
    const bool bActive = m_pServer->isActive();
    m_pEditIp->setEnabled(!bActive);
    m_pSpinPort->setEnabled(!bActive);
    if (!bClientRole) {
        m_pBtnServer->setText(bActive ? QStringLiteral("停止监听") : QStringLiteral("开始监听"));
        m_pLabelServer->setText(bActive ? QStringLiteral("<font color=green>● 监听中  客户端 %1</font>").arg(m_pServer->clientCount())
                                        : QStringLiteral("<font color=gray>● 未监听</font>"));
    } else {
        m_pBtnServer->setText(bActive ? QStringLiteral("断开连接") : QStringLiteral("连接服务端"));
        if (!bActive) {
            m_pLabelServer->setText(QStringLiteral("<font color=gray>● 未连接</font>"));
        } else if (m_pServer->isClientConnected()) {
            m_pLabelServer->setText(QStringLiteral("<font color=green>● 已连接 %1</font>").arg(m_pServer->targetEndpoint()));
        } else {
            m_pLabelServer->setText(QStringLiteral("<font color=#D97706>● 正在连接 %1 (自动重连中)</font>").arg(m_pServer->targetEndpoint()));
        }
    }
}

void MainWindow::appendLog(const QString& strMsg, bool bError)
{
    const QString strTime = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_vecPendingLog.push_back(PendingLog{ QStringLiteral("%1  %2").arg(strTime, strMsg), bError });
    // 20261026 ZJH 积压超过日志框容量时丢弃最旧的 (反正写进去也会被行数上限挤掉)
    if (m_vecPendingLog.size() > s_nLogMaxLines) {
        m_vecPendingLog.erase(m_vecPendingLog.begin(), m_vecPendingLog.end() - s_nLogMaxLines);
    }
    // 20261026 ZJH 合并刷新: 50ms 内的日志一次写入
    if (m_pLogTimer && !m_pLogTimer->isActive()) {
        m_pLogTimer->start();
    }
}

void MainWindow::flushLog()
{
    if (m_vecPendingLog.isEmpty()) {
        return;
    }
    QScrollBar* pBar = m_pLog->verticalScrollBar();
    const bool bAtBottom = pBar->value() >= pBar->maximum() - 2;  // 用户向上翻看时不强制滚到底
    QTextCharFormat fmtNormal;
    QTextCharFormat fmtError;
    fmtError.setForeground(Qt::red);  // 20261024 ZJH 错误日志红色
    QTextCursor cursor(m_pLog->document());
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();  // 整批一次排版
    for (const PendingLog& log : m_vecPendingLog) {
        if (!m_pLog->document()->isEmpty()) {
            cursor.insertBlock();
        }
        cursor.insertText(log.strLine, log.bError ? fmtError : fmtNormal);  // 纯文本插入, 无需 HTML 转义/解析
    }
    cursor.endEditBlock();
    m_vecPendingLog.clear();
    if (bAtBottom) {
        pBar->setValue(pBar->maximum());
    }
}

void MainWindow::onCommLog(const QString& strMsg, bool bError)
{
    // 20261024 ZJH 关闭报文显示时只显示错误和连接类日志
    if (!m_pChkCommLog->isChecked() && !bError && (strMsg.startsWith(QStringLiteral("收")) || strMsg.startsWith(QStringLiteral("发")))) {
        return;
    }
    appendLog(strMsg, bError);
}

void MainWindow::onSaveConfig()
{
    collectSettingsFromCore();
    QString strErr;
    if (AppConfig::save(AppConfig::defaultPath(), m_settings, strErr)) {
        appendLog(QStringLiteral("配置已保存 %1").arg(AppConfig::defaultPath()), false);
    } else {
        appendLog(strErr, true);
    }
}

void MainWindow::closeEvent(QCloseEvent* pEvent)
{
    onSaveConfig();  // 退出自动保存
    m_pEngine->stopAll();
    QMainWindow::closeEvent(pEvent);
}
