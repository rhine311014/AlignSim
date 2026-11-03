// 20261103 ZJH DataMatrix —— Data Matrix ECC200 编码器 (ISO/IEC 16022), 仅依赖标准库
//
// 用途: 生成 DM 码棋盘格标定板中每个白格的二维码 (内容为格子行列号, 如 "R03C05")
// 范围: ASCII 编码模式 (含两位数字压缩), 正方形单数据区符号 10×10 ~ 26×26 (单个 RS 块, 无交织)
//       最多 44 个数据码字, 对标定板的短坐标文本足够
//
// 符号结构 (以 12×12 为例):
//   左列与底行   : 实线 "L" 形寻像图形 (全黑)
//   顶行与右列   : 虚线时钟图形 (黑白交替, 左上角与右下角为黑)
//   内部 10×10   : 数据区, 码字按 ISO 16022 附录 F 的 "Utah" 斜向排布
// 纠错: GF(256) 本原多项式 x^8+x^5+x^3+x^2+1 (0x12D), 生成多项式根 α^1..α^n
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 20261103 ZJH DM 符号: 模块矩阵 (行优先, true = 黑), 不含静区
struct DmSymbol {
    int nSize = 0;                  // 符号边长 (模块数), 10/12/14/.../26
    std::vector<uint8_t> vecDark;   // nSize × nSize, 1 = 黑模块
    bool dark(int nRow, int nCol) const { return vecDark[static_cast<size_t>(nRow) * nSize + nCol] != 0; }
};

namespace DataMatrix {

// 20261103 ZJH ASCII 模式编码数据码字 (不含填充): 连续两位数字压缩为 1 个码字, 其余字符 = ASCII + 1
// 返回 false: 含非 ASCII (>127) 字符 (本编码器不支持扩展 ASCII)
bool encodeAscii(const std::string& strText, std::vector<int>& vecCodewords);

// 20261103 ZJH 计算纠错码字 (vecData 为补齐后的数据码字, nEcc 为纠错码字数)
std::vector<int> reedSolomon(const std::vector<int>& vecData, int nEcc);

// 20261103 ZJH 选能容纳 nDataCodewords 的最小正方形符号; 超出 26×26 容量返回 0
int symbolSizeFor(int nDataCodewords);

// 20261103 ZJH 完整编码: 文本 → 符号; nMinSize > 0 时符号边长至少为 nMinSize (用于一块板上所有码等大)
// 失败 (字符不支持 / 超容量) 返回 false 并写 strErr
bool encode(const std::string& strText, DmSymbol& symbol, std::string& strErr, int nMinSize = 0);

}  // namespace DataMatrix
