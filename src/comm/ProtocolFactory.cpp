// 20261024 ZJH 协议工厂实现
#include "comm/ProtocolFactory.h"

#include "comm/KeyenceLinkProtocol.h"
#include "comm/McAsciiProtocol.h"
#include "comm/McBinaryProtocol.h"
#include "comm/ModbusTcpProtocol.h"

namespace ProtocolFactory {

std::unique_ptr<IPlcProtocol> create(ProtocolType eType)
{
    // 20261024 ZJH 按类型分发; KV 的 MC 兼容模式复用三菱解析器, 只换显示名
    switch (eType) {
    case ProtocolType::McAscii:
        return std::make_unique<McAsciiProtocol>();
    case ProtocolType::KeyenceLink:
        return std::make_unique<KeyenceLinkProtocol>();
    case ProtocolType::KeyenceMcBinary:
        return std::make_unique<McBinaryProtocol>(QStringLiteral("基恩士 KV-MC 二进制"));
    case ProtocolType::KeyenceMcAscii:
        return std::make_unique<McAsciiProtocol>(QStringLiteral("基恩士 KV-MC ASCII"));
    case ProtocolType::ModbusTcp:
        return std::make_unique<ModbusTcpProtocol>();
    case ProtocolType::McBinary:
    default:
        return std::make_unique<McBinaryProtocol>();  // 默认与未知类型都用 MC 二进制
    }
}

QStringList displayNames()
{
    // 20261024 ZJH 顺序必须与 ProtocolType 数值一致 (下拉框索引 = 枚举值)
    return { QStringLiteral("三菱 MC 3E 二进制"), QStringLiteral("三菱 MC 3E ASCII"),
             QStringLiteral("基恩士上位链路"), QStringLiteral("基恩士 KV-MC 二进制"),
             QStringLiteral("基恩士 KV-MC ASCII"), QStringLiteral("Modbus TCP") };
}

int defaultPort(ProtocolType eType)
{
    // 20261024 ZJH 上位链路固定 8501, Modbus TCP 常用 502, 其余沿用参考工程的 7110
    if (eType == ProtocolType::KeyenceLink) {
        return 8501;
    }
    if (eType == ProtocolType::ModbusTcp) {
        return 502;
    }
    return 7110;
}

}  // namespace ProtocolFactory
