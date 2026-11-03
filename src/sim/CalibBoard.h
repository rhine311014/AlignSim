// 20261103 ZJH CalibBoard —— 相机内参标定板生成 (屏幕 1:1 显示 / PNG / PDF 共用同一绘制函数)
//
// 坐标约定: 标定板局部坐标以"格距 (pitch)"为单位绘制, 原点为图案区左上角 (不含边距/边框)
//   棋盘格         : nCols × nRows 个方格, 内角点 (nCols−1) × (nRows−1); 左上角方格为黑
//   对称圆点       : nCols × nRows 个圆, 圆心 ((j+0.5)·p, (i+0.5)·p)
//   非对称圆点     : OpenCV ASYMMETRIC_GRID 约定, 第 i 行第 j 个圆心 x = (2j + i%2 + 0.5)·p, y = (i + 0.5)·p
//                    (OpenCV patternSize = (nCols, nRows), 其 squareSize 参数 = p)
//   Halcon 风格圆点: nCols × nRows 圆点阵列 + 外围黑色方框 + 左上角定向三角 (近似 caltab, 配套 gen_caltab 参数)
//
// 精度要点: 屏幕/PNG 输出时格距为整数像素; 棋盘格关闭抗锯齿, 边缘正好落在像素边界;
//           圆点开启抗锯齿 (对称灰度过渡不改变质心, 反而提高亚像素精度)
#pragma once

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>
#include <vector>

class QPainter;

// 20261103 ZJH 标定板图案 (数值写入配置)
enum class BoardType {
    Chessboard = 0,        // 棋盘格
    CircleSymmetric = 1,   // 对称圆点阵列
    CircleAsymmetric = 2,  // 非对称圆点阵列 (OpenCV)
    HalconCircle = 3,      // Halcon 风格圆点板
};

// 20261103 ZJH 标定板参数
struct CalibBoardSpec {
    BoardType eType = BoardType::Chessboard;
    int nCols = 10;               // 列数 (棋盘格为方格数; 圆点为每行圆数)
    int nRows = 7;                // 行数
    int nPitchPx = 40;            // 格距 / 圆心距 (像素, 屏幕与 PNG 输出用)
    double dCircleRatio = 0.5;    // 圆直径 / 圆心距 (圆点类)
    int nMarginPitch = 1;         // 四周白色留白 (以格距为单位; 识别算法需要静区)
    bool bInvert = false;         // 黑白反色
};

namespace CalibBoard {

// 20261103 ZJH 参数合法化: 行列 ≥ 2 (非对称圆点行 ≥ 3)、格距 ≥ 4px、直径比 0.1~0.9、边距 0~10
CalibBoardSpec normalized(const CalibBoardSpec& spec);

// 20261103 ZJH 图案区尺寸 (以格距为单位, 不含边距; Halcon 含边框)
QSizeF patternSizeInPitch(const CalibBoardSpec& spec);
// 20261103 ZJH 整板尺寸 (以格距为单位, 含边距)
QSizeF boardSizeInPitch(const CalibBoardSpec& spec);
// 20261103 ZJH 整板像素尺寸 (格距 = nPitchPx)
QSize boardSizePx(const CalibBoardSpec& spec);

// 20261103 ZJH 特征点理论位置 (以格距为单位, 整板坐标含边距):
//   棋盘格 = 内角点 (行优先); 圆点 = 圆心 (行优先)
std::vector<QPointF> featurePointsInPitch(const CalibBoardSpec& spec);

// 20261103 ZJH 在 painter 上绘制整板, 1 个格距 = dUnitPerPitch 个 painter 单位, 左上角在 painter 原点
// bAntialiasCircles: 圆点是否抗锯齿 (屏幕/PNG 建议 true; PDF 为矢量无所谓)
void paint(QPainter& painter, const CalibBoardSpec& spec, double dUnitPerPitch, bool bAntialiasCircles = true);

// 20261103 ZJH 渲染为精确像素的灰度图 (格距 = nPitchPx); dPixelPitchMm > 0 时写入对应 DPI 元数据
QImage render(const CalibBoardSpec& spec, double dPixelPitchMm = 0.0);

// 20261103 ZJH 导出 PDF (矢量), 1 个格距 = dPitchMm 毫米, 页面大小 = 整板物理尺寸
bool exportPdf(const CalibBoardSpec& spec, double dPitchMm, const QString& strPath, QString& strErr);

// 20261103 ZJH 视觉软件参数说明 (图案数量、间距、OpenCV / Halcon 调用写法); dPitchMm 为实际格距
QString describe(const CalibBoardSpec& spec, double dPitchMm);

// 20261103 ZJH 图案显示名
QString typeName(BoardType eType);

}  // namespace CalibBoard
