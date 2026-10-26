// 20261024 ZJH PlcServer 实现 (支持 TCP 服务端监听 + TCP 客户端主动连接双模式)
#include "comm/PlcServer.h"

#include "core/RegisterBank.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#include <algorithm>

namespace {
constexpr int s_nMaxBufferSize = 1024 * 1024;  // 单连接缓冲上限 1MB, 防止异常客户端撑爆内存
}

PlcServer::PlcServer(RegisterBank* pBank, QObject* pParent)
    : QObject(pParent)
    , m_pBank(pBank)
{
    // 20261024 ZJH 新连接到来时接入
    connect(&m_server, &QTcpServer::newConnection, this, &PlcServer::onNewConnection);

    m_pReconnectTimer = new QTimer(this);
    m_pReconnectTimer->setInterval(2000);
    connect(m_pReconnectTimer, &QTimer::timeout, this, &PlcServer::onReconnectTick);

    m_pPollTimer = new QTimer(this);
    m_pPollTimer->setInterval(500);
    connect(m_pPollTimer, &QTimer::timeout, this, &PlcServer::onMasterPollTick);

    if (m_pBank) {
        // 20261026 ZJH 用 DirectConnection 在 writeWords 发生的同一时刻同步检查 m_bUpdatingFromRemote,
        // 防止网络回写触发 QueuedConnection 时 m_bUpdatingFromRemote 已复位导致死循环回写!
        connect(m_pBank, &RegisterBank::registersChanged, this, [this](int nAddr, int nCount) {
            if (m_bUpdatingFromRemote || !m_bClientMode || !m_bClientRunning || !m_bClientMasterPoll) {
                return;
            }
            QMetaObject::invokeMethod(this, [this, nAddr, nCount] {
                onLocalRegistersChanged(nAddr, nCount);
            }, Qt::QueuedConnection);
        }, Qt::DirectConnection);
    }
}

PlcServer::~PlcServer()
{
    stop();  // 析构时断开所有连接
}

bool PlcServer::start(ProtocolType eType, const QString& strIp, int nPort)
{
    stop();  // 先停掉旧的监听或客户端连接
    m_bClientMode = false;
    m_eType = eType;
    m_pProtocol = ProtocolFactory::create(eType);  // 创建协议实例
    // 20261024 ZJH IP 为空或 0.0.0.0 时监听所有网卡
    const QHostAddress addr = (strIp.isEmpty() || strIp == QStringLiteral("0.0.0.0"))
                                  ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(strIp);
    // 20261024 ZJH 端口范围校验
    if (nPort <= 0 || nPort > 65535) {
        emit logMessage(QStringLiteral("端口无效: %1").arg(nPort), true);
        return false;  // 返回失败: 端口非法
    }
    const bool bOk = m_server.listen(addr, static_cast<quint16>(nPort));
    // 20261024 ZJH 记录监听结果
    if (bOk) {
        emit logMessage(QStringLiteral("[服务端模式] 开始监听 %1:%2 [%3]").arg(addr.toString()).arg(nPort).arg(m_pProtocol->name()), false);
    } else {
        emit logMessage(QStringLiteral("[服务端模式] 监听失败: %1").arg(m_server.errorString()), true);
    }
    return bOk;  // 返回监听结果
}

bool PlcServer::startClient(ProtocolType eType, const QString& strIp, int nPort)
{
    stop();
    if (nPort <= 0 || nPort > 65535) {
        emit logMessage(QStringLiteral("端口无效: %1").arg(nPort), true);
        return false;
    }
    m_bClientMode = true;
    m_bClientRunning = true;
    m_eType = eType;
    m_pProtocol = ProtocolFactory::create(eType);
    m_strClientHost = (strIp.isEmpty() || strIp == QStringLiteral("0.0.0.0"))
                          ? QStringLiteral("127.0.0.1") : strIp.trimmed();
    m_nClientPort = nPort;

    m_pClientSocket = new QTcpSocket(this);
    m_pClientSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_pClientSocket, &QTcpSocket::connected, this, &PlcServer::onClientConnected);
    connect(m_pClientSocket, &QTcpSocket::readyRead, this, &PlcServer::onReadyRead);
    connect(m_pClientSocket, &QTcpSocket::disconnected, this, &PlcServer::onDisconnected);
    connect(m_pClientSocket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_pClientSocket && m_bClientRunning) {
            emit logMessage(QStringLiteral("[客户端模式] 连接 %1:%2 提示: %3 (将自动重试)")
                                .arg(m_strClientHost).arg(m_nClientPort).arg(m_pClientSocket->errorString()), true);
        }
    });

    emit logMessage(QStringLiteral("[客户端模式] 正在连接远端 %1:%2 [%3] ...")
                        .arg(m_strClientHost).arg(m_nClientPort).arg(m_pProtocol->name()), false);
    m_pClientSocket->connectToHost(m_strClientHost, static_cast<quint16>(m_nClientPort));
    m_pReconnectTimer->start();
    emit clientCountChanged(0);
    return true;
}

