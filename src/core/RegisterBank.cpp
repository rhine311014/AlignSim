// 20261024 ZJH RegisterBank 实现
#include "core/RegisterBank.h"

#include <QMutexLocker>
#include <cstring>

RegisterBank::RegisterBank(QObject* pParent)
    : QObject(pParent)
    , m_vecWords(s_nRegisterCount, 0)  // 20261024 ZJH 上电全部清零
{
}

bool RegisterBank::isRangeValid(int nAddr, int nCount)
{
    // 20261024 ZJH 地址非负、数量为正、尾地址不超过总数 (用 long long 防止相加溢出)
    return nAddr >= 0 && nCount > 0
        && static_cast<long long>(nAddr) + nCount <= s_nRegisterCount;
}

bool RegisterBank::readWords(int nAddr, int nCount, std::vector<uint16_t>& vecOut) const
{
    // 20261024 ZJH 越界直接拒绝, 由协议层回复错误码
    if (!isRangeValid(nAddr, nCount)) {
        return false;  // 返回失败: 地址越界
    }
    QMutexLocker locker(&m_mutex);  // 加锁读取, 防止读到写了一半的双字
    vecOut.assign(m_vecWords.begin() + nAddr, m_vecWords.begin() + nAddr + nCount);
    return true;  // 返回成功
}

bool RegisterBank::writeWords(int nAddr, const std::vector<uint16_t>& vecWords)
{
    const int nCount = static_cast<int>(vecWords.size());  // 写入字数
    // 20261024 ZJH 越界直接拒绝, 不做部分写入
    if (!isRangeValid(nAddr, nCount)) {
        return false;  // 返回失败: 地址越界或空数据
    }
    {
        QMutexLocker locker(&m_mutex);  // 加锁写入
        std::copy(vecWords.begin(), vecWords.end(), m_vecWords.begin() + nAddr);
    }
    // 20261024 ZJH 解锁后再发信号, 避免槽函数里回读寄存器时死锁
    emit registersChanged(nAddr, nCount);
    return true;  // 返回成功
}

int16_t RegisterBank::getInt16(int nAddr) const
{
    std::vector<uint16_t> vecWords;  // 读出的 1 个字
    // 20261024 ZJH 越界返回 0
    if (!readWords(nAddr, 1, vecWords)) {
        return 0;
    }
    return static_cast<int16_t>(vecWords[0]);  // 返回有符号 16 位值
}

void RegisterBank::setInt16(int nAddr, int16_t nValue)
{
    writeWords(nAddr, { static_cast<uint16_t>(nValue) });  // 越界时 writeWords 内部忽略
}

int32_t RegisterBank::getInt32(int nAddr) const
{
    std::vector<uint16_t> vecWords;  // 读出的 2 个字 (低字, 高字)
    // 20261024 ZJH 越界返回 0
    if (!readWords(nAddr, 2, vecWords)) {
        return 0;
    }
    const uint32_t nRaw = static_cast<uint32_t>(vecWords[0])
                        | (static_cast<uint32_t>(vecWords[1]) << 16);  // 低字在前拼成 32 位
    return static_cast<int32_t>(nRaw);  // 返回有符号 32 位值
}

void RegisterBank::setInt32(int nAddr, int32_t nValue)
{
    const uint32_t nRaw = static_cast<uint32_t>(nValue);  // 按位解释为无符号, 便于拆字
    writeWords(nAddr, { static_cast<uint16_t>(nRaw & 0xFFFF),
                        static_cast<uint16_t>(nRaw >> 16) });  // 低字在前
}

float RegisterBank::getFloat(int nAddr) const
{
    const uint32_t nRaw = static_cast<uint32_t>(getInt32(nAddr));  // IEEE754 单精度原始位
    float fValue = 0.0f;  // 输出浮点值
    std::memcpy(&fValue, &nRaw, sizeof(fValue));  // memcpy 做类型双关, 避免严格别名问题
    return fValue;  // 返回浮点值
}

void RegisterBank::setFloat(int nAddr, float fValue)
{
    uint32_t nRaw = 0;  // IEEE754 原始位
    std::memcpy(&nRaw, &fValue, sizeof(nRaw));
    setInt32(nAddr, static_cast<int32_t>(nRaw));  // 复用双字写入
}

void RegisterBank::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        std::fill(m_vecWords.begin(), m_vecWords.end(), 0);  // 全部清零
    }
    emit registersChanged(0, s_nRegisterCount);  // 通知界面整体刷新
}
