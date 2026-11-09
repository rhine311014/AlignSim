// 20261103 ZJH CalibBoard 实现
#include "sim/CalibBoard.h"

#include "sim/DataMatrix.h"

#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace {
constexpr double s_dHalconFrame = 0.15;  // Halcon 风格边框厚度 (格距)
constexpr int s_nPdfResolution = 1200;   // PDF 绘制分辨率 (dpi), 仅影响内部坐标精度, 输出为矢量
}

namespace CalibBoard {

CalibBoardSpec normalized(const CalibBoardSpec& spec)
{
    CalibBoardSpec out = spec;
    // 20261103 ZJH 非对称圆点至少 3 行才有交错结构, 其余至少 2×2
    out.nCols = std::clamp(out.nCols, 2, 200);
    out.nRows = std::clamp(out.nRows, out.eType == BoardType::CircleAsymmetric ? 3 : 2, 200);
    out.nPitchPx = std::clamp(out.nPitchPx, 4, 2000);
    out.dCircleRatio = std::clamp(out.dCircleRatio, 0.1, 0.9);
    out.nCodeSpacing = std::clamp(out.nCodeSpacing / 2 * 2, 4, 100);  // 取偶数, 保证错排位置为整数角点
    out.nMarginPitch = std::clamp(out.nMarginPitch, 0, 10);
    return out;  // 返回合法化参数
}

QSizeF patternSizeInPitch(const CalibBoardSpec& specIn)
{
    const CalibBoardSpec spec = normalized(specIn);
    switch (spec.eType) {
    case BoardType::CircleAsymmetric:
        return QSizeF(2.0 * spec.nCols, spec.nRows);         // 交错排列, 每行横向占 2 个格距
    case BoardType::HalconCircle:
        return QSizeF(spec.nCols + 1.0, spec.nRows + 1.0);   // 圆点四周各留半格 + 边框
    case BoardType::Chessboard:
    case BoardType::DmChessboard:
    case BoardType::CircleSymmetric:
    default:
        return QSizeF(spec.nCols, spec.nRows);
    }
}

QSizeF boardSizeInPitch(const CalibBoardSpec& specIn)
{
    const CalibBoardSpec spec = normalized(specIn);
    const QSizeF sizePattern = patternSizeInPitch(spec);
    return QSizeF(sizePattern.width() + 2.0 * spec.nMarginPitch, sizePattern.height() + 2.0 * spec.nMarginPitch);
}

QSize boardSizePx(const CalibBoardSpec& specIn)
{
    const CalibBoardSpec spec = normalized(specIn);
    const QSizeF size = boardSizeInPitch(spec);
    // 20261103 ZJH 各图案的格距数都是整数, 乘整数像素格距即精确像素尺寸
    return QSize(static_cast<int>(std::lround(size.width() * spec.nPitchPx)),
                 static_cast<int>(std::lround(size.height() * spec.nPitchPx)));
}

CalibBoardSpec fitToScreen(const CalibBoardSpec& specIn, const QSize& sizeScreenPx, bool bGrowGrid)
{
    CalibBoardSpec spec = normalized(specIn);  // 先合法化, 保证格数计算有效
    // 20261109 ZJH 读不到屏幕尺寸时不改参数
    if (sizeScreenPx.isEmpty()) {
        return spec;  // 原样返回
    }
    // 20261109 ZJH 最大整数格距 = floor(min(屏宽 / 整板格数宽, 屏高 / 整板格数高)), 向下取整保证放得下
    const QSizeF sizePitch = boardSizeInPitch(spec);
    const double dMax = std::min(sizeScreenPx.width() / sizePitch.width(), sizeScreenPx.height() / sizePitch.height());
    spec.nPitchPx = std::clamp(static_cast<int>(std::floor(dMax)), 4, 2000);
    // 20261109 ZJH 不补行列时只再调 DM 码间距
    if (!bGrowGrid) {
        if (spec.eType == BoardType::DmChessboard) {
            spec.nCodeSpacing = dmCodeSpacingFor(spec);  // 保证码数够
        }
        return spec;  // 只改格距 (DM 另调码间距)
    }
    // 20261109 ZJH 格距定好后, 受限方向已满; 另一方向逐列/逐行加到再加就放不下为止 (上限 200)
    auto fitsAt = [&sizeScreenPx](const CalibBoardSpec& specTry) {
        const QSize sizeBoard = boardSizePx(specTry);  // 加一列/行后的整板像素
        return sizeBoard.width() <= sizeScreenPx.width() && sizeBoard.height() <= sizeScreenPx.height();
    };
    while (spec.nCols < 200) {  // 补列
        CalibBoardSpec specTry = spec;
        ++specTry.nCols;
        if (!fitsAt(specTry)) {
            break;  // 再加一列就超宽
        }
        spec = specTry;
    }
    while (spec.nRows < 200) {  // 补行
        CalibBoardSpec specTry = spec;
        ++specTry.nRows;
        if (!fitsAt(specTry)) {
            break;  // 再加一行就超高
        }
        spec = specTry;
    }
    // 20261109 ZJH DM 码棋盘格: 行列定好后按码数调间距 (现场 14×7 + 间距 10 只有 1 个码, 视觉解不出姿态)
    if (spec.eType == BoardType::DmChessboard) {
        spec.nCodeSpacing = dmCodeSpacingFor(spec);
    }
    return spec;  // 返回铺满后的参数
}

std::vector<QPointF> featurePointsInPitch(const CalibBoardSpec& specIn)
{
    const CalibBoardSpec spec = normalized(specIn);
    const double m = spec.nMarginPitch;  // 边距偏移
    std::vector<QPointF> vecPts;
    switch (spec.eType) {
    case BoardType::Chessboard:
    case BoardType::DmChessboard:
        // 20261103 ZJH 内角点: 方格交点, 去掉外圈
        for (int i = 1; i < spec.nRows; ++i) {
            for (int j = 1; j < spec.nCols; ++j) {
                vecPts.emplace_back(m + j, m + i);
            }
        }
        break;
    case BoardType::CircleAsymmetric:
        for (int i = 0; i < spec.nRows; ++i) {
            for (int j = 0; j < spec.nCols; ++j) {
                vecPts.emplace_back(m + 2.0 * j + (i % 2) + 0.5, m + i + 0.5);
            }
        }
        break;
    case BoardType::HalconCircle:
        for (int i = 0; i < spec.nRows; ++i) {
            for (int j = 0; j < spec.nCols; ++j) {
                vecPts.emplace_back(m + j + 1.0, m + i + 1.0);
            }
        }
        break;
    case BoardType::CircleSymmetric:
    default:
        for (int i = 0; i < spec.nRows; ++i) {
            for (int j = 0; j < spec.nCols; ++j) {
                vecPts.emplace_back(m + j + 0.5, m + i + 0.5);
            }
        }
        break;
    }
    return vecPts;  // 返回特征点 (行优先)
}

void paint(QPainter& painter, const CalibBoardSpec& specIn, double u, bool bAntialiasCircles)
{
    const CalibBoardSpec spec = normalized(specIn);
    const QColor colorBg = spec.bInvert ? Qt::black : Qt::white;  // 背景色
    const QColor colorFg = spec.bInvert ? Qt::white : Qt::black;  // 图案色
    const QSizeF sizeBoard = boardSizeInPitch(spec);
    const double m = spec.nMarginPitch;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setRenderHint(QPainter::Antialiasing, false);  // 矩形一律不抗锯齿 (整数像素边界)
    painter.fillRect(QRectF(0, 0, sizeBoard.width() * u, sizeBoard.height() * u), colorBg);

    // 20261103 ZJH 棋盘格: 左上角方格为黑, (i+j) 为偶数的格子填色
    if (spec.eType == BoardType::Chessboard || spec.eType == BoardType::DmChessboard) {
        for (int i = 0; i < spec.nRows; ++i) {
            for (int j = 0; j < spec.nCols; ++j) {
                if ((i + j) % 2 == 0) {
                    painter.fillRect(QRectF((m + j) * u, (m + i) * u, u, u), colorFg);
                }
            }
        }
        // 20261103 ZJH DM 码: 码区 2×2 格涂黑 (前景色), 码的"深色"模块用背景色画 → 黑底白码 (light_on_dark)
        //            几何按像素格距计算后整体缩放 (屏幕/PNG 时缩放为 1, 模块落在整数像素)
        if (spec.eType == BoardType::DmChessboard) {
            const DmLayout layout = dmLayout(spec);
            const double k = u / spec.nPitchPx;        // 像素 → painter 单位
            const double dMod = layout.dModulePx * k;  // 模块边长
            for (const QPoint& pt : dmCodeCorners(spec)) {  // 遍历每个放码角点
                const double dBx = (m + pt.x() - 1) * u;  // 码区左上角 (角点左上一格)
                const double dBy = (m + pt.y() - 1) * u;
                painter.fillRect(QRectF(dBx, dBy, 2 * u, 2 * u), colorFg);
                DmSymbol sym;
                std::string strErr;
                if (!DataMatrix::encode(dmCodeText(spec, pt.x(), pt.y()).toStdString(), sym, strErr, layout.nSymbolSize)) {
                    continue;  // 理论上不会失败 (内容为短 ASCII)
                }
                const double dX0 = dBx + layout.dOffsetPx * k;  // 符号左上角
                const double dY0 = dBy + layout.dOffsetPx * k;
                for (int r = 0; r < sym.nSize; ++r) {  // 逐模块绘制
                    for (int c = 0; c < sym.nSize; ++c) {
                        if (sym.dark(r, c)) {
                            painter.fillRect(QRectF(dX0 + c * dMod, dY0 + r * dMod, dMod, dMod), colorBg);
                        }
                    }
                }
            }
        }
        painter.restore();
        return;
    }

    // 20261103 ZJH Halcon 风格: 外框 (四条边) + 左上角定向三角
    if (spec.eType == BoardType::HalconCircle) {
        const QSizeF sizePat = patternSizeInPitch(spec);
        const double dX0 = m * u, dY0 = m * u;                              // 图案区左上角
        const double dW = sizePat.width() * u, dH = sizePat.height() * u;   // 图案区宽高
        const double t = s_dHalconFrame * u;                                // 边框厚度
        painter.fillRect(QRectF(dX0, dY0, dW, t), colorFg);                 // 上
        painter.fillRect(QRectF(dX0, dY0 + dH - t, dW, t), colorFg);        // 下
        painter.fillRect(QRectF(dX0, dY0, t, dH), colorFg);                 // 左
        painter.fillRect(QRectF(dX0 + dW - t, dY0, t, dH), colorFg);        // 右
        painter.setRenderHint(QPainter::Antialiasing, bAntialiasCircles);
        painter.setBrush(colorFg);
        // 20261103 ZJH 定向三角: 直角在左上框内角, 两直角边长 0.45 格距 (不触及第一个圆)
        painter.drawPolygon(QPolygonF({ QPointF(dX0 + t, dY0 + t), QPointF(dX0 + t + 0.45 * u, dY0 + t),
                                        QPointF(dX0 + t, dY0 + t + 0.45 * u) }));
    }

    // 20261103 ZJH 圆点: 圆心 = 特征点, 半径 = 直径比 × 格距 / 2
    painter.setRenderHint(QPainter::Antialiasing, bAntialiasCircles);
    painter.setBrush(colorFg);
    const double r = spec.dCircleRatio * u / 2.0;
    for (const QPointF& pt : featurePointsInPitch(spec)) {
        painter.drawEllipse(QPointF(pt.x() * u, pt.y() * u), r, r);
    }
    painter.restore();
}

QImage render(const CalibBoardSpec& specIn, double dPixelPitchMm)
{
    CalibBoardSpec spec = normalized(specIn);
    // 20261103 ZJH 未指定码内格距时按 格距像素 × 点距 推算 (DM 码内容写入物理格距)
    if (spec.dCodePitchMm <= 0.0 && dPixelPitchMm > 0.0) {
        spec.dCodePitchMm = spec.nPitchPx * dPixelPitchMm;
    }
    QImage image(boardSizePx(spec), QImage::Format_Grayscale8);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        paint(painter, spec, spec.nPitchPx, true);
    }
    // 20261103 ZJH 写入 DPI 元数据 (每米点数 = 1000 / 点距mm), 打印/查看软件可按物理尺寸显示
    if (dPixelPitchMm > 0.0) {
        const int nDpm = static_cast<int>(std::lround(1000.0 / dPixelPitchMm));
        image.setDotsPerMeterX(nDpm);
        image.setDotsPerMeterY(nDpm);
    }
    return image;  // 返回精确像素图
}