void PlcServer::stop()
{
    m_pReconnectTimer->stop();
    m_pPollTimer->stop();
    const bool bWasClient = m_bClientRunning;
    m_bClientRunning = false;

    // 20261024 ZJH 先取出所有连接再断开, 避免遍历时 onDisconnected 修改哈希表
    const QList<QTcpSocket*> lstSockets = m_hashBuffers.keys();
    m_hashBuffers.clear();
    for (QTcpSocket* pSocket : lstSockets) {  // 逐个断开并延迟释放
        if (pSocket != m_pClientSocket) {
            pSocket->disconnect(this);
            pSocket->abort();
            pSocket->deleteLater();
        }
    }
    if (m_pClientSocket) {
        m_pClientSocket->disconnect(this);
        m_pClientSocket->abort();
        m_pClientSocket->deleteLater();
        m_pClientSocket = nullptr;
    }
    // 20261024 ZJH 正在监听才关闭并记日志
    if (m_server.isListening()) {
        m_server.close();
        emit logMessage(QStringLiteral("[服务端模式] 停止监听"), false);
    } else if (bWasClient) {
        emit logMessage(QStringLiteral("[客户端模式] 已断开连接"), false);
    }
    emit clientCountChanged(0);
}

void PlcServer::setClientMasterPoll(bool bEnabled)
{
    m_bClientMasterPoll = bEnabled;
    m_pPollTimer->stop();  // 20261026 ZJH 不做后台定时轮询心跳, 仅在本机寄存器被修改时按需单次发送
}

bool PlcServer::isClientConnected() const
{
    return m_bClientMode && m_pClientSocket && m_pClientSocket->state() == QAbstractSocket::ConnectedState;
}

void PlcServer::onNewConnection()
{
    // 20261024 ZJH 一次可能有多个挂起连接, 循环全部取出
    while (QTcpSocket* pSocket = m_server.nextPendingConnection()) {
        m_hashBuffers.insert(pSocket, QByteArray());  // 为该连接建立空缓冲
        pSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);  // 关闭 Nagle, 降低回复延迟
        connect(pSocket, &QTcpSocket::readyRead, this, &PlcServer::onReadyRead);
        connect(pSocket, &QTcpSocket::disconnected, this, &PlcServer::onDisconnected);
        emit logMessage(QStringLiteral("客户端接入 %1:%2").arg(pSocket->peerAddress().toString()).arg(pSocket->peerPort()), false);
    }
    emit clientCountChanged(clientCount());
}

void PlcServer::onClientConnected()
{
    if (!m_pClientSocket) {
        return;
    }
    m_hashBuffers.insert(m_pClientSocket, QByteArray());
    emit logMessage(QStringLiteral("[客户端模式] 已成功连接远端 %1:%2 [%3]")
                        .arg(m_strClientHost).arg(m_nClientPort).arg(m_pProtocol ? m_pProtocol->name() : QString()), false);
    emit clientCountChanged(clientCount());
}

void PlcServer::onReconnectTick()
{
    if (!m_bClientMode || !m_bClientRunning || !m_pClientSocket) {
        m_pReconnectTimer->stop();
        return;
    }
    if (m_pClientSocket->state() == QAbstractSocket::UnconnectedState) {
        m_pClientSocket->connectToHost(m_strClientHost, static_cast<quint16>(m_nClientPort));
    }
}

void PlcServer::onMasterPollTick()
{
    if (!isClientConnected() || !m_bClientMasterPoll) {
        return;
    }
    // 20261026 ZJH 客户端主站定时读取远端 D150~D165 (视觉/远端PLC回复区)
    clientSendRead(150, 16);
}

