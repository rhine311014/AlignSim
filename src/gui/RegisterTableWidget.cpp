// 20261024 ZJH RegisterTableWidget 实现
#include "gui/RegisterTableWidget.h"

#include "core/RegisterBank.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <vector>

namespace {
// 20261024 ZJH 列定义
enum Column { ColAddr = 0, ColInt16, ColHex, ColInt32, ColFloat, ColDesc, ColCount };

// 20261026 ZJH 常用握手与结果寄存器中文说明 (帮助联调时快速对照协议地址)
QString registerDescription(int nAddr)
{
    switch (nAddr) {
    case 100: return QStringLiteral("PLC→视觉: 触发拍照 (1=触发)");
    case 102: return QStringLiteral("PLC→视觉: 指令码 (1=FCLB标定,3=FAAC,4=FAAL,20=拾取)");
    case 104: return QStringLiteral("PLC→视觉: 场景号 / 工位号");
    case 106: return QStringLiteral("PLC→视觉: 当前爪号 (1=1号爪, 2=2号爪)");
    case 108: return QStringLiteral("PLC→视觉: 当前轴1 / 位姿X (Int32)");
    case 110: return QStringLiteral("PLC→视觉: 当前轴2 / 位姿Y (Int32)");
    case 112: return QStringLiteral("PLC→视觉: 当前轴3 / 位姿θ (Int32)");
    case 150: return QStringLiteral("视觉→PLC: 回复完成标志 (1=完成)");
    case 152: return QStringLiteral("视觉→PLC: 回复结果码 (1002=下一点,1001=标定完)");
    case 154: return QStringLiteral("视觉→PLC: 目标轴1 / 位姿X (Int32)");
    case 156: return QStringLiteral("视觉→PLC: 目标轴2 / 位姿Y (Int32)");
    case 158: return QStringLiteral("视觉→PLC: 目标轴3 / 位姿θ (Int32)");
    case 200: return QStringLiteral("结果回写: 是否吸到 / 对位完成");
    case 202: return QStringLiteral("结果回写: 残差/误差 ΔX (Float mm)");
    case 204: return QStringLiteral("结果回写: 残差/误差 ΔY (Float mm)");
    case 206: return QStringLiteral("结果回写: 残差/误差 Δθ (Float °)");
    case 208: return QStringLiteral("结果回写: 是否误吸反面 (1=反面)");
    case 300: return QStringLiteral("验证实时: 当前轮次");
    case 302: return QStringLiteral("验证实时: 本轮残差 |Δ| (Float mm)");
    case 304: return QStringLiteral("验证实时: 本轮残差 Δθ (Float °)");
    case 310: return QStringLiteral("验证汇总: 合格率 % (Float)");
    case 312: return QStringLiteral("验证汇总: X 3σ (Float µm)");
    case 314: return QStringLiteral("验证汇总: Y 3σ (Float µm)");
    case 316: return QStringLiteral("验证汇总: θ 3σ (Float °)");
    case 318: return QStringLiteral("验证汇总: 最大 |Δ| (Float µm)");
    default:  return QString();
    }
}
}

