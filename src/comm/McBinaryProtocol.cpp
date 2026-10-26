// 20261024 ZJH 三菱 MC 3E 二进制协议实现
#include "comm/McBinaryProtocol.h"

#include "core/RegisterBank.h"

#include <vector>

namespace {

constexpr int s_nHeaderLen = 9;            // 数据长度字段之前(含)的固定头长度: 副头2+网络1+PC1+IO2+站1+长度2
constexpr int s_nMaxDataLen = 4096;        // 数据长度字段的合理上限, 超过视为垃圾 (960 点写 = 1932 字节)
constexpr uint8_t s_nDeviceCodeD = 0xA8;   // 软元件代码: D 数据寄存器
constexpr uint16_t s_nCmdRead = 0x0401;    // 批量读
constexpr uint16_t s_nCmdWrite = 0x1401;   // 批量写

// 20261024 ZJH 小端读取 16 位
uint16_t readU16(const QByteArray& ba, int nPos)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(ba[nPos]) | (static_cast<uint8_t>(ba[nPos + 1]) << 8));
}

// 20261024 ZJH 小端追加 16 位
void appendU16(QByteArray& ba, uint16_t nValue)
{
    ba.append(static_cast<char>(nValue & 0xFF));
    ba.append(static_cast<char>(nValue >> 8));
}

// 20261024 ZJH 组装回复帧: 副头 D0 00 + 原样回传路由信息(网络/PC/IO/站) + 长度 + 结束代码 + 数据
QByteArray packAnswer(const QByteArray& baReq, uint16_t nEndCode, const QByteArray& baData)
{
    QByteArray baAns;
    baAns.append('\xD0');
    baAns.append('\x00');
    baAns.append(baReq.mid(2, 5));  // 网络号 PC号 IO号(2) 站号, 原样回传
    appendU16(baAns, static_cast<uint16_t>(2 + baData.size()));  // 数据长度 = 结束代码 2 字节 + 数据
    appendU16(baAns, nEndCode);
    baAns.append(baData);
    return baAns;  // 返回完整回复帧
}

// 20261024 ZJH 组装异常回复: 结束代码后附 9 字节错误信息 (网络/PC/IO/站 + 指令 + 子指令)
QByteArray packError(const QByteArray& baReq, uint16_t nEndCode)
{
    QByteArray baErrInfo = baReq.mid(2, 5);  // 路由信息
    baErrInfo.append(baReq.mid(11, 4).leftJustified(4, '\0'));  // 指令 + 子指令 (不足时补 0)
    return packAnswer(baReq, nEndCode, baErrInfo);  // 返回异常回复帧
}

}  // namespace

McBinaryProtocol::McBinaryProtocol(const QString& strName)
    : m_strName(strName)
{
}

IPlcProtocol::FrameStatus McBinaryProtocol::extractFrame(const QByteArray& baBuffer, int& nLen) const
{
    // 20261024 ZJH 副头没收全, 先等
    if (baBuffer.size() < 2) {
        return FrameStatus::Incomplete;
    }
    // 20261024 ZJH 副头必须是 50 00, 否则丢 1 字节重新同步
    if (static_cast<uint8_t>(baBuffer[0]) != 0x50 || static_cast<uint8_t>(baBuffer[1]) != 0x00) {
        nLen = 1;
        return FrameStatus::Garbage;
    }
    // 20261024 ZJH 固定头没收全, 先等
    if (baBuffer.size() < s_nHeaderLen) {
        return FrameStatus::Incomplete;
    }
    const int nDataLen = readU16(baBuffer, 7);  // 数据长度字段: 从监视定时器到帧尾的字节数
    // 20261024 ZJH 长度不合理 (比 "定时器+指令+子指令" 还短, 或过大) → 当垃圾丢 1 字节
    if (nDataLen < 6 || nDataLen > s_nMaxDataLen) {
        nLen = 1;
        return FrameStatus::Garbage;
    }
    // 20261024 ZJH 数据区没收全, 先等
    if (baBuffer.size() < s_nHeaderLen + nDataLen) {
        return FrameStatus::Incomplete;
    }
    nLen = s_nHeaderLen + nDataLen;  // 完整帧长度
    return FrameStatus::Complete;
}

