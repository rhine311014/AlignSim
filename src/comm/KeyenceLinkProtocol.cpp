// 20261024 ZJH 基恩士上位链路协议实现
// 解析策略: 去掉 CR 后按空白切分为 token, 逐项解析 (不依赖固定列宽, 兼容 "DM100" / "DM00100" / 带或不带后缀)
#include "comm/KeyenceLinkProtocol.h"

#include "core/RegisterBank.h"

#include <QList>
#include <vector>

namespace {

const QByteArray s_baOk("OK\r\n");   // 写成功回复
const QByteArray s_baE0("E0\r\n");   // 软元件编号异常
const QByteArray s_baE1("E1\r\n");   // 指令异常 (格式/个数/数据错误)

// 20261024 ZJH 数据格式后缀
enum class DataFormat { U16, S16, U32, S32, H16 };

// 20261024 ZJH 解析后的软元件描述
struct DeviceRef {
    int nAddr = 0;                        // D 寄存器地址
    DataFormat eFormat = DataFormat::U16; // 数据格式
    int nWordsPerItem = 1;                // 每个元素占几个字 (32 位格式占 2)
};

// 20261024 ZJH 解析 "DM100.L" 这类软元件 token
// 返回: 0 成功; 1 软元件编号异常(E0); 2 格式异常(E1)
int parseDevice(const QByteArray& baToken, DeviceRef& dev)
{
    QByteArray baName = baToken.toUpper();  // 统一大写
    QByteArray baSuffix;                    // 后缀 (不含点)
    const int nDot = baName.indexOf('.');
    // 20261024 ZJH 拆出后缀
    if (nDot >= 0) {
        baSuffix = baName.mid(nDot + 1);
        baName = baName.left(nDot);
    }
    // 20261024 ZJH 后缀 → 数据格式
    if (baSuffix.isEmpty() || baSuffix == "U") {
        dev.eFormat = DataFormat::U16;
    } else if (baSuffix == "S") {
        dev.eFormat = DataFormat::S16;
    } else if (baSuffix == "D") {
        dev.eFormat = DataFormat::U32;
    } else if (baSuffix == "L") {
        dev.eFormat = DataFormat::S32;
    } else if (baSuffix == "H") {
        dev.eFormat = DataFormat::H16;
    } else {
        return 2;  // 未知后缀 → 指令异常
    }
    dev.nWordsPerItem = (dev.eFormat == DataFormat::U32 || dev.eFormat == DataFormat::S32) ? 2 : 1;

    // 20261024 ZJH 软元件类型: DM 或 D (都映射 D 寄存器), 其余类型不支持
    QByteArray baNum;  // 地址数字部分
    if (baName.startsWith("DM")) {
        baNum = baName.mid(2);
    } else if (baName.startsWith("D")) {
        baNum = baName.mid(1);
    } else {
        return 1;  // 不支持的软元件 → E0
    }
    bool bOk = !baNum.isEmpty();
    for (char ch : baNum) {  // 地址必须全是数字
        bOk = bOk && isdigit(static_cast<unsigned char>(ch));
    }
    // 20261024 ZJH 位数过多 (>6) 直接视为越界, 防止 toInt 溢出
    if (!bOk || baNum.size() > 6) {
        return 1;
    }
    dev.nAddr = baNum.toInt();
    return 0;  // 解析成功
}

// 20261024 ZJH 按格式把寄存器值格式化为文本 (pWords 指向该元素的首字)
QByteArray formatValue(const uint16_t* pWords, DataFormat eFormat)
{
    switch (eFormat) {
    case DataFormat::S16: {
        const int nValue = static_cast<int16_t>(pWords[0]);  // 有符号 16 位
        return QByteArray(nValue < 0 ? "-" : "+") + QByteArray::number(std::abs(nValue)).rightJustified(5, '0');
    }
    case DataFormat::U32: {
        const quint32 nValue = pWords[0] | (static_cast<quint32>(pWords[1]) << 16);  // 低字在前
        return QByteArray::number(nValue).rightJustified(10, '0');
    }
    case DataFormat::S32: {
        const qint32 nValue = static_cast<qint32>(pWords[0] | (static_cast<quint32>(pWords[1]) << 16));
        const qint64 nAbs = std::abs(static_cast<qint64>(nValue));  // 用 64 位取绝对值, 防 INT_MIN 溢出
        return QByteArray(nValue < 0 ? "-" : "+") + QByteArray::number(nAbs).rightJustified(10, '0');
    }
    case DataFormat::H16:
        return QByteArray::number(pWords[0], 16).toUpper().rightJustified(4, '0');
    case DataFormat::U16:
    default:
        return QByteArray::number(pWords[0]).rightJustified(5, '0');
    }
}

// 20261024 ZJH 把一个数据 token 按格式解析成 1~2 个字, 追加到 vecWords; 失败返回 false
bool parseValue(const QByteArray& baToken, DataFormat eFormat, std::vector<uint16_t>& vecWords)
{
    bool bOk = false;  // 数值解析结果
    // 20261024 ZJH 十六进制格式
    if (eFormat == DataFormat::H16) {
        const uint nValue = baToken.toUInt(&bOk, 16);
        if (!bOk || nValue > 0xFFFF) {
            return false;
        }
        vecWords.push_back(static_cast<uint16_t>(nValue));
        return true;
    }
    const qint64 nValue = baToken.toLongLong(&bOk, 10);  // 十进制, 允许 +/- 号
    if (!bOk) {
        return false;
    }
    // 20261024 ZJH 按格式检查范围 (16 位允许 -32768~65535, 兼容有/无符号写法)
    if (eFormat == DataFormat::U32 || eFormat == DataFormat::S32) {
        if (nValue < INT32_MIN || nValue > static_cast<qint64>(UINT32_MAX)) {
            return false;
        }
        const quint32 nRaw = static_cast<quint32>(nValue);  // 截成 32 位原始位
        vecWords.push_back(static_cast<uint16_t>(nRaw & 0xFFFF));  // 低字在前
        vecWords.push_back(static_cast<uint16_t>(nRaw >> 16));
        return true;
    }
    if (nValue < INT16_MIN || nValue > UINT16_MAX) {
        return false;
    }
    vecWords.push_back(static_cast<uint16_t>(nValue));
    return true;  // 解析成功
}

}  // namespace