RegisterTableWidget::RegisterTableWidget(RegisterBank* pBank, QWidget* pParent)
    : QWidget(pParent)
    , m_pBank(pBank)
{
    auto* pMain = new QVBoxLayout(this);
    pMain->setContentsMargins(10, 10, 10, 10);
    pMain->setSpacing(8);

    auto* pTop = new QHBoxLayout();
    pTop->setSpacing(8);
    m_pSpinStart = new QSpinBox(this);
    m_pSpinStart->setRange(0, RegisterBank::s_nRegisterCount - s_nRowCount);
    m_pSpinStart->setPrefix(QStringLiteral("D"));
    m_pSpinStart->setSingleStep(s_nRowCount);
    auto* pBtnD0 = new QPushButton(QStringLiteral("D0 (基础区)"), this);
    auto* pBtnD100 = new QPushButton(QStringLiteral("D100 (握手区)"), this);
    pBtnD100->setProperty("primary", true);
    auto* pBtnD200 = new QPushButton(QStringLiteral("D200 (结果区)"), this);
    auto* pBtnD300 = new QPushButton(QStringLiteral("D300 (验证区)"), this);
    auto* pBtnClear = new QPushButton(QStringLiteral("全部寄存器清零"), this);
    pBtnClear->setProperty("danger", true);
    pTop->addWidget(new QLabel(QStringLiteral("起始地址:"), this));
    pTop->addWidget(m_pSpinStart);
    pTop->addWidget(pBtnD0);
    pTop->addWidget(pBtnD100);
    pTop->addWidget(pBtnD200);
    pTop->addWidget(pBtnD300);
    pTop->addStretch();
    pTop->addWidget(new QLabel(QStringLiteral("提示: 双击任意数值或「协议说明」单元格可直接修改并自动保存"), this));
    pTop->addWidget(pBtnClear);
    pMain->addLayout(pTop);

    m_pTable = new QTableWidget(s_nRowCount, ColCount, this);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setHorizontalHeaderLabels({ QStringLiteral("地址"), QStringLiteral("16位 (Int16)"), QStringLiteral("十六进制 (HEX)"),
                                          QStringLiteral("32位 (Int32 低字在前)"), QStringLiteral("浮点数 (Float)"), QStringLiteral("协议说明 (可双击编辑)") });
    m_pTable->verticalHeader()->setVisible(false);
    m_pTable->verticalHeader()->setDefaultSectionSize(24);
    m_pTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pTable->horizontalHeader()->setSectionResizeMode(ColDesc, QHeaderView::Interactive);
    m_pTable->setColumnWidth(ColDesc, 320);
    pMain->addWidget(m_pTable);

    m_pRefreshTimer = new QTimer(this);
    m_pRefreshTimer->setSingleShot(true);
    m_pRefreshTimer->setInterval(100);

    connect(m_pSpinStart, qOverload<int>(&QSpinBox::valueChanged), this, &RegisterTableWidget::refresh);
    connect(pBtnD0, &QPushButton::clicked, this, [this] { m_pSpinStart->setValue(0); });
    connect(pBtnD100, &QPushButton::clicked, this, [this] { m_pSpinStart->setValue(100); });
    connect(pBtnD200, &QPushButton::clicked, this, [this] { m_pSpinStart->setValue(200); });
    connect(pBtnD300, &QPushButton::clicked, this, [this] { m_pSpinStart->setValue(300); });
    connect(pBtnClear, &QPushButton::clicked, m_pBank, &RegisterBank::clear);
    connect(m_pRefreshTimer, &QTimer::timeout, this, &RegisterTableWidget::refresh);
    // 20261024 ZJH 寄存器写入可能来自脚本线程, 排队连接
    connect(m_pBank, &RegisterBank::registersChanged, this, &RegisterTableWidget::onRegistersChanged, Qt::QueuedConnection);
    connect(m_pTable, &QTableWidget::itemChanged, this, &RegisterTableWidget::onItemChanged);

    loadCustomDescriptions();
    refresh();
}

QString RegisterTableWidget::descriptionFor(int nAddr) const
{
    auto it = m_hashCustomDesc.find(nAddr);
    if (it != m_hashCustomDesc.end()) {
        return it.value();
    }
    return registerDescription(nAddr);
}

void RegisterTableWidget::loadCustomDescriptions()
{
    QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/config/register_notes.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool bOk = false;
        const int nAddr = it.key().toInt(&bOk);
        if (bOk && nAddr >= 0 && nAddr < RegisterBank::s_nRegisterCount) {
            m_hashCustomDesc.insert(nAddr, it.value().toString());
        }
    }
}

