// 20261108 ZJH MiniPanel 实现
#include "gui/MiniPanel.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

MiniPanel::MiniPanel(QWidget* pParent)
    : QWidget(pParent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(QStringLiteral("AlignSim 小窗"));
    setObjectName(QStringLiteral("miniPanel"));
    setStyleSheet(QStringLiteral(
        "QWidget#miniPanel { background-color: #F1F5F9; font-family: \"Microsoft YaHei UI\"; font-size: 12px; }"
        "QPushButton { padding: 4px 8px; border: 1px solid #CBD5E1; border-radius: 4px; background: #FFFFFF; }"
        "QPushButton:hover { background: #E0F2FE; }"
        "QPushButton[running=\"true\"] { background: #DCFCE7; border-color: #22C55E; }"
        "QPushButton#runBtn { background: #2563EB; color: white; border-color: #2563EB; }"
        "QPushButton#stopAllBtn { color: #B91C1C; }"));
    auto* pMain = new QVBoxLayout(this);
    pMain->setContentsMargins(8, 8, 8, 8);
    pMain->setSpacing(6);

    // ---------------- 状态 + 开关 ----------------
    auto* pTopRow = new QHBoxLayout();
    m_pLabelStatus = new QLabel(QStringLiteral("<font color=gray>● 未监听</font>"), this);
    m_pChkTop = new QCheckBox(QStringLiteral("置顶"), this);
    m_pChkTop->setChecked(true);
    pTopRow->addWidget(m_pLabelStatus, 1);
    pTopRow->addWidget(m_pChkTop);
    pMain->addLayout(pTopRow);
    m_pLabelRunning = new QLabel(this);
    m_pLabelRunning->setWordWrap(true);
    pMain->addWidget(m_pLabelRunning);

    // ---------------- 脚本下拉 + 运行/停止 ----------------
    m_pComboScript = new QComboBox(this);
    m_pComboScript->setMinimumContentsLength(16);
    m_pComboScript->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto* pBtnReload = new QPushButton(QStringLiteral("↻"), this);
    pBtnReload->setToolTip(QStringLiteral("重新扫描脚本目录"));
    pBtnReload->setFixedWidth(30);
    auto* pComboRow = new QHBoxLayout();
    pComboRow->addWidget(m_pComboScript, 1);
    pComboRow->addWidget(pBtnReload);
    pMain->addLayout(pComboRow);
    auto* pBtnRow = new QHBoxLayout();
    auto* pBtnRun = new QPushButton(QStringLiteral("▶ 运行"), this);
    pBtnRun->setObjectName(QStringLiteral("runBtn"));
    auto* pBtnStop = new QPushButton(QStringLiteral("■ 停止"), this);
    auto* pBtnStopAll = new QPushButton(QStringLiteral("全部停止"), this);
    pBtnStopAll->setObjectName(QStringLiteral("stopAllBtn"));
    pBtnStopAll->setToolTip(QStringLiteral("全局热键 Ctrl+Alt+0"));
    pBtnRow->addWidget(pBtnRun);
    pBtnRow->addWidget(pBtnStop);
    pBtnRow->addWidget(pBtnStopAll);
    pMain->addLayout(pBtnRow);

    // ---------------- 常用脚本 ----------------
    auto* pFavTitle = new QHBoxLayout();
    pFavTitle->addWidget(new QLabel(QStringLiteral("<b>常用</b> (热键 Ctrl+Alt+1~6)"), this), 1);
    auto* pBtnEditFav = new QPushButton(QStringLiteral("设置"), this);
    pFavTitle->addWidget(pBtnEditFav);
    pMain->addLayout(pFavTitle);
    m_pFavGrid = new QGridLayout();
    m_pFavGrid->setSpacing(4);
    pMain->addLayout(m_pFavGrid);
    m_pChkHotkeys = new QCheckBox(QStringLiteral("全局热键 (视觉软件在前台也能触发)"), this);
    m_pChkHotkeys->setChecked(true);
    pMain->addWidget(m_pChkHotkeys);
    m_pLabelHotkeyHint = new QLabel(this);
    m_pLabelHotkeyHint->setWordWrap(true);
    m_pLabelHotkeyHint->setStyleSheet(QStringLiteral("color: #B45309;"));
    m_pLabelHotkeyHint->hide();
    pMain->addWidget(m_pLabelHotkeyHint);

    // ---------------- 最近日志 ----------------
    m_pLog = new QPlainTextEdit(this);
    m_pLog->setReadOnly(true);
    m_pLog->setMaximumBlockCount(s_nMaxLogLines);  // 只留最近几行
    m_pLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_pLog->setFixedHeight(92);
    m_pLog->setStyleSheet(QStringLiteral("font-family: Consolas, monospace; font-size: 11px; background: #FFFFFF;"));
    pMain->addWidget(m_pLog);

    auto* pBtnBack = new QPushButton(QStringLiteral("回主界面"), this);
    pMain->addWidget(pBtnBack);

    // ---------------- 信号 ----------------
    connect(pBtnReload, &QPushButton::clicked, this, &MiniPanel::reloadScripts);
    connect(pBtnRun, &QPushButton::clicked, this, [this] {
        // 20261108 ZJH 未选脚本时不发请求
        if (!currentScript().isEmpty()) {
            emit runRequested(currentScript());
        }
    });
    connect(pBtnStop, &QPushButton::clicked, this, [this] {
        if (!currentScript().isEmpty()) {
            emit stopRequested(currentScript());
        }
    });
    connect(pBtnStopAll, &QPushButton::clicked, this, &MiniPanel::stopAllRequested);
    connect(pBtnEditFav, &QPushButton::clicked, this, &MiniPanel::onEditFavorites);
    connect(pBtnBack, &QPushButton::clicked, this, &MiniPanel::returnToMainRequested);
    connect(m_pChkTop, &QCheckBox::toggled, this, [this](bool bOn) {
        setStayOnTop(bOn);
        emit stayOnTopToggled(bOn);
    });
    connect(m_pChkHotkeys, &QCheckBox::toggled, this, &MiniPanel::hotkeysToggled);

    rebuildFavoriteButtons();
    updateRunningLabel();
    resize(300, 380);
}