IPlcProtocol::FrameStatus KeyenceLinkProtocol::extractFrame(const QByteArray& baBuffer, int& nLen) const
{
    // 20261024 ZJH 空缓冲直接等
    if (baBuffer.isEmpty()) {
        return FrameStatus::Incomplete;
    }
    // 20261024 ZJH 客户端用 CRLF 结尾时, 上一帧之后会残留 LF; 开头的空白字符逐个丢弃
    if (baBuffer[0] == '\n' || baBuffer[0] == '\0') {
        nLen = 1;
        return FrameStatus::Garbage;
    }
    const int nCr = baBuffer.indexOf('\r');  // 帧结束符位置
    // 20261024 ZJH 没收到 CR: 行太长判垃圾整段丢弃, 否则继续等
    if (nCr < 0) {
        if (baBuffer.size() > s_nMaxLineLength) {
            nLen = static_cast<int>(baBuffer.size());
            return FrameStatus::Garbage;
        }
        return FrameStatus::Incomplete;
    }
    nLen = nCr + 1;  // 帧长度含 CR
    return FrameStatus::Complete;
}

QByteArray KeyenceLinkProtocol::handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const
{
    // 20261024 ZJH 去掉结尾 CR/LF 后按空白切分
    const QList<QByteArray> lstTokens = baFrame.trimmed().simplified().split(' ');
    const QByteArray baCmd = lstTokens.value(0).toUpper();  // 指令字
    strLog = QString::fromLatin1(baFrame.trimmed());

    // 20261024 ZJH 握手类指令
    if (baCmd == "CR") {
        return "CC\r\n";  // 通信模式切换应答
    }
    if (baCmd == "?K") {
        return "55\r\n";  // 机型代码: 55 = KV-8000 (视觉软件只做存在性检查)
    }

    const bool bRead = (baCmd == "RD" || baCmd == "RDS");    // 是否读指令
    const bool bWrite = (baCmd == "WR" || baCmd == "WRS");   // 是否写指令
    const bool bMulti = (baCmd == "RDS" || baCmd == "WRS");  // 是否连续读写
    // 20261024 ZJH 未知指令 / 缺少软元件参数
    if ((!bRead && !bWrite) || lstTokens.size() < 2) {
        return s_baE1;
    }

    DeviceRef dev;  // 软元件
    const int nDevRet = parseDevice(lstTokens[1], dev);
    if (nDevRet == 1) {
        return s_baE0;  // 软元件编号异常
    }
    if (nDevRet == 2) {
        return s_baE1;  // 后缀异常
    }

    int nCount = 1;      // 元素个数
    int nDataIndex = 2;  // 写数据起始 token 下标
    // 20261024 ZJH 连续读写: 第 3 个 token 是个数
    if (bMulti) {
        bool bOk = false;
        nCount = lstTokens.value(2).toInt(&bOk);
        if (!bOk || nCount <= 0 || nCount > s_nMaxCount) {
            return s_baE1;
        }
        nDataIndex = 3;
    }
    const int nWordCount = nCount * dev.nWordsPerItem;  // 实际占用字数
    // 20261024 ZJH 地址越界 → E0
    if (!RegisterBank::isRangeValid(dev.nAddr, nWordCount)) {
        return s_baE0;
    }

    // 20261024 ZJH 读: 每个元素格式化后用空格连接
    if (bRead) {
        // 20261024 ZJH 读指令不应带多余参数
        if (lstTokens.size() != nDataIndex) {
            return s_baE1;
        }
        std::vector<uint16_t> vecWords;
        bank.readWords(dev.nAddr, nWordCount, vecWords);
        QByteArray baAns;
        for (int i = 0; i < nCount; ++i) {  // 逐元素格式化
            if (i > 0) {
                baAns.append(' ');
            }
            baAns.append(formatValue(&vecWords[i * dev.nWordsPerItem], dev.eFormat));
        }
        return baAns + "\r\n";  // 返回读回复
    }

    // 20261024 ZJH 写: 数据 token 个数必须正好等于 nCount
    if (lstTokens.size() != nDataIndex + nCount) {
        return s_baE1;
    }
    std::vector<uint16_t> vecWords;  // 待写入字
    vecWords.reserve(nWordCount);
    for (int i = 0; i < nCount; ++i) {  // 逐个解析数据
        if (!parseValue(lstTokens[nDataIndex + i], dev.eFormat, vecWords)) {
            return s_baE1;
        }
    }
    bank.writeWords(dev.nAddr, vecWords);
    return s_baOk;  // 返回写成功
}
