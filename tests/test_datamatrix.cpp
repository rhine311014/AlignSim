// 20261103 ZJH DataMatrix ECC200 编码器测试: ASCII 压缩、ISO 16022 标准例子纠错码字、符号尺寸选择、寻像/时钟图形
#include <QtTest>

#include "sim/DataMatrix.h"

class TestDataMatrix : public QObject
{
    Q_OBJECT
private slots:
    void asciiDigitPairs()
    {
        // 20261103 ZJH "123456" → 三个两位数字码字 130+12 / 130+34 / 130+56
        std::vector<int> vecCw;
        QVERIFY(DataMatrix::encodeAscii("123456", vecCw));
        QCOMPARE(vecCw, (std::vector<int>{ 142, 164, 186 }));
        // 20261103 ZJH 字母 = ASCII + 1, 数字对压缩: "R03C05" → R, 03, C, 05
        QVERIFY(DataMatrix::encodeAscii("R03C05", vecCw));
        QCOMPARE(vecCw, (std::vector<int>{ 'R' + 1, 133, 'C' + 1, 135 }));
        // 20261103 ZJH 奇数个数字: 末位单独编码
        QVERIFY(DataMatrix::encodeAscii("123", vecCw));
        QCOMPARE(vecCw, (std::vector<int>{ 142, '3' + 1 }));
        // 20261103 ZJH 扩展 ASCII 不支持
        QVERIFY(!DataMatrix::encodeAscii("\xC3\xA9", vecCw));
    }

    void reedSolomonIsoExample()
    {
        // 20261103 ZJH ISO/IEC 16022 附录 O 例子: 10×10 符号 "123456" 的 5 个纠错码字
        QCOMPARE(DataMatrix::reedSolomon({ 142, 164, 186 }, 5), (std::vector<int>{ 114, 25, 5, 88, 102 }));
    }

    void symbolSize()
    {
        QCOMPARE(DataMatrix::symbolSizeFor(3), 10);
        QCOMPARE(DataMatrix::symbolSizeFor(4), 12);
        QCOMPARE(DataMatrix::symbolSizeFor(44), 26);
        QCOMPARE(DataMatrix::symbolSizeFor(45), 0);
        DmSymbol sym;
        std::string strErr;
        QVERIFY(DataMatrix::encode("R03C05", sym, strErr));
        QCOMPARE(sym.nSize, 12);
        // 20261103 ZJH 指定最小尺寸 (整板码等大)
        QVERIFY(DataMatrix::encode("1", sym, strErr, 14));
        QCOMPARE(sym.nSize, 14);
        QVERIFY(!DataMatrix::encode(std::string(100, 'A'), sym, strErr));
    }

    void finderAndTiming()
    {
        DmSymbol sym;
        std::string strErr;
        QVERIFY(DataMatrix::encode("123456", sym, strErr));
        QCOMPARE(sym.nSize, 10);
        const int n = sym.nSize;
        for (int i = 0; i < n; ++i) {
            QVERIFY(sym.dark(i, 0));                   // 左列全黑
            QVERIFY(sym.dark(n - 1, i));               // 底行全黑
            QCOMPARE(sym.dark(0, i), i % 2 == 0);      // 顶行交替, 左上黑
            QCOMPARE(sym.dark(i, n - 1), i % 2 == 1);  // 右列交替, 右下黑
        }
    }
};

QTEST_MAIN(TestDataMatrix)
#include "test_datamatrix.moc"