void RegisterTableWidget::saveCustomDescriptions() const
{
    const QString strPath = QCoreApplication::applicationDirPath() + QStringLiteral("/config/register_notes.json");
    QDir().mkpath(QFileInfo(strPath).absolutePath());
    QJsonObject obj;
    for (auto it = m_hashCustomDesc.begin(); it != m_hashCustomDesc.end(); ++it) {
        obj.insert(QString::number(it.key()), it.value());
    }
    QFile file(strPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

void RegisterTableWidget::setStartAddress(int nAddr)
{
    m_pSpinStart->setValue(nAddr);
}

int RegisterTableWidget::startAddress() const
{
    return m_pSpinStart->value();
}

void RegisterTableWidget::onRegistersChanged(int nAddr, int nCount)
{
    const int nStart = m_pSpinStart->value();
    // 20261024 ZJH 写入区间 [nAddr, nAddr+nCount) 与可视区间 [nStart-1, nStart+rows) 相交才刷新 (32位列会读到下一个字)
    if (nAddr + nCount >= nStart - 1 && nAddr <= nStart + s_nRowCount) {
        // 20261026 ZJH 页签不可见时只记脏标记, 切回来 (showEvent) 再刷新
        if (!isVisible()) {
            m_bDirty = true;
        } else if (!m_pRefreshTimer->isActive()) {
            m_pRefreshTimer->start();
        }
    }
}

void RegisterTableWidget::showEvent(QShowEvent* pEvent)
{
    QWidget::showEvent(pEvent);
    // 20261026 ZJH 隐藏期间有写入 → 显示时补一次刷新
    if (m_bDirty) {
        refresh();
    }
}

void RegisterTableWidget::refresh()
{
    m_bDirty = false;
    m_bUpdating = true;  // 屏蔽 itemChanged
    const int nStart = m_pSpinStart->value();
    // 20261026 ZJH 一次加锁读出整页 (+1 字给最后一行的 32 位列), 末页越界时少读一个字补 0
    std::vector<uint16_t> vecWords;
    const int nReadCount = std::min(s_nRowCount + 1, RegisterBank::s_nRegisterCount - nStart);
    m_pBank->readWords(nStart, nReadCount, vecWords);
    vecWords.resize(s_nRowCount + 1, 0);
    for (int nRow = 0; nRow < s_nRowCount; ++nRow) {  // 逐行刷新
        const int nAddr = nStart + nRow;
        const uint16_t nLow = vecWords[nRow];
        const uint32_t nRaw32 = static_cast<uint32_t>(nLow) | (static_cast<uint32_t>(vecWords[nRow + 1]) << 16);  // 低字在前
        // 20261026 ZJH 最后一个寄存器没有下一个字, 32 位/浮点列与 RegisterBank 越界读一致显示 0
        const bool bHas32 = nAddr + 1 < RegisterBank::s_nRegisterCount;
        float fValue = 0.0f;
        std::memcpy(&fValue, &nRaw32, sizeof(fValue));
        const QStringList lstText = {
            QStringLiteral("D%1").arg(nAddr), QString::number(static_cast<int16_t>(nLow)),
            QStringLiteral("%1").arg(nLow, 4, 16, QChar('0')).toUpper(),
            QString::number(bHas32 ? static_cast<int32_t>(nRaw32) : 0),
            QString::number(bHas32 ? fValue : 0.0f, 'g', 7),
            descriptionFor(nAddr) };
        const bool bNonZero = (nLow != 0 || (bHas32 && nRaw32 != 0));
        for (int nCol = 0; nCol < ColCount; ++nCol) {  // 逐列填写 (复用已有单元格, 减少分配)
            QTableWidgetItem* pItem = m_pTable->item(nRow, nCol);
            if (!pItem) {
                pItem = new QTableWidgetItem();
                // 20261026 ZJH 仅地址列只读; 16位、HEX、32位、浮点与协议说明列均可直接双击或打字编辑
                if (nCol == ColAddr) {
                    pItem->setFlags(pItem->flags() & ~Qt::ItemIsEditable);
                }
                m_pTable->setItem(nRow, nCol, pItem);
            }
            if (pItem->text() != lstText[nCol]) {
                pItem->setText(lstText[nCol]);
            }
            // 20261026 ZJH 非零寄存器行浅黄底高亮, 说明列深灰蓝字体
            if (nCol == ColDesc) {
                pItem->setForeground(QColor(60, 90, 140));
            }
            pItem->setBackground(bNonZero ? QColor(255, 250, 205) : Qt::transparent);
        }
    }
    m_bUpdating = false;
}

void RegisterTableWidget::onItemChanged(QTableWidgetItem* pItem)
{
    // 20261024 ZJH 程序填表时忽略
    if (m_bUpdating || !pItem) {
        return;
    }
    const int nAddr = m_pSpinStart->value() + pItem->row();
    bool bOk = false;
    // 20261024 ZJH 按列解析并写入; 输入非法时刷新回原值
    switch (pItem->column()) {
    case ColInt16: {
        const int nValue = pItem->text().toInt(&bOk);
        if (bOk && nValue >= -32768 && nValue <= 65535) {
            m_pBank->setInt16(nAddr, static_cast<int16_t>(nValue));
        }
        break;
    }
    case ColHex: {
        const uint nValue = pItem->text().trimmed().toUInt(&bOk, 16);
        if (bOk && nValue <= 0xFFFFu) {
            m_pBank->setInt16(nAddr, static_cast<int16_t>(static_cast<uint16_t>(nValue)));
        }
        break;
    }
    case ColInt32: {
        const qint64 nValue = pItem->text().toLongLong(&bOk);
        if (bOk && nValue >= INT32_MIN && nValue <= INT32_MAX && nAddr + 1 < RegisterBank::s_nRegisterCount) {
            m_pBank->setInt32(nAddr, static_cast<int32_t>(nValue));
        }
        break;
    }
    case ColFloat: {
        const float fValue = pItem->text().toFloat(&bOk);
        if (bOk && nAddr + 1 < RegisterBank::s_nRegisterCount) {
            m_pBank->setFloat(nAddr, fValue);
        }
        break;
    }
    case ColDesc: {
        m_hashCustomDesc.insert(nAddr, pItem->text().trimmed());
        saveCustomDescriptions();
        return;  // 备注编辑无需触发数值还原刷新
    }
    default:
        break;
    }
    m_pRefreshTimer->start();  // 无论成功与否都刷新, 非法输入被还原
}
