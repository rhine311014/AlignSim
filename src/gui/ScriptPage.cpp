// 20261024 ZJH ScriptPage 实现
#include "gui/ScriptPage.h"

#include "script/LuaEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
// 20261024 ZJH 脚本名合法性: 非空, 不含路径分隔符等文件名非法字符
bool isValidScriptName(const QString& strName)
{
    static const QString s_strBad = QStringLiteral("\\/:*?\"<>|");
    if (strName.trimmed().isEmpty()) {
        return false;
    }
    for (QChar ch : strName) {
        if (s_strBad.contains(ch)) {
            return false;
        }
    }
    return true;  // 返回合法
}
}  // namespace

ScriptPage::ScriptPage(LuaEngine* pEngine, QWidget* pParent)
    : QWidget(pParent)
    , m_pEngine(pEngine)
{
    auto* pMain = new QHBoxLayout(this);
    pMain->setContentsMargins(10, 10, 10, 10);
    auto* pSplitter = new QSplitter(Qt::Horizontal, this);
    pSplitter->setHandleWidth(6);
    pMain->addWidget(pSplitter);

    // ---------------- 左: 列表 ----------------
    auto* pLeft = new QWidget(pSplitter);
    auto* pLeftLayout = new QVBoxLayout(pLeft);
    pLeftLayout->setContentsMargins(0, 0, 4, 0);
    pLeftLayout->setSpacing(6);
    m_pList = new QListWidget(pLeft);
    m_pList->setAlternatingRowColors(true);
    auto* pBtnRow = new QHBoxLayout();
    auto* pBtnNew = new QPushButton(QStringLiteral("新建"), pLeft);
    auto* pBtnDel = new QPushButton(QStringLiteral("删除"), pLeft);
    pBtnDel->setProperty("danger", true);
    auto* pBtnReload = new QPushButton(QStringLiteral("刷新"), pLeft);
    pBtnRow->addWidget(pBtnNew);
    pBtnRow->addWidget(pBtnDel);
    pBtnRow->addWidget(pBtnReload);
    pLeftLayout->addWidget(new QLabel(QStringLiteral("Lua 自动化脚本列表 (双击直接运行)"), pLeft));
    pLeftLayout->addWidget(m_pList);
    pLeftLayout->addLayout(pBtnRow);

    // ---------------- 右: 编辑器 ----------------
    auto* pRight = new QWidget(pSplitter);
    auto* pRightLayout = new QVBoxLayout(pRight);
    pRightLayout->setContentsMargins(4, 0, 0, 0);
    pRightLayout->setSpacing(6);
    auto* pToolRow = new QHBoxLayout();
    auto* pBtnSave = new QPushButton(QStringLiteral("保存 (Ctrl+S)"), pRight);
    pBtnSave->setShortcut(QKeySequence::Save);
    auto* pBtnCheck = new QPushButton(QStringLiteral("语法检查"), pRight);
    auto* pBtnRun = new QPushButton(QStringLiteral("▶ 运行脚本"), pRight);
    pBtnRun->setProperty("primary", true);
    auto* pBtnStop = new QPushButton(QStringLiteral("■ 停止当前"), pRight);
    auto* pBtnStopAll = new QPushButton(QStringLiteral("全部停止"), pRight);
    pBtnStopAll->setProperty("danger", true);
    pToolRow->addWidget(pBtnSave);
    pToolRow->addWidget(pBtnCheck);
    pToolRow->addStretch();
    pToolRow->addWidget(pBtnRun);
    pToolRow->addWidget(pBtnStop);
    pToolRow->addWidget(pBtnStopAll);
    pRightLayout->addLayout(pToolRow);
    m_pEditor = new QPlainTextEdit(pRight);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    m_pEditor->setFont(font);
    m_pEditor->setTabStopDistance(4 * m_pEditor->fontMetrics().horizontalAdvance(' '));
    m_pEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    pRightLayout->addWidget(m_pEditor, 1);
    m_pLabelHint = new QLabel(pRight);
    m_pLabelHint->setWordWrap(true);
    m_pLabelHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pLabelHint->setText(QStringLiteral("可用函数: ") + LuaEngine::functionNames().join(QStringLiteral(", ")));
    pRightLayout->addWidget(m_pLabelHint);

    pSplitter->addWidget(pLeft);
    pSplitter->addWidget(pRight);
    pSplitter->setStretchFactor(0, 1);
    pSplitter->setStretchFactor(1, 4);

    // ---------------- 信号 ----------------
    connect(m_pList, &QListWidget::currentRowChanged, this, &ScriptPage::onSelectionChanged);
    connect(m_pList, &QListWidget::itemDoubleClicked, this, &ScriptPage::onRun);
    connect(pBtnNew, &QPushButton::clicked, this, &ScriptPage::onNew);
    connect(pBtnDel, &QPushButton::clicked, this, &ScriptPage::onDelete);
    connect(pBtnReload, &QPushButton::clicked, this, &ScriptPage::reloadList);
    connect(pBtnSave, &QPushButton::clicked, this, &ScriptPage::onSave);
    connect(pBtnCheck, &QPushButton::clicked, this, &ScriptPage::onCheck);
    connect(pBtnRun, &QPushButton::clicked, this, &ScriptPage::onRun);
    connect(pBtnStop, &QPushButton::clicked, this, &ScriptPage::onStop);
    connect(pBtnStopAll, &QPushButton::clicked, this, &ScriptPage::onStopAll);
    // 20261024 ZJH 脚本开始/结束 (结束信号来自工作线程) → 排队刷新列表与日志
    connect(m_pEngine, &LuaEngine::scriptStarted, this, [this](const QString& strName) {
        emit logMessage(QStringLiteral("脚本开始: %1").arg(strName), false);
        onScriptStateChanged();
    }, Qt::QueuedConnection);
    connect(m_pEngine, &LuaEngine::scriptFinished, this, [this](const QString& strName, bool bOk, const QString& strErr) {
        emit logMessage(bOk ? QStringLiteral("脚本结束: %1").arg(strName)
                            : QStringLiteral("脚本异常结束: %1  %2").arg(strName, strErr), !bOk);
        onScriptStateChanged();
    }, Qt::QueuedConnection);

    reloadList();
}