void PlcServer::onLocalRegistersChanged(int nAddr, int nCount)
{
    if (!isClientConnected() || !m_bClientMasterPoll || m_bUpdatingFromRemote || nCount <= 0) {
        return;
    }
    // 20261026 ZJH 本机寄存器修改时(如脚本写入 D100~D114 或手动改表), 客户端自动将变更推送写入远端服务端
    clientSendWrite(nAddr, std::min(nCount, 64));
}

bool PlcServer::clientSendWrite(int nStartAddr, int nWordCount)
{
    if (!isClientConnected() || nStartAddr < 0 || nWordCount <= 0) {
        return false;
    }
    const QByteArray baFrame = buildClientWriteFrame(nStartAddr, nWordCount);
    if (baFrame.isEmpty()) {
        return false;
    }
    m_bLastReqWasWrite = true;
    m_pClientSocket->write(baFrame);
    if (m_bFrameLog) {
        emit logMessage(QStringLiteral("[客户端主站发写] D%1×%2  %3").arg(nStartAddr).arg(nWordCount).arg(frameToText(baFrame)), false);
    }
    return true;
}

bool PlcServer::clientSendRead(int nStartAddr, int nWordCount)
{
    if (!isClientConnected() || nStartAddr < 0 || nWordCount <= 0) {
        return false;
    }
    const QByteArray baFrame = buildClientReadFrame(nStartAddr, nWordCount);
    if (baFrame.isEmpty()) {
        return false;
    }
    m_nLastReadAddr = nStartAddr;
    m_nLastReadCount = nWordCount;
    m_bLastReqWasWrite = false;
    m_pClientSocket->write(baFrame);
    return true;
}

void PlcServer::onReadyRead()
{
    auto* pSocket = qobject_cast<QTcpSocket*>(sender());  // 触发信号的连接
    // 20261024 ZJH 连接已被 stop() 移除或协议未创建时忽略
    if (!pSocket || !m_hashBuffers.contains(pSocket) || !m_pProtocol) {
        return;
    }
    QByteArray& baBuffer = m_hashBuffers[pSocket];  // 该连接的累计缓冲
    baBuffer.append(pSocket->readAll());
    // 20261024 ZJH 缓冲超限: 客户端发了大量无法切帧的数据, 清空自保
    if (baBuffer.size() > s_nMaxBufferSize) {
        emit logMessage(QStringLiteral("接收缓冲超限, 已清空"), true);
        baBuffer.clear();
        return;
    }

    // 20261024 ZJH 循环切帧, 直到数据不足一帧
    while (!baBuffer.isEmpty()) {
        // 20261026 ZJH 若处于客户端模式, 先检查是否为远端服务端对主动读/写请求的【应答帧】
        if (m_bClientMode && pSocket == m_pClientSocket) {
            const int nConsumed = tryConsumeClientResponse(baBuffer);
            if (nConsumed > 0) {
                baBuffer.remove(0, nConsumed);
                continue;
            }
            if (nConsumed < 0) {
                // 应答帧半包, 等待后续字节
                break;
            }
        }

        int nLen = 0;  // 帧长或丢弃长度
        const IPlcProtocol::FrameStatus eStatus = m_pProtocol->extractFrame(baBuffer, nLen);
        // 20261024 ZJH 半包: 等下一次 readyRead
        if (eStatus == IPlcProtocol::FrameStatus::Incomplete) {
            break;
        }
        // 20261024 ZJH 垃圾数据: 丢弃并记日志, 继续尝试同步
        if (eStatus == IPlcProtocol::FrameStatus::Garbage) {
            emit logMessage(QStringLiteral("丢弃无法识别数据: %1").arg(frameToText(baBuffer.left(nLen))), true);
            baBuffer.remove(0, nLen);
            continue;
        }
        const QByteArray baFrame = baBuffer.left(nLen);  // 完整一帧
        baBuffer.remove(0, nLen);
        QString strDesc;  // 协议给出的解析描述
        m_bUpdatingFromRemote = true;
        const QByteArray baAnswer = m_pProtocol->handleFrame(baFrame, *m_pBank, strDesc);
        m_bUpdatingFromRemote = false;
        // 20261026 ZJH 先回复再记日志, 日志格式化不计入视觉看到的响应延迟
        if (!baAnswer.isEmpty()) {
            pSocket->write(baAnswer);
        }
        // 20261026 ZJH 逐帧日志可关闭 (视觉高频轮询时每秒上百帧)
        if (m_bFrameLog) {
            emit logMessage(QStringLiteral("收 %1  | %2").arg(frameToText(baFrame), strDesc), false);
            if (!baAnswer.isEmpty()) {
                emit logMessage(QStringLiteral("发 %1").arg(frameToText(baAnswer)), false);
            }
        }
    }
}

