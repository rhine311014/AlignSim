// 20261024 ZJH 基恩士 KV 系列 上位链路协议 (TCP, 默认端口 8501)
// 指令以 CR 结尾, 回复以 CR LF 结尾, 字段之间用空格分隔:
//   RD  软元件[.后缀]                 → 单个读
//   RDS 软元件[.后缀] 个数            → 连续读
//   WR  软元件[.后缀] 数据            → 单个写
//   WRS 软元件[.后缀] 个数 数据1 ...  → 连续写
//   ?K → 机型代码;  CR → "CC" (切换通信模式, 兼容握手)
// 软元件: DM / D (都映射到寄存器区 D), 地址十进制
// 后缀: .U 无符号16位(默认) .S 有符号16位 .D 无符号32位 .L 有符号32位 .H 十六进制16位
// 错误回复: E0 软元件编号异常, E1 指令异常
#pragma once

#include "comm/IPlcProtocol.h"

class KeyenceLinkProtocol : public IPlcProtocol
{
public:
    static constexpr int s_nMaxCount = 1000;       // RDS/WRS 单次个数上限 (手册值)
    static constexpr int s_nMaxLineLength = 8192;  // 无 CR 的最大行长, 超过视为垃圾数据

    QString name() const override { return QStringLiteral("基恩士上位链路"); }
    FrameStatus extractFrame(const QByteArray& baBuffer, int& nLen) const override;
    QByteArray handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const override;
};