QString ScriptPage::scriptDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/scripts");
}

QString ScriptPage::currentName() const
{
    const QListWidgetItem* pItem = m_pList->currentItem();
    return pItem ? pItem->data(Qt::UserRole).toString() : QString();  // UserRole 存不带装饰的脚本名
}

void ScriptPage::reloadList()
{
    const QString strKeep = currentName();  // 刷新后保持选中
    QDir().mkpath(scriptDir());
    const QStringList lstFiles = QDir(scriptDir()).entryList({ QStringLiteral("*.lua") }, QDir::Files, QDir::Name);
    const QSignalBlocker blocker(m_pList);
    m_pList->clear();
    for (const QString& strFile : lstFiles) {  // 逐文件加入列表
        auto* pItem = new QListWidgetItem(QFileInfo(strFile).completeBaseName(), m_pList);
        pItem->setData(Qt::UserRole, QFileInfo(strFile).completeBaseName());
    }
    onScriptStateChanged();
    // 20261024 ZJH 恢复选中; 没有则选第一个
    const QList<QListWidgetItem*> lstMatch = m_pList->findItems(strKeep, Qt::MatchExactly);
    m_pList->setCurrentRow(lstMatch.isEmpty() ? 0 : m_pList->row(lstMatch.first()));
    onSelectionChanged();
}

void ScriptPage::onSelectionChanged()
{
    const QString strName = currentName();
    // 20261024 ZJH 切换前若编辑器有未保存修改, 自动保存到原脚本
    if (!m_strLoadedName.isEmpty() && m_strLoadedName != strName && m_pEditor->document()->isModified()) {
        if (saveCurrent()) {
            emit logMessage(QStringLiteral("已自动保存 %1").arg(m_strLoadedName), false);
        }
    }
    m_strLoadedName = strName;
    // 20261024 ZJH 没有选中脚本
    if (strName.isEmpty()) {
        m_pEditor->clear();
        return;
    }
    QFile file(scriptDir() + QStringLiteral("/%1.lua").arg(strName));
    m_pEditor->setPlainText(file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString());
    m_pEditor->document()->setModified(false);
}

