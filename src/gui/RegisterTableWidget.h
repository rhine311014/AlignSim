// 20261024 ZJH RegisterTableWidget —— 寄存器监视/编辑表
// 每行一个 D 寄存器: 地址 | 16位十进制 | 十六进制 | 32位(本字+下一字, 低字在前) | 浮点(同 32 位)
// 16位/32位/浮点列可双击编辑; 寄存器变化时合并刷新 (100ms 节流, 避免高频通信卡界面)
#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class RegisterBank;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

class RegisterTableWidget : public QWidget
{
    Q_OBJECT
public:
    static constexpr int s_nRowCount = 100;  // 一页显示行数

    explicit RegisterTableWidget(RegisterBank* pBank, QWidget* pParent = nullptr);

    void setStartAddress(int nAddr);
    int startAddress() const;

protected:
    void showEvent(QShowEvent* pEvent) override;  // 20261026 ZJH 切回本页签时补刷新

private slots:
    void onRegistersChanged(int nAddr, int nCount);  // 寄存器被写 → 若在可视范围内标记刷新
    void refresh();                                  // 重新读取并显示
    void onItemChanged(QTableWidgetItem* pItem);     // 用户编辑单元格

private:
    QString descriptionFor(int nAddr) const;         // 20261026 ZJH 取某地址的协议说明 (用户自定义优先)
    void loadCustomDescriptions();                   // 从 config/register_notes.json 加载用户备注
    void saveCustomDescriptions() const;             // 保存用户备注到 config/register_notes.json

    RegisterBank* m_pBank = nullptr;     // 寄存器区 (不持有)
    QSpinBox* m_pSpinStart = nullptr;    // 起始地址
    QTableWidget* m_pTable = nullptr;    // 表格
    QTimer* m_pRefreshTimer = nullptr;   // 节流定时器
    QHash<int, QString> m_hashCustomDesc;// 20261026 ZJH 用户自定义协议说明备注 (地址 → 备注文本)
    bool m_bUpdating = false;            // 程序填表中, 忽略 itemChanged
    bool m_bDirty = false;               // 20261026 ZJH 不可见期间寄存器有变化, 待显示时刷新
};
