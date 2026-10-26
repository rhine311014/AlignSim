// 20261026 ZJH 标准 Modbus TCP 协议实现
// 帧格式 (大端字节序 Big-Endian):
//   MBAP 头 (7 字节): TransactionId(2) + ProtocolId(2, 必须为 0x0000) + Length(2, UnitId+PDU 字节数) + UnitId(1)
//   PDU:
//     0x03 / 0x04 读保持/输入寄存器: 请求 [FC(1) Addr(2) Count(2)] → 响应 [FC(1) ByteCount(1) Data(2*Count)]
//     0x06        写单个保持寄存器 : 请求 [FC(1) Addr(2) Value(2)] → 响应 原样回显请求
//     0x10        写多个保持寄存器 : 请求 [FC(1) Addr(2) Count(2) ByteCount(1) Data(2*Count)] → 响应 [FC(1) Addr(2) Count(2)]
//   异常响应: [FC|0x80(1) ExceptionCode(1)]  (0x01 非法功能码, 0x02 非法数据地址, 0x03 非法数据值)
#pragma once

#include "comm/IPlcProtocol.h"

class ModbusTcpProtocol : public IPlcProtocol
{
public:
    QString name() const override { return QStringLiteral("Modbus TCP"); }
    FrameStatus extractFrame(const QByteArray& baBuffer, int& nLen) const override;
    QByteArray handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const override;
};
