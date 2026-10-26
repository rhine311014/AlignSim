// 20261024 ZJH 三菱 MC 协议 3E 帧 二进制格式 (QnA 兼容 3E)
// 支持: 0401 批量读字 / 1401 批量写字, 子命令 0000 (字单位), 软元件 D (代码 0xA8)
// 请求帧: 50 00 | 网络号 | PC号 | IO号(2,LE) | 站号 | 数据长度(2,LE) | 监视定时器(2) | 指令(2) | 子指令(2) | 首地址(3,LE) | 软元件代码 | 点数(2) | [写数据]
// 回复帧: D0 00 | 网络号 | PC号 | IO号 | 站号 | 数据长度(2) | 结束代码(2) | [读数据]
#pragma once

#include "comm/IPlcProtocol.h"

class McBinaryProtocol : public IPlcProtocol
{
public:
    // 20261024 ZJH 结束代码 (三菱手册定义, 非 0 表示异常)
    static constexpr uint16_t s_nEndOk = 0x0000;            // 正常
    static constexpr uint16_t s_nEndCmdError = 0xC059;      // 指令/子指令不支持
    static constexpr uint16_t s_nEndDeviceError = 0xC056;   // 软元件超范围
    static constexpr uint16_t s_nEndLengthError = 0xC051;   // 点数超范围
    static constexpr uint16_t s_nEndDataLenError = 0xC061;  // 请求数据长度与内容不符
    static constexpr int s_nMaxPoints = 960;                // 单次批量读写字点数上限 (手册值)

    explicit McBinaryProtocol(const QString& strName = QStringLiteral("MC 3E 二进制"));

    QString name() const override { return m_strName; }
    FrameStatus extractFrame(const QByteArray& baBuffer, int& nLen) const override;
    QByteArray handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const override;

private:
    QString m_strName;  // 显示名 (基恩士 KV 走 MC 兼容时显示不同名称)
};
