// 20261103 ZJH DataMatrix ECC200 编码器实现 (ISO/IEC 16022 附录 E 纠错 / 附录 F 码字排布)
#include "sim/DataMatrix.h"

#include <array>

namespace {

// 20261103 ZJH 正方形单数据区符号参数表: 边长 / 数据码字数 / 纠错码字数 (数据区边长 = 边长 − 2)
struct SymbolInfo {
    int nSize;   // 符号边长 (含寻像与时钟图形)
    int nData;   // 数据码字容量
    int nEcc;    // 纠错码字数
};
constexpr std::array<SymbolInfo, 9> s_arrSymbols = { {
    { 10, 3, 5 }, { 12, 5, 7 }, { 14, 8, 10 }, { 16, 12, 12 }, { 18, 18, 14 },
    { 20, 22, 18 }, { 22, 30, 20 }, { 24, 36, 24 }, { 26, 44, 28 },
} };

// 20261103 ZJH GF(256) 指数/对数表 (本原多项式 0x12D)
struct GaloisField {
    std::array<int, 256> arrExp{};  // α^i
    std::array<int, 256> arrLog{};  // log_α(x)
    GaloisField()
    {
        int x = 1;  // 从 α^0 = 1 开始
        for (int i = 0; i < 255; ++i) {  // 遍历乘法群全部 255 个元素
            arrExp[i] = x;
            arrLog[x] = i;
            x <<= 1;  // 乘 α
            if (x & 0x100) {
                x ^= 0x12D;  // 超出 8 位时模本原多项式
            }
        }
        arrExp[255] = arrExp[0];  // 便于指数取模后直接查表
    }
    int mul(int a, int b) const
    {
        // 20261103 ZJH 任一为 0 时乘积为 0 (0 无对数)
        if (a == 0 || b == 0) {
            return 0;
        }
        return arrExp[(arrLog[a] + arrLog[b]) % 255];  // 返回 a·b
    }
};

const GaloisField& gf()
{
    static const GaloisField s_gf;  // 只构造一次
    return s_gf;
}

// 20261103 ZJH 附录 F 码字排布器: 计算数据区每个模块属于哪个码字的哪一位
class Placer
{
public:
    Placer(int nRow, int nCol) : m_nRow(nRow), m_nCol(nCol), m_vecMap(static_cast<size_t>(nRow) * nCol, 0) {}

    // 20261103 ZJH 返回: 0 = 未分配 (浅色), 1 = 固定黑, ≥10 = 码字号×10 + 位号 (位号 1 = 最高位)
    const std::vector<int>& run()
    {
        int nChr = 1;   // 当前码字序号 (1 起)
        int nR = 4;     // 起点行
        int nC = 0;     // 起点列
        do {
            // 20261103 ZJH 四种角落特殊形状 (按数据区尺寸决定是否出现)
            if (nR == m_nRow && nC == 0) {
                corner1(nChr++);
            }
            if (nR == m_nRow - 2 && nC == 0 && (m_nCol % 4) != 0) {
                corner2(nChr++);
            }
            if (nR == m_nRow - 2 && nC == 0 && (m_nCol % 8) == 4) {
                corner3(nChr++);
            }
            if (nR == m_nRow + 4 && nC == 2 && (m_nCol % 8) == 0) {
                corner4(nChr++);
            }
            // 20261103 ZJH 向右上斜扫
            do {
                if (nR < m_nRow && nC >= 0 && at(nR, nC) == 0) {
                    utah(nR, nC, nChr++);
                }
                nR -= 2;
                nC += 2;
            } while (nR >= 0 && nC < m_nCol);
            nR += 1;
            nC += 3;
            // 20261103 ZJH 向左下斜扫
            do {
                if (nR >= 0 && nC < m_nCol && at(nR, nC) == 0) {
                    utah(nR, nC, nChr++);
                }
                nR += 2;
                nC -= 2;
            } while (nR < m_nRow && nC >= 0);
            nR += 3;
            nC += 1;
        } while (nR < m_nRow || nC < m_nCol);
        // 20261103 ZJH 右下角未被占用时填固定图案 (右下与其左上对角为黑)
        if (at(m_nRow - 1, m_nCol - 1) == 0) {
            at(m_nRow - 1, m_nCol - 1) = 1;
            at(m_nRow - 2, m_nCol - 2) = 1;
        }
        return m_vecMap;  // 返回模块分配表
    }

private:
    int& at(int r, int c) { return m_vecMap[static_cast<size_t>(r) * m_nCol + c]; }

