// 20261024 ZJH 三菱 MC 3E ASCII 协议实现
// 字段偏移 (字符): 副头0-3 网络4-5 PC6-7 IO8-11 站12-13 长度14-17 | 定时器18-21 指令22-25 子指令26-29 软元件30-31 地址32-37 点数38-41 数据42-
#include "comm/McAsciiProtocol.h"

#include "core/RegisterBank.h"

#include <vector>

namespace {

constexpr int s_nHeaderLen = 18;        // 长度字段之前(含)的固定头字符数
constexpr int s_nMaxDataLen = 8192;     // 长度字段合理上限 (960 点写 ≈ 3864 字符)
constexpr int s_nMaxPoints = 960;       // 单次点数上限
constexpr uint16_t s_nEndCmdError = 0xC059;
constexpr uint16_t s_nEndDeviceError = 0xC056;
constexpr uint16_t s_nEndLengthError = 0xC051;
constexpr uint16_t s_nEndDataLenError = 0xC061;

// 20261024 ZJH 解析定长十六进制字段; 含非十六进制字符时 bOk=false
int parseHex(const QByteArray& ba, int nPos, int nWidth, bool& bOk)
{
    // 20261024 ZJH 越界视为失败
    if (nPos + nWidth > ba.size()) {
        bOk = false;
        return 0;
    }
    const QByteArray baField = ba.mid(nPos, nWidth);  // 截取字段
    // 20261024 ZJH QByteArray::toInt 会接受前导空格/符号, 这里逐字符严格校验
    for (char ch : baField) {
        if (!isxdigit(static_cast<unsigned char>(ch))) {
            bOk = false;
            return 0;
        }
    }
    return baField.toInt(&bOk, 16);  // 返回字段数值
}

// 20261024 ZJH 数值 → 定长大写十六进制文本
QByteArray toHex(int nValue, int nWidth)
{
    return QByteArray::number(nValue, 16).toUpper().rightJustified(nWidth, '0');
}

// 20261024 ZJH 组装回复: "D000" + 路由信息原样回传 + 长度(4) + 结束代码(4) + 数据
QByteArray packAnswer(const QByteArray& baReq, uint16_t nEndCode, const QByteArray& baData)
{
    QByteArray baAns("D000");
    baAns.append(baReq.mid(4, 10).leftJustified(10, '0'));  // 网络/PC/IO/站
    baAns.append(toHex(4 + baData.size(), 4));               // 长度 = 结束代码 4 字符 + 数据
    baAns.append(toHex(nEndCode, 4));
    baAns.append(baData);
    return baAns;  // 返回完整回复帧
}

// 20261024 ZJH 异常回复: 附 18 字符错误信息 (路由 10 + 指令 4 + 子指令 4)
QByteArray packError(const QByteArray& baReq, uint16_t nEndCode)
{
    QByteArray baErrInfo = baReq.mid(4, 10).leftJustified(10, '0');
    baErrInfo.append(baReq.mid(22, 8).leftJustified(8, '0'));
    return packAnswer(baReq, nEndCode, baErrInfo);  // 返回异常回复帧
}

}  // namespace

McAsciiProtocol::McAsciiProtocol(const QString& strName)
    : m_strName(strName)
{
}

IPlcProtocol::FrameStatus McAsciiProtocol::extractFrame(const QByteArray& baBuffer, int& nLen) const
{
    static const QByteArray s_baSubHeader("5000");  // 请求副头
    // 20261024 ZJH 副头逐字符比对: 已收到的部分若与 "5000" 不符立即判垃圾, 否则等
    const int nCmpLen = std::min<int>(4, static_cast<int>(baBuffer.size()));
    if (baBuffer.left(nCmpLen) != s_baSubHeader.left(nCmpLen)) {
        nLen = 1;  // 丢 1 字节重新同步
        return FrameStatus::Garbage;
    }
    // 20261024 ZJH 固定头没收全
    if (baBuffer.size() < s_nHeaderLen) {
        return FrameStatus::Incomplete;
    }
    bool bOk = false;
    const int nDataLen = parseHex(baBuffer, 14, 4, bOk);  // 长度字段: 定时器起到帧尾的字符数
    // 20261024 ZJH 长度字段非法 → 垃圾
    if (!bOk || nDataLen < 12 || nDataLen > s_nMaxDataLen) {
        nLen = 1;
        return FrameStatus::Garbage;
    }
    // 20261024 ZJH 数据区没收全
    if (baBuffer.size() < s_nHeaderLen + nDataLen) {
        return FrameStatus::Incomplete;
    }
    nLen = s_nHeaderLen + nDataLen;  // 完整帧长度
    return FrameStatus::Complete;
}