void PlcServer::onDisconnected()
{
    auto* pSocket = qobject_cast<QTcpSocket*>(sender());  // 断开的连接
    if (!pSocket) {
        return;
    }
    m_hashBuffers.remove(pSocket);  // 释放缓冲
    if (pSocket == m_pClientSocket) {
        m_pPollTimer->stop();
        emit logMessage(QStringLiteral("[客户端模式] 与远端服务端断开连接 (正在等待自动重连...)"), true);
    } else {
        pSocket->deleteLater();     // 延迟删除 (仍在其信号处理中)
        emit logMessage(QStringLiteral("客户端断开"), false);
    }
    emit clientCountChanged(clientCount());
}

int PlcServer::tryConsumeClientResponse(const QByteArray& baBuffer)
{
    const auto u8 = [&](int i) { return static_cast<uint8_t>(baBuffer[i]); };
    // 1. MC 3E 二进制应答帧: 头为 D0 00 00 FF FF 03 00
    if ((m_eType == ProtocolType::McBinary || m_eType == ProtocolType::KeyenceMcBinary)
        && baBuffer.size() >= 2 && u8(0) == 0xD0 && u8(1) == 0x00) {
        if (baBuffer.size() < 11) {
            return -1;  // 半包
        }
        const int nDataLen = u8(7) | (u8(8) << 8);
        const int nTotal = 9 + nDataLen;
        if (baBuffer.size() < nTotal) {
            return -1;
        }
        const int nEndCode = u8(9) | (u8(10) << 8);
        if (nEndCode == 0 && nDataLen > 2 && m_pBank) {
            const int nWords = (nDataLen - 2) / 2;
            std::vector<uint16_t> vecWords(nWords);
            for (int i = 0; i < nWords; ++i) {
                vecWords[i] = static_cast<uint16_t>(u8(11 + i * 2) | (u8(12 + i * 2) << 8));
            }
            m_bUpdatingFromRemote = true;
            m_pBank->writeWords(m_nLastReadAddr, vecWords);
            m_bUpdatingFromRemote = false;
        }
        return nTotal;
    }
    // 2. MC 3E ASCII 应答帧: 头为 "D000"
    if ((m_eType == ProtocolType::McAscii || m_eType == ProtocolType::KeyenceMcAscii)
        && baBuffer.startsWith("D000")) {
        if (baBuffer.size() < 22) {
            return -1;
        }
        bool bOk = false;
        const int nDataChars = baBuffer.mid(14, 4).toInt(&bOk, 16);
        if (!bOk) {
            return 4;
        }
        const int nTotal = 18 + nDataChars;
        if (baBuffer.size() < nTotal) {
            return -1;
        }
        const int nEndCode = baBuffer.mid(18, 4).toInt(&bOk, 16);
        if (bOk && nEndCode == 0 && nDataChars > 4 && m_pBank) {
            const int nWords = (nDataChars - 4) / 4;
            std::vector<uint16_t> vecWords(nWords);
            for (int i = 0; i < nWords; ++i) {
                vecWords[i] = static_cast<uint16_t>(baBuffer.mid(22 + i * 4, 4).toUInt(&bOk, 16));
            }
            m_bUpdatingFromRemote = true;
            m_pBank->writeWords(m_nLastReadAddr, vecWords);
            m_bUpdatingFromRemote = false;
        }
        return nTotal;
    }
    // 3. Keyence 上位链路应答帧: 以 "OK\r\n"、"E0\r\n" 或 数字空格序列+"\r\n" 结尾 (且不以 RD/WR 命令开头)
    if (m_eType == ProtocolType::KeyenceLink
        && !baBuffer.startsWith("RD") && !baBuffer.startsWith("WR")) {
        const int nLf = baBuffer.indexOf('\n');
        if (nLf < 0) {
            return -1;
        }
        const QString strLine = QString::fromLatin1(baBuffer.left(nLf)).trimmed();
        if (!m_bLastReqWasWrite && strLine != QStringLiteral("OK") && !strLine.startsWith(QLatin1Char('E')) && m_pBank) {
            const QStringList lstTokens = strLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            std::vector<uint16_t> vecWords;
            for (const QString& strTok : lstTokens) {
                bool bOk = false;
                const int nVal = strTok.toInt(&bOk);
                if (bOk) {
                    vecWords.push_back(static_cast<uint16_t>(static_cast<int16_t>(nVal)));
                }
            }
            if (!vecWords.empty()) {
                m_bUpdatingFromRemote = true;
                m_pBank->writeWords(m_nLastReadAddr, vecWords);
                m_bUpdatingFromRemote = false;
            }
        }
        return nLf + 1;
    }
    // 4. Modbus TCP 主站应答帧: Transaction ID 为 0xA55A (客户端主站主动发出的标志)
    if (m_eType == ProtocolType::ModbusTcp && baBuffer.size() >= 2 && u8(0) == 0xA5 && u8(1) == 0x5A) {
        if (baBuffer.size() < 8) {
            return -1;
        }
        const int nMbapLen = (u8(4) << 8) | u8(5);
        const int nTotal = 6 + nMbapLen;
        if (baBuffer.size() < nTotal) {
            return -1;
        }
        const uint8_t uFc = u8(7);
        if (uFc == 0x03 && nTotal >= 9 && m_pBank) {
            const int nBytes = u8(8);
            const int nWords = nBytes / 2;
            if (9 + nBytes <= nTotal) {
                std::vector<uint16_t> vecWords(nWords);
                for (int i = 0; i < nWords; ++i) {
                    vecWords[i] = static_cast<uint16_t>((u8(9 + i * 2) << 8) | u8(10 + i * 2));
                }
                m_bUpdatingFromRemote = true;
                m_pBank->writeWords(m_nLastReadAddr, vecWords);
                m_bUpdatingFromRemote = false;
            }
        }
        return nTotal;
    }
    return 0;
}

