// 20261026 ZJH Modbus TCP 协议实现
#include "comm/ModbusTcpProtocol.h"

#include "core/RegisterBank.h"

#include <cstdint>
#include <vector>

namespace {

uint16_t readBE16(const uint8_t* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

void appendBE16(QByteArray& ba, uint16_t uVal)
{
    ba.append(static_cast<char>((uVal >> 8) & 0xFF));
    ba.append(static_cast<char>(uVal & 0xFF));
}

QByteArray makeException(const uint8_t* pMbap, uint8_t uFunc, uint8_t uExCode)
{
    QByteArray baAns;
    baAns.reserve(9);
    baAns.append(reinterpret_cast<const char*>(pMbap), 4);  // TransactionId + ProtocolId(0)
    appendBE16(baAns, 3);                                   // Length = UnitId(1) + FC(1) + ExCode(1)
    baAns.append(static_cast<char>(pMbap[6]));              // UnitId
    baAns.append(static_cast<char>(uFunc | 0x80));
    baAns.append(static_cast<char>(uExCode));
    return baAns;
}

}  // namespace

IPlcProtocol::FrameStatus ModbusTcpProtocol::extractFrame(const QByteArray& baBuffer, int& nLen) const
{
    if (baBuffer.size() < 6) {
        return FrameStatus::Incomplete;  // MBAP 前 6 字节尚未收齐
    }
    const auto* p = reinterpret_cast<const uint8_t*>(baBuffer.constData());
    const uint16_t uProtoId = readBE16(p + 2);
    const uint16_t uMbapLen = readBE16(p + 4);
    // 20261026 ZJH ProtocolId 必须为 0, 且 Length 至少包含 UnitId(1)+FC(1)=2 字节、不超过 260 字节
    if (uProtoId != 0 || uMbapLen < 2 || uMbapLen > 260) {
        nLen = 1;
        return FrameStatus::Garbage;
    }
    const int nTotal = 6 + static_cast<int>(uMbapLen);
    if (baBuffer.size() < nTotal) {
        return FrameStatus::Incomplete;
    }
    nLen = nTotal;
    return FrameStatus::Complete;
}

QByteArray ModbusTcpProtocol::handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const
{
    const auto* p = reinterpret_cast<const uint8_t*>(baFrame.constData());
    const int nSize = baFrame.size();
    if (nSize < 8) {
        strLog = QStringLiteral("Modbus 帧过短");
        return {};
    }
    const uint8_t uUnitId = p[6];
    const uint8_t uFunc = p[7];

    // 0x03 读保持寄存器 / 0x04 读输入寄存器
    if (uFunc == 0x03 || uFunc == 0x04) {
        if (nSize != 12) {
            strLog = QStringLiteral("Modbus FC=0x%1 长度错误").arg(uFunc, 2, 16, QChar('0'));
            return makeException(p, uFunc, 0x03);
        }
        const uint16_t uAddr = readBE16(p + 8);
        const uint16_t uCount = readBE16(p + 10);
        if (uCount < 1 || uCount > 125) {
            strLog = QStringLiteral("Modbus 读数量越界: %1").arg(uCount);
            return makeException(p, uFunc, 0x03);
        }
        std::vector<uint16_t> vecWords;
        if (!bank.readWords(static_cast<int>(uAddr), static_cast<int>(uCount), vecWords)) {
            strLog = QStringLiteral("Modbus 读地址越界: D%1 x%2").arg(uAddr).arg(uCount);
            return makeException(p, uFunc, 0x02);
        }
        strLog = QStringLiteral("Modbus 读 D%1 x%2").arg(uAddr).arg(uCount);
        const uint8_t uByteCount = static_cast<uint8_t>(uCount * 2);
        QByteArray baAns;
        baAns.reserve(9 + uByteCount);
        baAns.append(reinterpret_cast<const char*>(p), 4);
        appendBE16(baAns, static_cast<uint16_t>(3 + uByteCount));
        baAns.append(static_cast<char>(uUnitId));
        baAns.append(static_cast<char>(uFunc));
        baAns.append(static_cast<char>(uByteCount));
        for (uint16_t uWord : vecWords) {
            appendBE16(baAns, uWord);
        }
        return baAns;
    }

    // 0x06 写单个保持寄存器
    if (uFunc == 0x06) {
        if (nSize != 12) {
            strLog = QStringLiteral("Modbus FC=0x06 长度错误");
            return makeException(p, uFunc, 0x03);
        }
        const uint16_t uAddr = readBE16(p + 8);
        const uint16_t uVal = readBE16(p + 10);
        if (!bank.writeWords(static_cast<int>(uAddr), { uVal })) {
            strLog = QStringLiteral("Modbus 写单字地址越界: D%1").arg(uAddr);
            return makeException(p, uFunc, 0x02);
        }
        strLog = QStringLiteral("Modbus 写 D%1 = 0x%2").arg(uAddr).arg(uVal, 4, 16, QChar('0'));
        return baFrame;  // 标准 0x06 响应原样回显请求帧
    }

    // 0x10 写多个保持寄存器
    if (uFunc == 0x10) {
        if (nSize < 13) {
            strLog = QStringLiteral("Modbus FC=0x10 长度过短");
            return makeException(p, uFunc, 0x03);
        }
        const uint16_t uAddr = readBE16(p + 8);
        const uint16_t uCount = readBE16(p + 10);
        const uint8_t uByteCount = p[12];
        if (uCount < 1 || uCount > 123 || uByteCount != uCount * 2 || nSize != 13 + uByteCount) {
            strLog = QStringLiteral("Modbus 写多字数量/字节数非法");
            return makeException(p, uFunc, 0x03);
        }
        std::vector<uint16_t> vecWords(uCount);
        for (uint16_t i = 0; i < uCount; ++i) {
            vecWords[i] = readBE16(p + 13 + i * 2);
        }
        if (!bank.writeWords(static_cast<int>(uAddr), vecWords)) {
            strLog = QStringLiteral("Modbus 写多字地址越界: D%1 x%2").arg(uAddr).arg(uCount);
            return makeException(p, uFunc, 0x02);
        }
        strLog = QStringLiteral("Modbus 写 D%1 x%2").arg(uAddr).arg(uCount);
        QByteArray baAns;
        baAns.reserve(12);
        baAns.append(reinterpret_cast<const char*>(p), 4);
        appendBE16(baAns, 6);  // UnitId(1) + FC(1) + Addr(2) + Count(2)
        baAns.append(static_cast<char>(uUnitId));
        baAns.append(static_cast<char>(uFunc));
        appendBE16(baAns, uAddr);
        appendBE16(baAns, uCount);
        return baAns;
    }

    strLog = QStringLiteral("Modbus 不支持的功能码: 0x%1").arg(uFunc, 2, 16, QChar('0'));
    return makeException(p, uFunc, 0x01);
}
