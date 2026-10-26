// 20261024 ZJH MainWindow —— 主窗口: 持有所有核心对象, 组织各页面
// 顶部: 通信栏 (协议/IP/端口/启停/客户端数) + Y 轴方向
// 中部: 页签 通信寄存器 / 对位模拟 / 大视野拾取 / 脚本 / 平台配置
// 底部: 日志 (可过滤通信报文)
#pragma once

#include "core/AppConfig.h"

#include <QMainWindow>
#include <QVector>

class QTimer;

class RegisterBank;
class Stage;
class AlignScene;
class PickScene;
class PlcServer;
class LuaEngine;
class AlignPage;
class PickPage;
class RegisterTableWidget;
class StageConfigWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* pParent = nullptr);
    ~MainWindow() override;

    // 20261024 ZJH 运行 scripts 目录下的脚本 (不含 .lua); 失败写日志并返回 false
    bool runScriptFile(const QString& strName);
    // 20261024 ZJH 依次切换每个页签并把整个窗口截图保存到 strDir (界面自检/文档用)
    void grabAllTabs(const QString& strDir);

protected:
    void closeEvent(QCloseEvent* pEvent) override;

private slots:
    void onToggleServer();                        // 启动/停止监听
    void onProtocolChanged(int nIndex);           // 切换协议 → 推荐端口
    void appendLog(const QString& strMsg, bool bError);  // 追加日志
    void onCommLog(const QString& strMsg, bool bError);  // 通信日志 (可过滤)
    void onSaveConfig();                          // 保存配置

private:
    void buildUi();                    // 构建界面
    void applySettingsToCore();        // 配置 → 核心对象
    void collectSettingsFromCore();    // 核心对象 → 配置
    void updateServerUi();             // 刷新启停按钮/状态
    void flushLog();                   // 20261026 ZJH 把排队的日志一次性写入日志框

    // 20261026 ZJH 待写入的日志行 (高频通信时合并写入, 避免每行一次 HTML 解析 + 重排)
    struct PendingLog {
        QString strLine;   // 带时间戳的整行文本
        bool bError;       // 是否错误 (红色)
    };
    QVector<PendingLog> m_vecPendingLog;
    QTimer* m_pLogTimer = nullptr;     // 日志合并刷新定时器

    AppSettings m_settings;            // 当前配置
    RegisterBank* m_pBank = nullptr;   // 寄存器区
    Stage* m_pAlignStage = nullptr;    // 对位平台
    Stage* m_pPickStage = nullptr;     // 拾取头
    AlignScene* m_pAlignScene = nullptr;
    PickScene* m_pPickScene = nullptr;
    PlcServer* m_pServer = nullptr;
    LuaEngine* m_pEngine = nullptr;

    QTabWidget* m_pTabs = nullptr;
    QComboBox* m_pComboRole = nullptr;    // 20261026 ZJH 通信角色: 0=服务端(被连监听), 1=客户端(主动连接)
    QCheckBox* m_pChkMasterPoll = nullptr;// 20261026 ZJH 客户端模式下是否作为主站主动轮询/推送远端寄存器
    QComboBox* m_pComboProtocol = nullptr;
    QLineEdit* m_pEditIp = nullptr;
    QSpinBox* m_pSpinPort = nullptr;
    QPushButton* m_pBtnServer = nullptr;
    QLabel* m_pLabelServer = nullptr;
    QCheckBox* m_pChkYDown = nullptr;
    QCheckBox* m_pChkAutoSnap = nullptr;  // 20261026 ZJH D100=1 触发拍照时自动存图到 logs/触发存图
    int16_t m_nLastD100 = 0;              // 上一次 D100 值 (检测 0→1 上升沿)
    QCheckBox* m_pChkCommLog = nullptr;
    QPlainTextEdit* m_pLog = nullptr;
    RegisterTableWidget* m_pRegTable = nullptr;
    AlignPage* m_pAlignPage = nullptr;
    PickPage* m_pPickPage = nullptr;
    StageConfigWidget* m_pAlignStageWidget = nullptr;
    StageConfigWidget* m_pPickStageWidget = nullptr;
};