QByteArray McBinaryProtocol::handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const
{
    // 20261024 ZJH 至少要有到子指令为止的 15 字节
    if (baFrame.size() < 15) {
        strLog = QStringLiteral("帧过短");
        return packError(baFrame.leftJustified(15, '\0'), s_nEndDataLenError);
    }
    const uint16_t nCmd = readU16(baFrame, 11);     // 指令
    const uint16_t nSubCmd = readU16(baFrame, 13);  // 子指令
    // 20261024 ZJH 只支持批量读/写 + 字单位子指令 0000
    if ((nCmd != s_nCmdRead && nCmd != s_nCmdWrite) || nSubCmd != 0x0000) {
        strLog = QStringLiteral("不支持的指令 %1 子指令 %2").arg(nCmd, 4, 16, QChar('0')).arg(nSubCmd, 4, 16, QChar('0'));
        return packError(baFrame, s_nEndCmdError);
    }
    // 20261024 ZJH 读写都需要: 首地址3 + 软元件代码1 + 点数2 = 到第 21 字节
    if (baFrame.size() < 21) {
        strLog = QStringLiteral("请求数据不完整");
        return packError(baFrame, s_nEndDataLenError);
    }
    const int nAddr = static_cast<uint8_t>(baFrame[15])
                    | (static_cast<uint8_t>(baFrame[16]) << 8)
                    | (static_cast<uint8_t>(baFrame[17]) << 16);  // 首地址 (3 字节小端)
    const uint8_t nDevice = static_cast<uint8_t>(baFrame[18]);    // 软元件代码
    const int nPoints = readU16(baFrame, 19);                     // 点数
    // 20261024 ZJH 仅支持 D 寄存器
    if (nDevice != s_nDeviceCodeD) {
        strLog = QStringLiteral("不支持的软元件代码 0x%1").arg(nDevice, 2, 16, QChar('0'));
        return packError(baFrame, s_nEndDeviceError);
    }
    // 20261024 ZJH 点数范围检查
    if (nPoints <= 0 || nPoints > s_nMaxPoints) {
        strLog = QStringLiteral("点数超范围 %1").arg(nPoints);
        return packError(baFrame, s_nEndLengthError);
    }
    // 20261024 ZJH 地址越界检查
    if (!RegisterBank::isRangeValid(nAddr, nPoints)) {
        strLog = QStringLiteral("地址越界 D%1 x%2").arg(nAddr).arg(nPoints);
        return packError(baFrame, s_nEndDeviceError);
    }

    // 20261024 ZJH 批量读: 按小端逐字打包
    if (nCmd == s_nCmdRead) {
        std::vector<uint16_t> vecWords;  // 读出的寄存器值
        bank.readWords(nAddr, nPoints, vecWords);
        QByteArray baData;  // 回复数据区
        for (uint16_t nWord : vecWords) {
            appendU16(baData, nWord);
        }
        strLog = QStringLiteral("读 D%1 x%2").arg(nAddr).arg(nPoints);
        return packAnswer(baFrame, s_nEndOk, baData);  // 返回读回复
    }

    // 20261024 ZJH 批量写: 数据区长度必须正好等于 点数*2
    if (baFrame.size() != 21 + nPoints * 2) {
        strLog = QStringLiteral("写数据长度不符");
        return packError(baFrame, s_nEndDataLenError);
    }
    std::vector<uint16_t> vecWords(nPoints);  // 待写入数据
    for (int i = 0; i < nPoints; ++i) {  // 逐字解析写数据
        vecWords[i] = readU16(baFrame, 21 + i * 2);
    }
    bank.writeWords(nAddr, vecWords);
    strLog = QStringLiteral("写 D%1 x%2").arg(nAddr).arg(nPoints);
    return packAnswer(baFrame, s_nEndOk, {});  // 返回写成功回复 (无数据)
}