QByteArray McAsciiProtocol::handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const
{
    bool bOk = false;
    const int nCmd = parseHex(baFrame, 22, 4, bOk);  // 指令
    bool bOkSub = false;
    const int nSubCmd = parseHex(baFrame, 26, 4, bOkSub);  // 子指令
    // 20261024 ZJH 只支持 0401 / 1401 + 子指令 0000
    if (!bOk || !bOkSub || (nCmd != 0x0401 && nCmd != 0x1401) || nSubCmd != 0) {
        strLog = QStringLiteral("不支持的指令 %1").arg(QString::fromLatin1(baFrame.mid(22, 8)));
        return packError(baFrame, s_nEndCmdError);
    }
    // 20261024 ZJH 软元件必须是 "D*" (D 数据寄存器)
    if (baFrame.mid(30, 2) != "D*") {
        strLog = QStringLiteral("不支持的软元件 %1").arg(QString::fromLatin1(baFrame.mid(30, 2)));
        return packError(baFrame, s_nEndDeviceError);
    }
    const QByteArray baAddr = baFrame.mid(32, 6);  // 首地址 (6 位十进制)
    bool bOkAddr = baAddr.size() == 6;
    for (char ch : baAddr) {  // 严格校验全为数字
        bOkAddr = bOkAddr && isdigit(static_cast<unsigned char>(ch));
    }
    const int nAddr = bOkAddr ? baAddr.toInt() : -1;
    const int nPoints = parseHex(baFrame, 38, 4, bOk);  // 点数
    // 20261024 ZJH 地址/点数非法
    if (!bOkAddr || !bOk) {
        strLog = QStringLiteral("地址或点数格式错误");
        return packError(baFrame, s_nEndDataLenError);
    }
    if (nPoints <= 0 || nPoints > s_nMaxPoints) {
        strLog = QStringLiteral("点数超范围 %1").arg(nPoints);
        return packError(baFrame, s_nEndLengthError);
    }
    if (!RegisterBank::isRangeValid(nAddr, nPoints)) {
        strLog = QStringLiteral("地址越界 D%1 x%2").arg(nAddr).arg(nPoints);
        return packError(baFrame, s_nEndDeviceError);
    }

    // 20261024 ZJH 批量读: 每字 4 位十六进制
    if (nCmd == 0x0401) {
        std::vector<uint16_t> vecWords;  // 读出值
        bank.readWords(nAddr, nPoints, vecWords);
        QByteArray baData;
        for (uint16_t nWord : vecWords) {
            baData.append(toHex(nWord, 4));
        }
        strLog = QStringLiteral("读 D%1 x%2").arg(nAddr).arg(nPoints);
        return packAnswer(baFrame, 0, baData);  // 返回读回复
    }

    // 20261024 ZJH 批量写: 数据区必须正好 点数*4 字符
    if (baFrame.size() != 42 + nPoints * 4) {
        strLog = QStringLiteral("写数据长度不符");
        return packError(baFrame, s_nEndDataLenError);
    }
    std::vector<uint16_t> vecWords(nPoints);  // 待写入数据
    for (int i = 0; i < nPoints; ++i) {  // 逐字解析
        vecWords[i] = static_cast<uint16_t>(parseHex(baFrame, 42 + i * 4, 4, bOk));
        if (!bOk) {
            strLog = QStringLiteral("写数据含非法字符");
            return packError(baFrame, s_nEndDataLenError);
        }
    }
    bank.writeWords(nAddr, vecWords);
    strLog = QStringLiteral("写 D%1 x%2").arg(nAddr).arg(nPoints);
    return packAnswer(baFrame, 0, {});  // 返回写成功回复
}