    // 20261103 ZJH 放置单个模块, 越界时按附录 F 规则回绕
    void module(int r, int c, int nChr, int nBit)
    {
        if (r < 0) {
            r += m_nRow;
            c += 4 - ((m_nRow + 4) % 8);
        }
        if (c < 0) {
            c += m_nCol;
            r += 4 - ((m_nCol + 4) % 8);
        }
        at(r, c) = nChr * 10 + nBit;
    }
    // 20261103 ZJH 标准 "Utah" 形状: 8 个模块, (r,c) 为第 8 位 (最低位)
    void utah(int r, int c, int nChr)
    {
        module(r - 2, c - 2, nChr, 1);
        module(r - 2, c - 1, nChr, 2);
        module(r - 1, c - 2, nChr, 3);
        module(r - 1, c - 1, nChr, 4);
        module(r - 1, c, nChr, 5);
        module(r, c - 2, nChr, 6);
        module(r, c - 1, nChr, 7);
        module(r, c, nChr, 8);
    }
    void corner1(int n)
    {
        module(m_nRow - 1, 0, n, 1);
        module(m_nRow - 1, 1, n, 2);
        module(m_nRow - 1, 2, n, 3);
        module(0, m_nCol - 2, n, 4);
        module(0, m_nCol - 1, n, 5);
        module(1, m_nCol - 1, n, 6);
        module(2, m_nCol - 1, n, 7);
        module(3, m_nCol - 1, n, 8);
    }
    void corner2(int n)
    {
        module(m_nRow - 3, 0, n, 1);
        module(m_nRow - 2, 0, n, 2);
        module(m_nRow - 1, 0, n, 3);
        module(0, m_nCol - 4, n, 4);
        module(0, m_nCol - 3, n, 5);
        module(0, m_nCol - 2, n, 6);
        module(0, m_nCol - 1, n, 7);
        module(1, m_nCol - 1, n, 8);
    }
    void corner3(int n)
    {
        module(m_nRow - 3, 0, n, 1);
        module(m_nRow - 2, 0, n, 2);
        module(m_nRow - 1, 0, n, 3);
        module(0, m_nCol - 2, n, 4);
        module(0, m_nCol - 1, n, 5);
        module(1, m_nCol - 1, n, 6);
        module(2, m_nCol - 1, n, 7);
        module(3, m_nCol - 1, n, 8);
    }
    void corner4(int n)
    {
        module(m_nRow - 1, 0, n, 1);
        module(m_nRow - 1, m_nCol - 1, n, 2);
        module(0, m_nCol - 3, n, 3);
        module(0, m_nCol - 2, n, 4);
        module(0, m_nCol - 1, n, 5);
        module(1, m_nCol - 3, n, 6);
        module(1, m_nCol - 2, n, 7);
        module(1, m_nCol - 1, n, 8);
    }

    int m_nRow;                 // 数据区行数
    int m_nCol;                 // 数据区列数
    std::vector<int> m_vecMap;  // 模块分配表
};

}  // namespace