bool exportPdf(const CalibBoardSpec& specIn, double dPitchMm, const QString& strPath, QString& strErr)
{
    CalibBoardSpec spec = normalized(specIn);
    spec.dCodePitchMm = dPitchMm;  // PDF 的码内格距 = 打印格距
    // 20261103 ZJH 格距必须为正
    if (dPitchMm <= 0.0) {
        strErr = QStringLiteral("格距必须大于 0");
        return false;
    }
    const QSizeF sizeBoard = boardSizeInPitch(spec);
    QPdfWriter writer(strPath);
    writer.setResolution(s_nPdfResolution);
    writer.setPageSize(QPageSize(QSizeF(sizeBoard.width() * dPitchMm, sizeBoard.height() * dPitchMm),
                                 QPageSize::Millimeter, QStringLiteral("CalibBoard")));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(QStringLiteral("AlignSim 标定板 - %1").arg(typeName(spec.eType)));
    QPainter painter;
    if (!painter.begin(&writer)) {
        strErr = QStringLiteral("无法写入 PDF: %1").arg(strPath);
        return false;
    }
    const double dUnitPerPitch = dPitchMm / 25.4 * s_nPdfResolution;  // 1 格距对应的设备单位
    paint(painter, spec, dUnitPerPitch, true);
    painter.end();
    return true;  // 返回成功
}