bool ScriptPage::saveCurrent()
{
    // 20261024 ZJH 未载入任何脚本
    if (m_strLoadedName.isEmpty()) {
        return false;
    }
    QFile file(scriptDir() + QStringLiteral("/%1.lua").arg(m_strLoadedName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit logMessage(QStringLiteral("保存失败: %1").arg(file.errorString()), true);
        return false;
    }
    file.write(m_pEditor->toPlainText().toUtf8());
    m_pEditor->document()->setModified(false);
    return true;  // 返回成功
}

void ScriptPage::onNew()
{
    bool bOk = false;
    const QString strName = QInputDialog::getText(this, QStringLiteral("新建脚本"), QStringLiteral("脚本名 (不含 .lua)"),
                                                  QLineEdit::Normal, QString(), &bOk).trimmed();
    // 20261024 ZJH 取消或名称非法
    if (!bOk) {
        return;
    }
    if (!isValidScriptName(strName)) {
        QMessageBox::warning(this, QStringLiteral("新建脚本"), QStringLiteral("脚本名非法"));
        return;
    }
    const QString strPath = scriptDir() + QStringLiteral("/%1.lua").arg(strName);
    if (QFile::exists(strPath)) {
        QMessageBox::warning(this, QStringLiteral("新建脚本"), QStringLiteral("同名脚本已存在"));
        return;
    }
    QFile file(strPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QStringLiteral("-- %1\nLog('hello')\n").arg(strName).toUtf8());
    }
    file.close();
    reloadList();
    const QList<QListWidgetItem*> lstMatch = m_pList->findItems(strName, Qt::MatchExactly);
    if (!lstMatch.isEmpty()) {
        m_pList->setCurrentItem(lstMatch.first());
    }
}

void ScriptPage::onSave()
{
    if (saveCurrent()) {
        emit logMessage(QStringLiteral("已保存 %1").arg(m_strLoadedName), false);
    }
}

void ScriptPage::onDelete()
{
    const QString strName = currentName();
    // 20261024 ZJH 未选中或正在运行的脚本不允许删除
    if (strName.isEmpty()) {
        return;
    }
    if (m_pEngine->isRunning(strName)) {
        QMessageBox::warning(this, QStringLiteral("删除脚本"), QStringLiteral("脚本正在运行, 请先停止"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除脚本"), QStringLiteral("确定删除 %1 ?").arg(strName)) != QMessageBox::Yes) {
        return;
    }
    QFile::remove(scriptDir() + QStringLiteral("/%1.lua").arg(strName));
    m_strLoadedName.clear();  // 防止切换时把内容自动存回已删除文件
    reloadList();
}

void ScriptPage::onCheck()
{
    QString strErr;
    // 20261024 ZJH 语法检查结果写日志
    if (LuaEngine::checkSyntax(m_pEditor->toPlainText(), strErr)) {
        emit logMessage(QStringLiteral("语法检查通过: %1").arg(m_strLoadedName), false);
    } else {
        emit logMessage(QStringLiteral("语法错误: %1").arg(strErr), true);
    }
}

void ScriptPage::onRun()
{
    // 20261024 ZJH 先保存再运行, 保证运行的是编辑器里看到的内容
    if (!saveCurrent()) {
        return;
    }
    QString strErr;
    if (!m_pEngine->start(m_strLoadedName, m_pEditor->toPlainText(), strErr)) {
        emit logMessage(QStringLiteral("启动失败: %1").arg(strErr), true);
    }
}

void ScriptPage::onStop()
{
    m_pEngine->stop(currentName());
}

void ScriptPage::onStopAll()
{
    m_pEngine->stopAll();
}

void ScriptPage::onScriptStateChanged()
{
    for (int i = 0; i < m_pList->count(); ++i) {  // 运行中标绿
        QListWidgetItem* pItem = m_pList->item(i);
        const bool bRunning = m_pEngine->isRunning(pItem->data(Qt::UserRole).toString());
        pItem->setForeground(bRunning ? QColor(0, 150, 0) : palette().color(QPalette::Text));
        pItem->setText(pItem->data(Qt::UserRole).toString() + (bRunning ? QStringLiteral("  [运行中]") : QString()));
    }
}