QByteArray PlcServer::buildClientReadFrame(int nAddr, int nCount) const
{
    if (m_eType == ProtocolType::McBinary || m_eType == ProtocolType::KeyenceMcBinary) {
        QByteArray ba(21, '\0');
        const uint8_t hdr[15] = { 0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x0C, 0x00, 0x10, 0x00, 0x01, 0x04, 0x00, 0x00 };
        std::memcpy(ba.data(), hdr, 15);
        ba[15] = static_cast<char>(nAddr & 0xFF);
        ba[16] = static_cast<char>((nAddr >> 8) & 0xFF);
        ba[17] = static_cast<char>((nAddr >> 16) & 0xFF);
        ba[18] = static_cast<char>(0xA8);
        ba[19] = static_cast<char>(nCount & 0xFF);
        ba[20] = static_cast<char>((nCount >> 8) & 0xFF);
        return ba;
    }
    if (m_eType == ProtocolType::McAscii || m_eType == ProtocolType::KeyenceMcAscii) {
        return QStringLiteral("500000FF03FF000018001004010000D*%1%2")
            .arg(nAddr, 6, 10, QLatin1Char('0'))
            .arg(nCount, 4, 16, QLatin1Char('0')).toUpper().toLatin1();
    }
    if (m_eType == ProtocolType::KeyenceLink) {
        return QStringLiteral("RDS DM%1.S %2\r").arg(nAddr).arg(nCount).toLatin1();
    }
    if (m_eType == ProtocolType::ModbusTcp) {
        QByteArray ba(12, '\0');
        ba[0] = static_cast<char>(0xA5);
        ba[1] = static_cast<char>(0x5A);
        ba[5] = 0x06;
        ba[6] = 0x01;
        ba[7] = 0x03;
        ba[8] = static_cast<char>((nAddr >> 8) & 0xFF);
        ba[9] = static_cast<char>(nAddr & 0xFF);
        ba[10] = static_cast<char>((nCount >> 8) & 0xFF);
        ba[11] = static_cast<char>(nCount & 0xFF);
        return ba;
    }
    return {};
}