void MiniPanel::setScriptDir(const QString& strDir)
{
    m_strScriptDir = strDir;
    reloadScripts();
}

void MiniPanel::reloadScripts()
{
    const QString strKeep = currentScript();  // 刷新后保持原选择
    m_pComboScript->clear();
    const QFileInfoList lstFiles = QDir(m_strScriptDir).entryInfoList({ QStringLiteral("*.lua") }, QDir::Files, QDir::Name);
    for (const QFileInfo& info : lstFiles) {  // 只列顶层脚本 (lib/ 子目录为公共库)
        m_pComboScript->addItem(info.completeBaseName());
    }
    const int nIndex = m_pComboScript->findText(strKeep);
    if (nIndex >= 0) {
        m_pComboScript->setCurrentIndex(nIndex);
    }
    rebuildFavoriteButtons();  // 常用脚本可能已被删除 → 按钮置灰
}

void MiniPanel::setFavorites(const QStringList& lstNames)
{
    m_lstFavorites.clear();
    for (const QString& strName : lstNames) {  // 去空、去重、截断到上限
        const QString strTrim = strName.trimmed();
        if (!strTrim.isEmpty() && !m_lstFavorites.contains(strTrim) && m_lstFavorites.size() < s_nMaxFavorites) {
            m_lstFavorites.append(strTrim);
        }
    }
    rebuildFavoriteButtons();
}

void MiniPanel::rebuildFavoriteButtons()
{
    for (QPushButton* pBtn : m_lstFavButtons) {  // 清掉旧按钮
        m_pFavGrid->removeWidget(pBtn);
        pBtn->deleteLater();
    }
    m_lstFavButtons.clear();
    for (int i = 0; i < m_lstFavorites.size(); ++i) {  // 两列排布
        const QString strName = m_lstFavorites[i];
        auto* pBtn = new QPushButton(QStringLiteral("%1 %2").arg(i + 1).arg(strName), this);
        pBtn->setToolTip(QStringLiteral("运行 %1  (Ctrl+Alt+%2)").arg(strName).arg(i + 1));
        // 20261108 ZJH 脚本文件不存在 (改名/删除) 时置灰
        const bool bExists = m_pComboScript->findText(strName) >= 0;
        pBtn->setEnabled(bExists);
        connect(pBtn, &QPushButton::clicked, this, [this, i] { runFavorite(i); });
        m_pFavGrid->addWidget(pBtn, i / 2, i % 2);
        m_lstFavButtons.append(pBtn);
    }
    updateRunningLabel();
}

bool MiniPanel::runFavorite(int nIndex)
{
    // 20261108 ZJH 越界或脚本已不存在
    if (nIndex < 0 || nIndex >= m_lstFavorites.size() || m_pComboScript->findText(m_lstFavorites[nIndex]) < 0) {
        return false;
    }
    emit runRequested(m_lstFavorites[nIndex]);
    return true;  // 已发出运行请求
}

