// 20261024 ZJH 三菱 MC 协议 3E 帧 ASCII 格式
// 与二进制帧字段一一对应, 只是每个字段用十六进制文本表示 (软元件地址为十进制):
// 请求: "5000" 网络号"00" PC号"FF" IO号"03FF" 站号"00" 数据长度(4) 定时器(4) 指令"0401" 子指令"0000" 软元件"D*" 地址(6位十进制) 点数(4) [写数据 4位/字]
// 回复: "D000" "00" "FF" "03FF" "00" 数据长度(4) 结束代码(4) [读数据 4位/字]
#pragma once

#include "comm/IPlcProtocol.h"

class McAsciiProtocol : public IPlcProtocol
{
public:
    explicit McAsciiProtocol(const QString& strName = QStringLiteral("MC 3E ASCII"));

    QString name() const override { return m_strName; }
    FrameStatus extractFrame(const QByteArray& baBuffer, int& nLen) const override;
    QByteArray handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const override;

private:
    QString m_strName;  // 显示名
};
