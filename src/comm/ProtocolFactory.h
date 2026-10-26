// 20261024 ZJH 协议工厂: 协议类型枚举 ↔ 协议实例 / 显示名
#pragma once

#include "comm/IPlcProtocol.h"

#include <QStringList>
#include <memory>

// 20261024 ZJH 协议类型 (数值写入配置文件, 新增类型只能追加, 不能改已有数值)
enum class ProtocolType {
    McBinary = 0,         // 三菱 MC 3E 二进制
    McAscii = 1,          // 三菱 MC 3E ASCII
    KeyenceLink = 2,      // 基恩士上位链路
    KeyenceMcBinary = 3,  // 基恩士 KV 走 MC 兼容 (二进制), 报文与 McBinary 相同
    KeyenceMcAscii = 4,   // 基恩士 KV 走 MC 兼容 (ASCII), 报文与 McAscii 相同
    ModbusTcp = 5,        // 20261026 ZJH 标准 Modbus TCP (FC 03/04/06/10)
};

namespace ProtocolFactory {
// 20261024 ZJH 创建协议实例; 未知类型返回 MC 3E 二进制
std::unique_ptr<IPlcProtocol> create(ProtocolType eType);
// 20261024 ZJH 按枚举数值顺序返回所有协议显示名 (界面下拉框用)
QStringList displayNames();
// 20261024 ZJH 协议的推荐默认端口 (MC 常用 5000/7110, 上位链路 8501)
int defaultPort(ProtocolType eType);
}