std::vector<QPoint> dmCodeCorners(const CalibBoardSpec& specIn)
{
    const CalibBoardSpec spec = normalized(specIn);
    const int s = spec.nCodeSpacing;  // 码间距 (格)
    const int h = s / 2;              // 错排偏移
    std::vector<QPoint> vecPts;
    // 20261103 ZJH 交错网格: (X%s==0 且 Y%s==0) 或 (X%s==h 且 Y%s==h); 码区 2×2 格须完全在图案内
    for (int y = 1; y < spec.nRows; ++y) {
        for (int x = 1; x < spec.nCols; ++x) {
            const bool bMain = (x % s == 0) && (y % s == 0);  // 主网格
            const bool bHalf = (x % s == h) && (y % s == h);  // 错排网格
            if (bMain || bHalf) {
                vecPts.emplace_back(x, y);
            }
        }
    }
    return vecPts;  // 返回放码角点 (行优先)
}

int dmCodeSpacingFor(const CalibBoardSpec& specIn)
{
    CalibBoardSpec spec = normalized(specIn);  // 间距已取偶数且 ≥ 4
    // 20261109 ZJH 从当前间距往下每次减 2, 第一个码数够的就是最大可用间距
    for (; spec.nCodeSpacing >= 4; spec.nCodeSpacing -= 2) {
        if (static_cast<int>(dmCodeCorners(spec).size()) >= s_nMinDmCodes) {
            return spec.nCodeSpacing;  // 码数够
        }
    }
    return 4;  // 行列太少, 最密也放不下 4 个, 用最小间距尽量多放
}

