// 20261024 ZJH 协议层单元测试: MC 3E 二进制 / MC 3E ASCII / 基恩士上位链路
// 覆盖: 正常读写、半包、粘包、垃圾数据、非法指令、地址越界
#include <QtTest>

#include "comm/KeyenceLinkProtocol.h"
#include "comm/McAsciiProtocol.h"
#include "comm/McBinaryProtocol.h"
#include "comm/ModbusTcpProtocol.h"
#include "core/RegisterBank.h"

// 20261024 ZJH 十六进制字符串 → 字节 (测试里写报文更直观)
static QByteArray hex(const char* pszHex)
{
    return QByteArray::fromHex(QByteArray(pszHex).replace(' ', ""));
}

class TestProtocols : public QObject
{
    Q_OBJECT
private slots:
    // ---------------- MC 3E 二进制 ----------------
    void mcBinary_readWords()
    {
        RegisterBank bank;  // 测试用寄存器区
        bank.writeWords(100, { 0x0001, 0x1234, 0xFFFF });
        McBinaryProtocol pro;
        // 20261024 ZJH 读 D100 起 3 点
        const QByteArray baReq = hex("5000 00 FF FF03 00 0C00 1000 0104 0000 640000 A8 0300");
        int nLen = 0;
        QCOMPARE(pro.extractFrame(baReq, nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, baReq.size());
        QString strLog;
        const QByteArray baAns = pro.handleFrame(baReq, bank, strLog);
        QCOMPARE(baAns, hex("D000 00 FF FF03 00 0800 0000 0100 3412 FFFF"));
    }

    void mcBinary_writeWords()
    {
        RegisterBank bank;
        McBinaryProtocol pro;
        // 20261024 ZJH 写 D200 起 2 点: 0x1234, 0xABCD
        const QByteArray baReq = hex("5000 00 FF FF03 00 1000 1000 0114 0000 C80000 A8 0200 3412 CDAB");
        QString strLog;
        QCOMPARE(pro.handleFrame(baReq, bank, strLog), hex("D000 00 FF FF03 00 0200 0000"));
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(200)), uint16_t(0x1234));
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(201)), uint16_t(0xABCD));
    }

    void mcBinary_halfAndStickyPackets()
    {
        McBinaryProtocol pro;
        const QByteArray baReq = hex("5000 00 FF FF03 00 0C00 1000 0104 0000 640000 A8 0300");
        int nLen = 0;
        // 20261024 ZJH 半包: 只收到前 10 字节
        QCOMPARE(pro.extractFrame(baReq.left(10), nLen), IPlcProtocol::FrameStatus::Incomplete);
        // 20261024 ZJH 半包: 头完整但数据区不完整
        QCOMPARE(pro.extractFrame(baReq.left(15), nLen), IPlcProtocol::FrameStatus::Incomplete);
        // 20261024 ZJH 粘包: 两帧连在一起, 只切出第一帧
        QCOMPARE(pro.extractFrame(baReq + baReq, nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, baReq.size());
        // 20261024 ZJH 垃圾数据: 开头不是 50 00
        QCOMPARE(pro.extractFrame(hex("FFEE") + baReq, nLen), IPlcProtocol::FrameStatus::Garbage);
        QVERIFY(nLen >= 1);
    }

    void mcBinary_errors()
    {
        RegisterBank bank;
        McBinaryProtocol pro;
        QString strLog;
        // 20261024 ZJH 不支持的软元件 (M 继电器 0x90) → C056 之外也可接受, 但必须是非 0 结束码
        QByteArray baAns = pro.handleFrame(hex("5000 00 FF FF03 00 0C00 1000 0104 0000 640000 90 0300"), bank, strLog);
        QCOMPARE(baAns.left(2), hex("D000"));
        QVERIFY(static_cast<uint8_t>(baAns[9]) | static_cast<uint8_t>(baAns[10]));
        // 20261024 ZJH 不支持的指令 0619 → C059
        baAns = pro.handleFrame(hex("5000 00 FF FF03 00 0600 1000 1906 0000"), bank, strLog);
        QCOMPARE(baAns.mid(9, 2), hex("59C0"));
        // 20261024 ZJH 地址越界: D65535 起读 2 点 → C056
        baAns = pro.handleFrame(hex("5000 00 FF FF03 00 0C00 1000 0104 0000 FFFF00 A8 0200"), bank, strLog);
        QCOMPARE(baAns.mid(9, 2), hex("56C0"));
    }

    // ---------------- MC 3E ASCII ----------------
    void mcAscii_readWrite()
    {
        RegisterBank bank;
        McAsciiProtocol pro;
        QString strLog;
        // 20261024 ZJH 写 D100 起 2 点: 0x0102, 0xA0B0
        const QByteArray baWrite("500000FF03FF00" "0020" "0010" "1401" "0000" "D*" "000100" "0002" "0102" "A0B0");
        int nLen = 0;
        QCOMPARE(pro.extractFrame(baWrite, nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, baWrite.size());
        QCOMPARE(pro.handleFrame(baWrite, bank, strLog), QByteArray("D00000FF03FF00" "0004" "0000"));
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(101)), uint16_t(0xA0B0));
        // 20261024 ZJH 读回 D100 起 2 点
        const QByteArray baRead("500000FF03FF00" "0018" "0010" "0401" "0000" "D*" "000100" "0002");
        QCOMPARE(pro.handleFrame(baRead, bank, strLog), QByteArray("D00000FF03FF00" "000C" "0000" "0102" "A0B0"));
    }

    void mcAscii_halfStickyGarbage()
    {
        McAsciiProtocol pro;
        const QByteArray baRead("500000FF03FF00" "0018" "0010" "0401" "0000" "D*" "000100" "0002");
        int nLen = 0;
        QCOMPARE(pro.extractFrame(baRead.left(16), nLen), IPlcProtocol::FrameStatus::Incomplete);
        QCOMPARE(pro.extractFrame(baRead.left(30), nLen), IPlcProtocol::FrameStatus::Incomplete);
        QCOMPARE(pro.extractFrame(baRead + baRead, nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, baRead.size());
        QCOMPARE(pro.extractFrame(QByteArray("xyz") + baRead, nLen), IPlcProtocol::FrameStatus::Garbage);
        // 20261024 ZJH 长度字段不是十六进制 → 垃圾
        QCOMPARE(pro.extractFrame(QByteArray("500000FF03FF00" "00G8" "0010"), nLen), IPlcProtocol::FrameStatus::Garbage);
    }

    void mcAscii_badDevice()
    {
        RegisterBank bank;
        McAsciiProtocol pro;
        QString strLog;
        // 20261024 ZJH M 软元件不支持 → 结束码非 0000
        const QByteArray baAns = pro.handleFrame(
            QByteArray("500000FF03FF00" "0018" "0010" "0401" "0000" "M*" "000100" "0002"), bank, strLog);
        QCOMPARE(baAns.left(4), QByteArray("D000"));
        QVERIFY(baAns.mid(18, 4) != QByteArray("0000"));
    }

    // ---------------- 基恩士上位链路 ----------------
    void keyence_readFormats()
    {
        RegisterBank bank;
        KeyenceLinkProtocol pro;
        QString strLog;
        bank.setInt16(100, 123);
        bank.setInt16(101, -5);
        bank.setInt32(200, -100000);
        QCOMPARE(pro.handleFrame("RD DM100\r", bank, strLog), QByteArray("00123\r\n"));
        QCOMPARE(pro.handleFrame("RD DM101.S\r", bank, strLog), QByteArray("-00005\r\n"));
        QCOMPARE(pro.handleFrame("RD DM101.U\r", bank, strLog), QByteArray("65531\r\n"));
        QCOMPARE(pro.handleFrame("RDS DM100.H 2\r", bank, strLog), QByteArray("007B FFFB\r\n"));
        QCOMPARE(pro.handleFrame("RD DM200.L\r", bank, strLog), QByteArray("-0000100000\r\n"));
        QCOMPARE(pro.handleFrame("RD D200.D\r", bank, strLog), QByteArray("4294867296\r\n"));
        // 20261024 ZJH 连续读 .L: 每个元素占 2 个字
        bank.setInt32(202, 7);
        QCOMPARE(pro.handleFrame("RDS DM200.L 2\r", bank, strLog), QByteArray("-0000100000 +0000000007\r\n"));
    }

    void keyence_write()
    {
        RegisterBank bank;
        KeyenceLinkProtocol pro;
        QString strLog;
        QCOMPARE(pro.handleFrame("WRS DM300 3 1 2 3\r", bank, strLog), QByteArray("OK\r\n"));
        QCOMPARE(bank.getInt16(302), int16_t(3));
        QCOMPARE(pro.handleFrame("WR DM400.L -70000\r", bank, strLog), QByteArray("OK\r\n"));
        QCOMPARE(bank.getInt32(400), int32_t(-70000));
        QCOMPARE(pro.handleFrame("WR DM500.H 1A2B\r", bank, strLog), QByteArray("OK\r\n"));
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(500)), uint16_t(0x1A2B));
        QCOMPARE(pro.handleFrame("WR DM501.S -2\r", bank, strLog), QByteArray("OK\r\n"));
        QCOMPARE(bank.getInt16(501), int16_t(-2));
    }

    void keyence_errorsAndHandshake()
    {
        RegisterBank bank;
        KeyenceLinkProtocol pro;
        QString strLog;
        QCOMPARE(pro.handleFrame("XX\r", bank, strLog), QByteArray("E1\r\n"));
        QCOMPARE(pro.handleFrame("RD DM70000\r", bank, strLog), QByteArray("E0\r\n"));
        QCOMPARE(pro.handleFrame("RD MR100\r", bank, strLog), QByteArray("E0\r\n"));
        QCOMPARE(pro.handleFrame("WRS DM1 3 1 2\r", bank, strLog), QByteArray("E1\r\n"));  // 数据个数不足
        QCOMPARE(pro.handleFrame("CR\r", bank, strLog), QByteArray("CC\r\n"));
        QVERIFY(pro.handleFrame("?K\r", bank, strLog).endsWith("\r\n"));
    }

    void keyence_extract()
    {
        KeyenceLinkProtocol pro;
        int nLen = 0;
        QCOMPARE(pro.extractFrame("RD DM1", nLen), IPlcProtocol::FrameStatus::Incomplete);
        QCOMPARE(pro.extractFrame("RD DM1\rRD DM2\r", nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, 7);
        // 20261024 ZJH 客户端发 CRLF 时, 下一帧开头残留的 LF 当垃圾丢弃
        QCOMPARE(pro.extractFrame("\nRD DM2\r", nLen), IPlcProtocol::FrameStatus::Garbage);
        QCOMPARE(nLen, 1);
    }

    // ---------------- Modbus TCP ----------------
    void modbusTcp_readWriteAndErrors()
    {
        RegisterBank bank;
        ModbusTcpProtocol pro;
        QString strLog;

        // 1. FC 0x10 写多个保持寄存器: D100 起写 2 个字 (0x1234, 0xABCD)
        const QByteArray baWrite10 = hex("0001 0000 000B 01 10 0064 0002 04 1234 ABCD");
        int nLen = 0;
        QCOMPARE(pro.extractFrame(baWrite10, nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, baWrite10.size());
        QCOMPARE(pro.handleFrame(baWrite10, bank, strLog), hex("0001 0000 0006 01 10 0064 0002"));
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(100)), uint16_t(0x1234));
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(101)), uint16_t(0xABCD));

        // 2. FC 0x06 写单个保持寄存器: D102 = 0x5566 (回显原请求)
        const QByteArray baWrite06 = hex("0002 0000 0006 01 06 0066 5566");
        QCOMPARE(pro.handleFrame(baWrite06, bank, strLog), baWrite06);
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(102)), uint16_t(0x5566));

        // 3. FC 0x03 读保持寄存器: D100 起读 3 个字
        const QByteArray baRead03 = hex("0003 0000 0006 01 03 0064 0003");
        QCOMPARE(pro.handleFrame(baRead03, bank, strLog), hex("0003 0000 0009 01 03 06 1234 ABCD 5566"));

        // 4. 半包、粘包与非法 ProtocolId 垃圾丢弃
        QCOMPARE(pro.extractFrame(baRead03.left(4), nLen), IPlcProtocol::FrameStatus::Incomplete);
        QCOMPARE(pro.extractFrame(baRead03.left(10), nLen), IPlcProtocol::FrameStatus::Incomplete);
        QCOMPARE(pro.extractFrame(baRead03 + baWrite06, nLen), IPlcProtocol::FrameStatus::Complete);
        QCOMPARE(nLen, baRead03.size());
        QCOMPARE(pro.extractFrame(hex("0001 0001 0006 01 03 0000 0001"), nLen), IPlcProtocol::FrameStatus::Garbage);

        // 5. 异常码: 非法功能码 0x05 → 0x85 0x01; 读地址越界 → 0x83 0x02
        QCOMPARE(pro.handleFrame(hex("0004 0000 0006 01 05 0000 FF00"), bank, strLog), hex("0004 0000 0003 01 85 01"));
        QCOMPARE(pro.handleFrame(hex("0005 0000 0006 01 03 FFFF 0002"), bank, strLog), hex("0005 0000 0003 01 83 02"));
    }

    // ---------------- RegisterBank ----------------
    void registerBank_types()
    {
        RegisterBank bank;
        bank.setFloat(10, 3.5f);
        QCOMPARE(bank.getFloat(10), 3.5f);
        bank.setInt32(20, -2);
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(20)), uint16_t(0xFFFE));  // 低字在前
        QCOMPARE(static_cast<uint16_t>(bank.getInt16(21)), uint16_t(0xFFFF));
        std::vector<uint16_t> vecOut;
        QVERIFY(!bank.readWords(65535, 2, vecOut));  // 越界
        QVERIFY(!bank.writeWords(-1, { 1 }));
    }
};

QTEST_APPLESS_MAIN(TestProtocols)
#include "test_protocols.moc"