void MiniPanel::onEditFavorites()
{
    // 20261108 ZJH 勾选式对话框: 已选的排前面并保持原顺序, 其余按名称
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("设置常用脚本 (最多 %1 个)").arg(s_nMaxFavorites));
    auto* pLayout = new QVBoxLayout(&dlg);
    auto* pList = new QListWidget(&dlg);
    QStringList lstAll;
    for (int i = 0; i < m_pComboScript->count(); ++i) {
        lstAll.append(m_pComboScript->itemText(i));
    }
    QStringList lstOrdered = m_lstFavorites;
    for (const QString& strName : lstAll) {
        if (!lstOrdered.contains(strName)) {
            lstOrdered.append(strName);
        }
    }
    for (const QString& strName : lstOrdered) {
        auto* pItem = new QListWidgetItem(strName, pList);
        pItem->setFlags(pItem->flags() | Qt::ItemIsUserCheckable);
        pItem->setCheckState(m_lstFavorites.contains(strName) ? Qt::Checked : Qt::Unchecked);
    }
    pList->setDragDropMode(QAbstractItemView::InternalMove);  // 拖动调整顺序 = 热键编号
    pLayout->addWidget(new QLabel(QStringLiteral("勾选常用脚本, 拖动调整顺序 (第 n 个对应 Ctrl+Alt+n)"), &dlg));
    pLayout->addWidget(pList);
    auto* pBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    pLayout->addWidget(pBox);
    connect(pBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(pBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.resize(320, 420);
    if (dlg.exec() != QDialog::Accepted) {
        return;  // 取消不改
    }
    QStringList lstNew;
    for (int i = 0; i < pList->count(); ++i) {  // 按列表当前顺序收集勾选项
        if (pList->item(i)->checkState() == Qt::Checked) {
            lstNew.append(pList->item(i)->text());
        }
    }
    // 20261108 ZJH 超出上限时提示并只取前 6 个
    if (lstNew.size() > s_nMaxFavorites) {
        QMessageBox::information(this, windowTitle(),
                                 QStringLiteral("最多 %1 个, 只保留前 %1 个").arg(s_nMaxFavorites));
    }
    setFavorites(lstNew);
    emit favoritesChanged();
}

void MiniPanel::setStayOnTop(bool bOn)
{
    const bool bVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, bOn);  // 改窗口标志会隐藏窗口
    if (m_pChkTop->isChecked() != bOn) {
        const QSignalBlocker blocker(m_pChkTop);
        m_pChkTop->setChecked(bOn);
    }
    if (bVisible) {
        show();  // 恢复显示
    }
}

bool MiniPanel::stayOnTop() const
{
    return m_pChkTop->isChecked();
}

void MiniPanel::setHotkeysEnabled(bool bOn)
{
    const QSignalBlocker blocker(m_pChkHotkeys);  // 程序设置不回发 hotkeysToggled
    m_pChkHotkeys->setChecked(bOn);
}

bool MiniPanel::hotkeysEnabled() const
{
    return m_pChkHotkeys->isChecked();
}

void MiniPanel::setHotkeyHint(const QString& strHint)
{
    m_pLabelHotkeyHint->setText(strHint);
    m_pLabelHotkeyHint->setVisible(!strHint.isEmpty());
}

void MiniPanel::setStatusText(const QString& strRich)
{
    m_pLabelStatus->setText(strRich);
}

void MiniPanel::setScriptRunning(const QString& strName, bool bRunning)
{
    if (bRunning) {
        m_setRunning.insert(strName);
    } else {
        m_setRunning.remove(strName);
    }
    updateRunningLabel();
}

QStringList MiniPanel::runningScripts() const
{
    QStringList lst(m_setRunning.begin(), m_setRunning.end());
    lst.sort();
    return lst;  // 返回排序后的运行中脚本
}

void MiniPanel::updateRunningLabel()
{
    const QStringList lst = runningScripts();
    m_pLabelRunning->setText(lst.isEmpty() ? QStringLiteral("<font color=gray>无脚本运行</font>")
                                           : QStringLiteral("<font color=#15803D>▶ 运行中: %1</font>").arg(lst.join(QStringLiteral(", "))));
    for (int i = 0; i < m_lstFavButtons.size(); ++i) {  // 运行中的常用按钮标绿
        QPushButton* pBtn = m_lstFavButtons[i];
        pBtn->setProperty("running", m_setRunning.contains(m_lstFavorites.value(i)));
        pBtn->style()->unpolish(pBtn);
        pBtn->style()->polish(pBtn);
    }
}

void MiniPanel::appendLog(const QString& strLine, bool bError)
{
    const QString strTime = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString strText = QStringLiteral("%1 %2").arg(strTime, strLine).toHtmlEscaped();
    m_pLog->appendHtml(bError ? QStringLiteral("<span style='color:#DC2626'>%1</span>").arg(strText) : strText);
}

QString MiniPanel::currentScript() const
{
    return m_pComboScript->currentText();
}

void MiniPanel::closeEvent(QCloseEvent* pEvent)
{
    // 20261108 ZJH 关闭小窗 = 回主界面, 不退出程序
    pEvent->ignore();
    emit returnToMainRequested();
}
