// 20261024 ZJH RegisterBank —— 模拟 PLC 的 D 寄存器区 (字寄存器, 每个 16 位)
// 所有通信协议与 Lua 脚本共用同一份寄存器, 线程安全 (网络线程/脚本线程/界面线程同时访问)
// 32 位数据约定: 低字在前 (D[n] = 低 16 位, D[n+1] = 高 16 位), 与三菱/基恩士双字一致
#pragma once

#include <QObject>
#include <QMutex>
#include <cstdint>
#include <vector>

class RegisterBank : public QObject
{
    Q_OBJECT
public:
    static constexpr int s_nRegisterCount = 65536;  // 寄存器总数: D0 ~ D65535

    explicit RegisterBank(QObject* pParent = nullptr);

    // 20261024 ZJH 判断 [nAddr, nAddr+nCount) 是否全部落在有效区间内
    static bool isRangeValid(int nAddr, int nCount);

    // 20261024 ZJH 批量读字; 越界返回 false 且不修改 vecOut
    bool readWords(int nAddr, int nCount, std::vector<uint16_t>& vecOut) const;
    // 20261024 ZJH 批量写字; 越界返回 false 且不写入任何数据
    bool writeWords(int nAddr, const std::vector<uint16_t>& vecWords);

    // 20261024 ZJH 按类型读写的便捷接口 (越界时读返回 0, 写忽略)
    int16_t getInt16(int nAddr) const;
    void setInt16(int nAddr, int16_t nValue);
    int32_t getInt32(int nAddr) const;
    void setInt32(int nAddr, int32_t nValue);
    float getFloat(int nAddr) const;
    void setFloat(int nAddr, float fValue);

    // 20261024 ZJH 全部清零
    void clear();

signals:
    // 20261024 ZJH 寄存器内容被写入后触发 (可能来自任意线程, 界面用排队连接接收)
    // nAddr: 起始地址; nCount: 被写入的字数
    void registersChanged(int nAddr, int nCount);

private:
    mutable QMutex m_mutex;              // 保护 m_vecWords 的互斥锁
    std::vector<uint16_t> m_vecWords;    // 寄存器存储区, 大小固定为 s_nRegisterCount
};