QString dmCodeText(const CalibBoardSpec& specIn, int nX, int nY)
{
    const CalibBoardSpec spec = normalized(specIn);
    // 20261103 ZJH 康耐视格式 "X,Y V1 P<格距> MM": 格距用最短十进制表示 (2 → "2", 1.3434 → "1.3434")
    const double dPitch = spec.dCodePitchMm > 0.0 ? spec.dCodePitchMm : 0.0;
    return QStringLiteral("%1,%2 V1 P%3 MM").arg(nX).arg(nY).arg(QString::number(dPitch, 'g', 6));
}

DmLayout dmLayout(const CalibBoardSpec& specIn)
{
    const CalibBoardSpec spec = normalized(specIn);
    DmLayout layout;
    // 20261103 ZJH 整板统一符号尺寸: 取所有码内容所需尺寸的最大值 (至少 10)
    layout.nSymbolSize = 10;
    for (const QPoint& pt : dmCodeCorners(spec)) {
        std::vector<int> vecCw;
        DataMatrix::encodeAscii(dmCodeText(spec, pt.x(), pt.y()).toStdString(), vecCw);
        layout.nSymbolSize = std::max(layout.nSymbolSize, DataMatrix::symbolSizeFor(static_cast<int>(vecCw.size())));
    }
    // 20261103 ZJH 码区边长 2 格; 符号 + 两侧各 1 模块黑边 = 码区 → 模块 = 2p / (S + 2), 取整数像素
    const double dBlockPx = 2.0 * spec.nPitchPx;
    const int nModPx = static_cast<int>(std::floor(dBlockPx / (layout.nSymbolSize + 2)));
    layout.dModulePx = nModPx >= 1 ? nModPx : dBlockPx / (layout.nSymbolSize + 2);
    const double dCodePx = layout.dModulePx * layout.nSymbolSize;  // 符号边长
    // 20261103 ZJH 居中; 整数模块时偏移也取整, 保证模块边界落在像素边界
    layout.dOffsetPx = nModPx >= 1 ? std::floor((dBlockPx - dCodePx) / 2.0) : (dBlockPx - dCodePx) / 2.0;
    return layout;  // 返回布局
}

