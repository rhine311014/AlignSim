// 20261108 ZJH MiniPanel —— 小窗模式: 置顶的小工具窗, 拍屏时代替主窗口触发脚本
// 主窗口收起后, 相机视图 (独立窗口) 继续全屏给相机拍, 屏幕其余部分留给视觉软件
// 内容: 通信状态 / 运行中脚本 / 脚本下拉 + 运行 / 停止 / 全部停止 / 常用脚本按钮 (≤6, 对应全局热键 Ctrl+Alt+1~6) / 最近日志
// 本类不直接操作脚本引擎, 只发请求信号, 由主窗口执行 (便于单独测试)
#pragma once

#include <QSet>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class MiniPanel : public QWidget
{
    Q_OBJECT
public:
    static constexpr int s_nMaxFavorites = 6;  // 常用脚本上限 (对应热键 Ctrl+Alt+1~6)
    static constexpr int s_nMaxLogLines = 5;   // 最近日志行数

    explicit MiniPanel(QWidget* pParent = nullptr);

    // 20261108 ZJH 脚本目录 (扫描 .lua 生成下拉列表); 设置后立即刷新
    void setScriptDir(const QString& strDir);
    // 20261108 ZJH 常用脚本 (超过上限截断, 去重, 去空)
    void setFavorites(const QStringList& lstNames);
    QStringList favorites() const { return m_lstFavorites; }
    // 20261108 ZJH 置顶开关 (改变窗口标志后需重新 show, 内部处理)
    void setStayOnTop(bool bOn);
    bool stayOnTop() const;
    // 20261108 ZJH 全局热键开关 (只记录状态与显示, 注册由主窗口负责)
    void setHotkeysEnabled(bool bOn);
    bool hotkeysEnabled() const;
    // 20261108 ZJH 热键注册结果提示 (空 = 正常)
    void setHotkeyHint(const QString& strHint);
    // 20261108 ZJH 通信状态 (富文本, 由主窗口推送)
    void setStatusText(const QString& strRich);
    // 20261108 ZJH 脚本运行状态变化 (由主窗口转发引擎信号)
    void setScriptRunning(const QString& strName, bool bRunning);
    QStringList runningScripts() const;
    // 20261108 ZJH 追加一行日志 (只保留最近 s_nMaxLogLines 行)
    void appendLog(const QString& strLine, bool bError);
    // 20261108 ZJH 运行第 nIndex 个常用脚本 (0 起; 热键调用); 不存在返回 false
    bool runFavorite(int nIndex);
    // 20261108 ZJH 当前下拉框选中的脚本名 (测试用)
    QString currentScript() const;

signals:
    void runRequested(const QString& strName);   // 请求运行脚本
    void stopRequested(const QString& strName);  // 请求停止脚本
    void stopAllRequested();                     // 请求停止全部
    void returnToMainRequested();                // 回主界面 (含点关闭按钮)
    void favoritesChanged();                     // 常用脚本被修改 (主窗口保存配置)
    void hotkeysToggled(bool bOn);               // 全局热键开关被用户切换
    void stayOnTopToggled(bool bOn);             // 置顶开关被用户切换

public slots:
    void reloadScripts();  // 重新扫描脚本目录

protected:
    void closeEvent(QCloseEvent* pEvent) override;

private slots:
    void onEditFavorites();  // 设置常用脚本

private:
    void rebuildFavoriteButtons();  // 按常用列表重建按钮
    void updateRunningLabel();      // 刷新 "运行中" 标签与按钮高亮

    QString m_strScriptDir;              // 脚本目录
    QStringList m_lstFavorites;          // 常用脚本名
    QSet<QString> m_setRunning;          // 运行中脚本名
    QLabel* m_pLabelStatus = nullptr;    // 通信状态
    QLabel* m_pLabelRunning = nullptr;   // 运行中脚本
    QComboBox* m_pComboScript = nullptr; // 脚本下拉
    QGridLayout* m_pFavGrid = nullptr;   // 常用脚本按钮网格
    QList<QPushButton*> m_lstFavButtons; // 常用脚本按钮
    QCheckBox* m_pChkTop = nullptr;      // 置顶
    QCheckBox* m_pChkHotkeys = nullptr;  // 全局热键
    QLabel* m_pLabelHotkeyHint = nullptr;// 热键提示
    QPlainTextEdit* m_pLog = nullptr;    // 最近日志
};