namespace DataMatrix {

bool encodeAscii(const std::string& strText, std::vector<int>& vecCodewords)
{
    vecCodewords.clear();
    const auto isDigit = [](unsigned char ch) { return ch >= '0' && ch <= '9'; };
    for (size_t i = 0; i < strText.size(); ++i) {  // 逐字符编码
        const unsigned char ch = static_cast<unsigned char>(strText[i]);
        // 20261103 ZJH 扩展 ASCII 需 Upper Shift (235), 标定板内容用不到, 直接拒绝
        if (ch > 127) {
            return false;
        }
        // 20261103 ZJH 两位数字 "dd" 压缩为 130 + dd
        if (isDigit(ch) && i + 1 < strText.size() && isDigit(static_cast<unsigned char>(strText[i + 1]))) {
            vecCodewords.push_back(130 + (ch - '0') * 10 + (strText[i + 1] - '0'));
            ++i;  // 跳过已压缩的第二位
        } else {
            vecCodewords.push_back(ch + 1);  // 普通字符 = ASCII + 1
        }
    }
    return true;  // 返回编码成功
}

std::vector<int> reedSolomon(const std::vector<int>& vecData, int nEcc)
{
    const GaloisField& field = gf();
    // 20261103 ZJH 生成多项式 g(x) = Π (x + α^i), i = 1..nEcc; 系数按最高次在前存放, vecGen[0] = 1
    std::vector<int> vecGen{ 1 };
    for (int i = 1; i <= nEcc; ++i) {  // 逐个乘一次因子
        std::vector<int> vecNext(vecGen.size() + 1, 0);
        for (size_t j = 0; j < vecGen.size(); ++j) {
            vecNext[j] ^= vecGen[j];                                  // x · g
            vecNext[j + 1] ^= field.mul(vecGen[j], field.arrExp[i]);  // α^i · g
        }
        vecGen.swap(vecNext);
    }
    // 20261103 ZJH 多项式除法 (LFSR): 余式即纠错码字
    std::vector<int> vecEcc(static_cast<size_t>(nEcc), 0);
    for (int nData : vecData) {  // 逐个数据码字移入
        const int nFeedback = nData ^ vecEcc[0];
        for (int k = 0; k < nEcc - 1; ++k) {
            vecEcc[k] = vecEcc[k + 1] ^ field.mul(vecGen[k + 1], nFeedback);
        }
        vecEcc[nEcc - 1] = field.mul(vecGen[nEcc], nFeedback);
    }
    return vecEcc;  // 返回纠错码字 (高位在前)
}

int symbolSizeFor(int nDataCodewords)
{
    for (const SymbolInfo& info : s_arrSymbols) {  // 从小到大找第一个放得下的
        if (info.nData >= nDataCodewords) {
            return info.nSize;  // 返回符号边长
        }
    }
    return 0;  // 超出本编码器支持的最大容量
}

bool encode(const std::string& strText, DmSymbol& symbol, std::string& strErr, int nMinSize)
{
    std::vector<int> vecCw;
    // 20261103 ZJH 第 1 步: 文本 → 数据码字
    if (!encodeAscii(strText, vecCw)) {
        strErr = "unsupported character (only ASCII 0-127)";
        return false;
    }
    // 20261103 ZJH 第 2 步: 选符号尺寸 (不小于 nMinSize)
    const SymbolInfo* pInfo = nullptr;
    for (const SymbolInfo& info : s_arrSymbols) {
        if (info.nData >= static_cast<int>(vecCw.size()) && info.nSize >= nMinSize) {
            pInfo = &info;
            break;
        }
    }
    if (!pInfo) {
        strErr = "text too long for 26x26 symbol";
        return false;
    }
    // 20261103 ZJH 第 3 步: 填充 —— 首个填充码字 129, 其后按 253 状态伪随机化
    if (static_cast<int>(vecCw.size()) < pInfo->nData) {
        vecCw.push_back(129);
    }
    while (static_cast<int>(vecCw.size()) < pInfo->nData) {
        const int nPos = static_cast<int>(vecCw.size()) + 1;  // 该填充码字的位置 (1 起)
        int nPad = 129 + ((149 * nPos) % 253) + 1;
        if (nPad > 254) {
            nPad -= 254;
        }
        vecCw.push_back(nPad);
    }
    // 20261103 ZJH 第 4 步: 追加纠错码字
    const std::vector<int> vecEcc = reedSolomon(vecCw, pInfo->nEcc);
    vecCw.insert(vecCw.end(), vecEcc.begin(), vecEcc.end());

    // 20261103 ZJH 第 5 步: 码字排布到数据区, 再加寻像/时钟图形
    const int nRegion = pInfo->nSize - 2;  // 数据区边长
    Placer placer(nRegion, nRegion);
    const std::vector<int>& vecMap = placer.run();
    symbol.nSize = pInfo->nSize;
    symbol.vecDark.assign(static_cast<size_t>(symbol.nSize) * symbol.nSize, 0);
    for (int r = 0; r < nRegion; ++r) {
        for (int c = 0; c < nRegion; ++c) {
            const int v = vecMap[static_cast<size_t>(r) * nRegion + c];
            bool bDark = false;
            if (v == 1) {
                bDark = true;  // 固定黑模块
            } else if (v >= 10) {
                const int nChr = v / 10;  // 码字序号 (1 起)
                const int nBit = v % 10;  // 位号 (1 = 最高位)
                bDark = (vecCw[static_cast<size_t>(nChr) - 1] >> (8 - nBit)) & 1;
            }
            symbol.vecDark[static_cast<size_t>(r + 1) * symbol.nSize + (c + 1)] = bDark ? 1 : 0;
        }
    }
    const int n = symbol.nSize;
    for (int i = 0; i < n; ++i) {
        symbol.vecDark[static_cast<size_t>(i) * n + 0] = 1;                            // 左列: 实线
        symbol.vecDark[static_cast<size_t>(n - 1) * n + i] = 1;                        // 底行: 实线
        symbol.vecDark[static_cast<size_t>(0) * n + i] = (i % 2 == 0) ? 1 : 0;         // 顶行: 偶数列黑
        symbol.vecDark[static_cast<size_t>(i) * n + (n - 1)] = (i % 2 == 1) ? 1 : 0;   // 右列: 奇数行黑
    }
    symbol.vecDark[static_cast<size_t>(n - 1) * n + (n - 1)] = 1;  // 右下角属于底行实线
    return true;  // 返回编码成功
}

}  // namespace DataMatrix
