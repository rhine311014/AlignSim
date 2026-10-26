// 20261024 ZJH PLC 协议接口
// 每种协议负责两件事:
//   1) extractFrame: 从 TCP 接收缓冲区里切出一帧完整报文 (处理半包/粘包)
//   2) handleFrame : 解析一帧 → 读写 RegisterBank → 生成回复报文
// 协议对象无状态 (除配置外), 多个连接可共用同一个实例
#pragma once

#include <QByteArray>
#include <QString>

class RegisterBank;

class IPlcProtocol
{
public:
    // 20261024 ZJH 切帧结果
    enum class FrameStatus {
        Incomplete,  // 数据不足一帧, 等待更多数据
        Complete,    // 切出一帧, nLen = 帧长度
        Garbage      // 缓冲区开头是无法识别的垃圾数据, nLen = 需要丢弃的字节数
    };

    virtual ~IPlcProtocol() = default;

    // 20261024 ZJH 协议显示名称 (日志/界面用)
    virtual QString name() const = 0;

    // 20261024 ZJH 从缓冲区开头尝试切出一帧
    // 参数: baBuffer - 当前连接累计收到的数据; nLen - 输出帧长度或丢弃长度
    virtual FrameStatus extractFrame(const QByteArray& baBuffer, int& nLen) const = 0;

    // 20261024 ZJH 处理一帧完整报文并返回回复 (返回空表示不回复)
    // 参数: baFrame - extractFrame 切出的帧; bank - 寄存器区; strLog - 输出可读日志
    virtual QByteArray handleFrame(const QByteArray& baFrame, RegisterBank& bank, QString& strLog) const = 0;
};