QByteArray PlcServer::buildClientWriteFrame(int nAddr, int nCount) const
{
    std::vector<uint16_t> vecWords;
    if (!m_pBank || nCount <= 0 || !m_pBank->readWords(nAddr, nCount, vecWords)) {
        return {};
    }
    if (m_eType == ProtocolType::McBinary || m_eType == ProtocolType::KeyenceMcBinary) {
        const int nDataLen = 12 + nCount * 2;
        QByteArray ba(9 + nDataLen, '\0');
        const uint8_t hdr[7] = { 0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00 };
        std::memcpy(ba.data(), hdr, 7);
        ba[7] = static_cast<char>(nDataLen & 0xFF);
        ba[8] = static_cast<char>((nDataLen >> 8) & 0xFF);
        ba[9] = 0x10;
        ba[10] = 0x00;
        ba[11] = 0x01;
        ba[12] = 0x14;
        ba[13] = 0x00;
        ba[14] = 0x00;
        ba[15] = static_cast<char>(nAddr & 0xFF);
        ba[16] = static_cast<char>((nAddr >> 8) & 0xFF);
        ba[17] = static_cast<char>((nAddr >> 16) & 0xFF);
        ba[18] = static_cast<char>(0xA8);
        ba[19] = static_cast<char>(nCount & 0xFF);
        ba[20] = static_cast<char>((nCount >> 8) & 0xFF);
        for (int i = 0; i < nCount; ++i) {
            ba[21 + i * 2] = static_cast<char>(vecWords[i] & 0xFF);
            ba[22 + i * 2] = static_cast<char>((vecWords[i] >> 8) & 0xFF);
        }
        return ba;
    }
    if (m_eType == ProtocolType::McAscii || m_eType == ProtocolType::KeyenceMcAscii) {
        const int nChars = 24 + nCount * 4;
        QString str = QStringLiteral("500000FF03FF00%1001014010000D*%2%3")
                          .arg(nChars, 4, 16, QLatin1Char('0'))
                          .arg(nAddr, 6, 10, QLatin1Char('0'))
                          .arg(nCount, 4, 16, QLatin1Char('0')).toUpper();
        for (int i = 0; i < nCount; ++i) {
            str += QStringLiteral("%1").arg(vecWords[i], 4, 16, QLatin1Char('0')).toUpper();
        }
        return str.toLatin1();
    }
    if (m_eType == ProtocolType::KeyenceLink) {
        QString str = QStringLiteral("WRS DM%1.S %2").arg(nAddr).arg(nCount);
        for (int i = 0; i < nCount; ++i) {
            str += QStringLiteral(" %1").arg(static_cast<int16_t>(vecWords[i]));
        }
        str += QLatin1Char('\r');
        return str.toLatin1();
    }
    if (m_eType == ProtocolType::ModbusTcp) {
        const int nMbapLen = 7 + nCount * 2;
        QByteArray ba(6 + nMbapLen, '\0');
        ba[0] = static_cast<char>(0xA5);
        ba[1] = static_cast<char>(0x5A);
        ba[4] = static_cast<char>((nMbapLen >> 8) & 0xFF);
        ba[5] = static_cast<char>(nMbapLen & 0xFF);
        ba[6] = 0x01;
        ba[7] = 0x10;
        ba[8] = static_cast<char>((nAddr >> 8) & 0xFF);
        ba[9] = static_cast<char>(nAddr & 0xFF);
        ba[10] = static_cast<char>((nCount >> 8) & 0xFF);
        ba[11] = static_cast<char>(nCount & 0xFF);
        ba[12] = static_cast<char>(nCount * 2);
        for (int i = 0; i < nCount; ++i) {
            ba[13 + i * 2] = static_cast<char>((vecWords[i] >> 8) & 0xFF);
            ba[14 + i * 2] = static_cast<char>(vecWords[i] & 0xFF);
        }
        return ba;
    }
    return {};
}

QString PlcServer::frameToText(const QByteArray& baFrame) const
{
    // 20261024 ZJH 二进制协议显示十六进制, 文本协议显示可读文本 (CR/LF 转义)
    if (m_eType == ProtocolType::McBinary || m_eType == ProtocolType::KeyenceMcBinary || m_eType == ProtocolType::ModbusTcp) {
        return QString::fromLatin1(baFrame.toHex(' ').toUpper());
    }
    QString strText = QString::fromLatin1(baFrame);
    strText.replace('\r', QStringLiteral("\\r")).replace('\n', QStringLiteral("\\n"));
    return strText;  // 返回可读文本
}