QString typeName(BoardType eType)
{
    switch (eType) {
    case BoardType::CircleSymmetric: return QStringLiteral("对称圆点");
    case BoardType::CircleAsymmetric: return QStringLiteral("非对称圆点");
    case BoardType::HalconCircle: return QStringLiteral("Halcon 风格圆点板");
    case BoardType::DmChessboard: return QStringLiteral("DM 码棋盘格");
    case BoardType::Chessboard:
    default: return QStringLiteral("棋盘格");
    }
}

QString describe(const CalibBoardSpec& specIn, double dPitchMm)
{
    CalibBoardSpec spec = normalized(specIn);
    // 20261103 ZJH 未指定码内格距时与实际格距一致
    if (spec.dCodePitchMm <= 0.0) {
        spec.dCodePitchMm = dPitchMm;
    }
    const QSizeF sizeBoard = boardSizeInPitch(spec);
    const QString strPitch = QString::number(dPitchMm, 'f', 4);
    QString strText = QStringLiteral("图案: %1\n格距/圆心距: %2 mm (%3 px)\n整板尺寸: %4 × %5 mm\n")
                          .arg(typeName(spec.eType), strPitch).arg(spec.nPitchPx)
                          .arg(sizeBoard.width() * dPitchMm, 0, 'f', 3).arg(sizeBoard.height() * dPitchMm, 0, 'f', 3);
    switch (spec.eType) {
    case BoardType::Chessboard:
        strText += QStringLiteral("方格: %1 × %2, 内角点: %3 × %4\n")
                       .arg(spec.nCols).arg(spec.nRows).arg(spec.nCols - 1).arg(spec.nRows - 1);
        strText += QStringLiteral("OpenCV: findChessboardCorners(img, cv::Size(%1, %2), corners); squareSize = %3 mm\n")
                       .arg(spec.nCols - 1).arg(spec.nRows - 1).arg(strPitch);
        strText += QStringLiteral("Halcon: 棋盘格用 find_calib_object 不支持, 建议改用圆点板或 OpenCV 标定\n");
        break;
    case BoardType::CircleSymmetric:
        strText += QStringLiteral("圆点: %1 × %2, 直径 %3 mm\n").arg(spec.nCols).arg(spec.nRows)
                       .arg(spec.dCircleRatio * dPitchMm, 0, 'f', 4);
        strText += QStringLiteral("OpenCV: findCirclesGrid(img, cv::Size(%1, %2), centers, CALIB_CB_SYMMETRIC_GRID); squareSize = %3 mm\n")
                       .arg(spec.nCols).arg(spec.nRows).arg(strPitch);
        break;
    case BoardType::CircleAsymmetric:
        strText += QStringLiteral("圆点: 每行 %1 个 × %2 行 (交错), 直径 %3 mm\n").arg(spec.nCols).arg(spec.nRows)
                       .arg(spec.dCircleRatio * dPitchMm, 0, 'f', 4);
        strText += QStringLiteral("OpenCV: findCirclesGrid(img, cv::Size(%1, %2), centers, CALIB_CB_ASYMMETRIC_GRID); "
                                  "物点 ((2j + i%%2)·s, i·s), s = %3 mm (同行相邻圆心距 2s)\n")
                       .arg(spec.nCols).arg(spec.nRows).arg(strPitch).replace(QStringLiteral("%%"), QStringLiteral("%"));
        break;
    case BoardType::DmChessboard: {
        const DmLayout layout = dmLayout(spec);
        const std::vector<QPoint> vecCodes = dmCodeCorners(spec);
        const double dModMm = layout.dModulePx / spec.nPitchPx * dPitchMm;  // 模块物理尺寸
        strText += QStringLiteral("方格: %1 × %2, 内角点: %3 × %4 (格坐标 X = 1..%5, Y = 1..%6, 左上为原点)\n")
                       .arg(spec.nCols).arg(spec.nRows).arg(spec.nCols - 1).arg(spec.nRows - 1)
                       .arg(spec.nCols - 1).arg(spec.nRows - 1);
        strText += QStringLiteral("DM 码: %1 个, 间距 %2 格 (相邻排错开 %3 格); 每个码占以角点为中心的 2×2 格, 黑底白码 (light_on_dark)\n")
                       .arg(vecCodes.size()).arg(spec.nCodeSpacing).arg(spec.nCodeSpacing / 2);
        strText += QStringLiteral("      Data Matrix ECC200 %1×%1 模块, 模块 %2 px = %3 mm\n")
                       .arg(layout.nSymbolSize).arg(layout.dModulePx, 0, 'g', 4).arg(dModMm, 0, 'f', 4);
        if (!vecCodes.empty()) {
            strText += QStringLiteral("码内容: \"X,Y V1 P格距 MM\", 如 \"%1\" = 角点 (%2, %3), 物理坐标 (X·s, Y·s), s = %4 mm\n")
                           .arg(dmCodeText(spec, vecCodes.front().x(), vecCodes.front().y()))
                           .arg(vecCodes.front().x()).arg(vecCodes.front().y()).arg(strPitch);
        }
        strText += QStringLiteral("Halcon: create_data_code_2d_model('Data Matrix ECC 200', 'polarity', 'light_on_dark', DataCodeHandle)\n"
                                  "        find_data_code_2d(Image, SymbolXLDs, DataCodeHandle, 'stop_after_result_num', 1000,\n"
                                  "                          ResultHandles, DecodedDataStrings)\n"
                                  "        → 码中心即角点 (X,Y), 由此确定周围角点的格坐标 (saddle_points_sub_pix 亚像素精化)\n"
                                  "        → 物点 (X·s, Y·s, 0) 经 set_calib_data_calib_object 自定义标定物, 再 calibrate_cameras\n");
        // 20261103 ZJH 码太少无法定位, 模块过小读不出码 (实测 Halcon 26 读 2px 模块原图失败, 3px 起成功)
        // 20261109 ZJH 视觉至少 2 个码才定得出板的姿态, 少于 s_nMinDmCodes 即警告
        if (static_cast<int>(vecCodes.size()) < s_nMinDmCodes) {
            strText += QStringLiteral("警告: 板上只有 %1 个码 (建议 ≥ %2 个, 视觉至少要 2 个才定得出姿态), 请减小码间距或增加行列数\n")
                           .arg(vecCodes.size()).arg(s_nMinDmCodes);
        }
        if (layout.dModulePx < 3.0) {
            strText += QStringLiteral("警告: 屏上模块不足 3 px, 读码不可靠, 请增大格距\n");
        }
        strText += QStringLiteral("提示: 相机图像中每个模块应 ≥ 3~4 像素才能稳定读码\n");
        break;
    }
    case BoardType::HalconCircle:
        strText += QStringLiteral("圆点: %1 × %2, 直径 %3 mm, 带外框与左上角定向三角\n").arg(spec.nCols).arg(spec.nRows)
                       .arg(spec.dCircleRatio * dPitchMm, 0, 'f', 4);
        strText += QStringLiteral("Halcon: gen_caltab(%1, %2, %3, %4, 'caltab.descr', 'caltab.ps')  (MarkDist 单位 m)\n")
                       .arg(spec.nCols).arg(spec.nRows).arg(dPitchMm / 1000.0, 0, 'g', 8).arg(spec.dCircleRatio);
        strText += QStringLiteral("注意: 本板为近似 caltab, 圆点间距/直径与上式一致, 外框尺寸可能与官方 .descr 不同;\n"
                                  "      需要 find_caltab 严格匹配时, 用 gen_caltab 生成的 .ps 打印官方板\n");
        break;
    }
    return strText;  // 返回说明文本
}

}  // namespace CalibBoard
