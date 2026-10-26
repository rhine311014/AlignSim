// 20261024 ZJH PlcServer —— 模拟 PLC 的 TCP 服务端
// 视觉软件作为客户端连接进来读写寄存器; 每个连接维护独立接收缓冲区, 用协议的 extractFrame 处理半包/粘包
// 运行在主线程 (Qt 事件循环驱动), 所有槽函数都在主线程执行
#pragma once

#include "comm/IPlcProtocol.h"
#include "comm/ProtocolFactory.h"

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <memory>

class QTcpSocket;
class QTimer;
class RegisterBank;

class PlcServer : public QObject
{
    Q_OBJECT
public:
    explicit PlcServer(RegisterBank* pBank, QObject* pParent = nullptr);
    ~PlcServer() override;

    // 20261024 ZJH 【服务端模式】按指定协议在 strIp:nPort 开始监听; 已在监听/连接时先停止
    bool start(ProtocolType eType, const QString& strIp, int nPort);
    // 20261026 ZJH 【客户端模式】作为 TCP 客户端主动连接远端服务端 strIp:nPort (断线自动重连, 既可响应远端指令也可主动同步寄存器)
    bool startClient(ProtocolType eType, const QString& strIp, int nPort);
    // 20261024 ZJH 停止监听或客户端连接并断开所有套接字
    void stop();

    // 20261026 ZJH 是否输出逐帧收发日志; 关闭后不再做十六进制/文本转换, 高频轮询时省掉主线程开销
    void setFrameLogEnabled(bool bEnabled) { m_bFrameLog = bEnabled; }
    bool isFrameLogEnabled() const { return m_bFrameLog; }

    // 20261026 ZJH 客户端模式下是否作为主站主动向远端发送读写请求同步 D100~D169 握手区
    void setClientMasterPoll(bool bEnabled);
    bool isClientMasterPoll() const { return m_bClientMasterPoll; }

    // 20261026 ZJH 客户端模式下主动向远端发送一次批量写/读请求
    bool clientSendWrite(int nStartAddr, int nWordCount);
    bool clientSendRead(int nStartAddr, int nWordCount);

    bool isClientMode() const { return m_bClientMode; }
    bool isListening() const { return m_server.isListening(); }
    bool isClientRunning() const { return m_bClientMode && m_bClientRunning; }
    bool isClientConnected() const;
    bool isActive() const { return isListening() || isClientRunning(); }
    int clientCount() const { return static_cast<int>(m_hashBuffers.size()); }
    QString targetEndpoint() const { return QStringLiteral("%1:%2").arg(m_strClientHost).arg(m_nClientPort); }
    QString errorString() const { return m_server.errorString(); }

signals:
    // 20261024 ZJH 通信日志 (收发报文/解析结果), bError 表示异常类日志
    void logMessage(const QString& strMsg, bool bError);
    // 20261024 ZJH 客户端连接数或连接状态变化
    void clientCountChanged(int nCount);

private slots:
    void onNewConnection();     // [服务端] 有新客户端接入
    void onClientConnected();   // [客户端] 成功连上远端服务端
    void onReadyRead();         // 某套接字有数据可读
    void onDisconnected();      // 某套接字断开
    void onReconnectTick();     // [客户端] 断线自动重连定时尝试
    void onMasterPollTick();    // [客户端] 主站定时轮询远端握手寄存器
    void onLocalRegistersChanged(int nAddr, int nCount);  // [客户端] 本机寄存器变化时主动推送远端

private:
    // 20261024 ZJH 把报文转成日志文本: ASCII 协议显示可读文本, 二进制显示十六进制
    QString frameToText(const QByteArray& baFrame) const;
    // 20261026 ZJH 客户端主站模式: 识别并消费远端服务端返回的读/写应答帧; 返回 0 表示不是应答帧(交由被动协议处理), >0 表示消费字节数
    int tryConsumeClientResponse(const QByteArray& baBuffer);
    QByteArray buildClientReadFrame(int nAddr, int nCount) const;
    QByteArray buildClientWriteFrame(int nAddr, int nCount) const;

    RegisterBank* m_pBank = nullptr;                // 共享寄存器区 (不持有)
    QTcpServer m_server;                            // 监听服务 (服务端模式)
    QTcpSocket* m_pClientSocket = nullptr;          // 主动连接套接字 (客户端模式)
    QTimer* m_pReconnectTimer = nullptr;            // 客户端断线重连定时器
    QTimer* m_pPollTimer = nullptr;                 // 客户端主站轮询定时器
    bool m_bClientMode = false;                     // 当前是否为客户端模式
    bool m_bClientRunning = false;                  // 客户端是否处于启动状态
    bool m_bClientMasterPoll = true;                // 客户端是否主动读写远端寄存器
    bool m_bUpdatingFromRemote = false;             // 防止远端回写本机时再次触发外推
    QString m_strClientHost;                        // 客户端目标 IP
    int m_nClientPort = 7110;                       // 客户端目标端口
    int m_nLastReadAddr = 100;                      // 最近一次主动读起始地址
    int m_nLastReadCount = 70;                      // 最近一次主动读字数
    bool m_bLastReqWasWrite = false;                // 最近一次主动请求是否为写 (用于 Keyence 文本应答区分)
    std::unique_ptr<IPlcProtocol> m_pProtocol;      // 当前协议
    ProtocolType m_eType = ProtocolType::McBinary;  // 当前协议类型
    QHash<QTcpSocket*, QByteArray> m_hashBuffers;   // 每个连接的接收缓冲区
    bool m_bFrameLog = true;                        // 是否输出逐帧收发日志
};
