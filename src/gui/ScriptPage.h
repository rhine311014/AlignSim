// 20261024 ZJH ScriptPage —— Lua 脚本管理页面
// 左: <exe目录>/scripts 下的 .lua 文件列表 (运行中的脚本标绿); 右: 编辑器 + 新建/保存/删除/检查/运行/停止
// 脚本以文件名 (不含扩展名) 作为运行名, 同名脚本同一时刻只能运行一个
#pragma once

#include <QWidget>

class LuaEngine;
class QListWidget;
class QPlainTextEdit;
class QLabel;

class ScriptPage : public QWidget
{
    Q_OBJECT
public:
    ScriptPage(LuaEngine* pEngine, QWidget* pParent = nullptr);

    static QString scriptDir();  // 脚本目录

signals:
    // 20261024 ZJH 日志输出 (主窗口显示)
    void logMessage(const QString& strMsg, bool bError);

private slots:
    void reloadList();                 // 重新扫描目录
    void onSelectionChanged();         // 切换脚本: 载入编辑器
    void onNew();                      // 新建
    void onSave();                     // 保存当前
    void onDelete();                   // 删除当前
    void onCheck();                    // 语法检查
    void onRun();                      // 运行当前 (先保存)
    void onStop();                     // 停止当前
    void onStopAll();                  // 停止全部
    void onScriptStateChanged();       // 运行状态变化 → 刷新列表颜色

private:
    QString currentName() const;       // 当前选中脚本名
    bool saveCurrent();                // 保存编辑器内容到文件

    LuaEngine* m_pEngine = nullptr;    // 脚本引擎 (不持有)
    QListWidget* m_pList = nullptr;    // 脚本列表
    QPlainTextEdit* m_pEditor = nullptr; // 编辑器
    QLabel* m_pLabelHint = nullptr;    // 函数提示
    QString m_strLoadedName;           // 编辑器当前载入的脚本名
};
