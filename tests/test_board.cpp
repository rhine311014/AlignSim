// 20261103 ZJH 标定板生成测试: 尺寸、棋盘格像素边界、圆心亚像素精度、非对称布局、Halcon 边框/三角、反色、DPI、PDF、参数说明
#include <QtTest>

#include "sim/CalibBoard.h"
#include "sim/DataMatrix.h"

#include <QFile>
#include <QTemporaryDir>
#include <cmath>

namespace {
// 20261103 ZJH 以 (dCx,dCy) 为中心、半宽 nHalf 的窗口内求暗像素加权质心 (像素中心坐标 = 下标 + 0.5)
QPointF darkCentroid(const QImage& img, double dCx, double dCy, int nHalf)
{
    double dSumW = 0.0, dSumX = 0.0, dSumY = 0.0;
    for (int y = static_cast<int>(dCy) - nHalf; y <= static_cast<int>(dCy) + nHalf; ++y) {
        const uchar* pLine = img.constScanLine(y);
        for (int x = static_cast<int>(dCx) - nHalf; x <= static_cast<int>(dCx) + nHalf; ++x) {
            const double w = 255.0 - pLine[x];
            dSumW += w;
            dSumX += w * (x + 0.5);
            dSumY += w * (y + 0.5);
        }
    }
    return dSumW > 0 ? QPointF(dSumX / dSumW, dSumY / dSumW) : QPointF();
}
}  // namespace

class TestBoard : public QObject
{
    Q_OBJECT
private slots:
    void sizes()
    {
        CalibBoardSpec spec;  // 棋盘格 10×7, 40px, 边距 1
        QCOMPARE(CalibBoard::boardSizePx(spec), QSize(480, 360));
        QCOMPARE(static_cast<int>(CalibBoard::featurePointsInPitch(spec).size()), 9 * 6);
        spec.eType = BoardType::CircleAsymmetric;
        spec.nCols = 4;
        spec.nRows = 11;
        QCOMPARE(CalibBoard::boardSizePx(spec), QSize((8 + 2) * 40, (11 + 2) * 40));
        spec.eType = BoardType::HalconCircle;
        spec.nCols = 7;
        spec.nRows = 7;
        QCOMPARE(CalibBoard::boardSizePx(spec), QSize((8 + 2) * 40, (8 + 2) * 40));
        // 20261103 ZJH 非法参数被合法化
        spec.nPitchPx = 1;
        spec.nCols = 0;
        QCOMPARE(CalibBoard::normalized(spec).nPitchPx, 4);
        QCOMPARE(CalibBoard::normalized(spec).nCols, 2);
    }

    void chessboardPixelExact()
    {
        CalibBoardSpec spec;
        const QImage img = CalibBoard::render(spec);
        QCOMPARE(img.size(), QSize(480, 360));
        // 20261103 ZJH 左上角方格 [40,80) 全黑, 边界外一像素全白, 无抗锯齿灰边
        QCOMPARE(qGray(img.pixel(40, 40)), 0);
        QCOMPARE(qGray(img.pixel(79, 79)), 0);
        QCOMPARE(qGray(img.pixel(80, 40)), 255);
        QCOMPARE(qGray(img.pixel(39, 40)), 255);
        QCOMPARE(qGray(img.pixel(40, 80)), 255);
        QCOMPARE(qGray(img.pixel(80, 80)), 0);
        // 20261103 ZJH 全图只有 0 和 255 两种灰度
        for (int y = 0; y < img.height(); ++y) {
            const uchar* p = img.constScanLine(y);
            for (int x = 0; x < img.width(); ++x) {
                QVERIFY(p[x] == 0 || p[x] == 255);
            }
        }
        // 20261103 ZJH 第一个内角点在 (80,80) 像素交界
        QCOMPARE(CalibBoard::featurePointsInPitch(spec).front() * spec.nPitchPx, QPointF(80, 80));
    }

    void circleCentroidSubpixel_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("对称") << static_cast<int>(BoardType::CircleSymmetric);
        QTest::newRow("非对称") << static_cast<int>(BoardType::CircleAsymmetric);
        QTest::newRow("Halcon") << static_cast<int>(BoardType::HalconCircle);
    }

    void circleCentroidSubpixel()
    {
        QFETCH(int, type);
        CalibBoardSpec spec;
        spec.eType = static_cast<BoardType>(type);
        spec.nCols = 4;
        spec.nRows = 5;
        spec.nPitchPx = 41;  // 奇数格距: 圆心落在像素中间, 检验非整数位置
        const QImage img = CalibBoard::render(spec);
        const int nHalf = static_cast<int>(spec.dCircleRatio * spec.nPitchPx / 2.0) + 2;
        double dMaxErr = 0.0;
        for (const QPointF& ptPitch : CalibBoard::featurePointsInPitch(spec)) {  // 每个圆心
            const QPointF ptExpect = ptPitch * spec.nPitchPx;
            const QPointF ptMeas = darkCentroid(img, ptExpect.x(), ptExpect.y(), nHalf);
            dMaxErr = std::max(dMaxErr, std::hypot(ptMeas.x() - ptExpect.x(), ptMeas.y() - ptExpect.y()));
        }
        QVERIFY2(dMaxErr < 0.05, qPrintable(QString::number(dMaxErr)));
    }

    void asymmetricLayout()
    {
        CalibBoardSpec spec;
        spec.eType = BoardType::CircleAsymmetric;
        spec.nCols = 4;
        spec.nRows = 11;
        const std::vector<QPointF> vecPts = CalibBoard::featurePointsInPitch(spec);
        QCOMPARE(static_cast<int>(vecPts.size()), 44);
        QCOMPARE(vecPts[0], QPointF(1.5, 1.5));            // 第 0 行第 0 个
        QCOMPARE(vecPts[1], QPointF(3.5, 1.5));            // 同行相邻相距 2
        QCOMPARE(vecPts[4], QPointF(2.5, 2.5));            // 第 1 行右移 1
    }

    void halconFrameAndTriangle()
    {
        CalibBoardSpec spec;
        spec.eType = BoardType::HalconCircle;
        spec.nCols = 7;
        spec.nRows = 7;
        const QImage img = CalibBoard::render(spec);
        QCOMPARE(qGray(img.pixel(41, 200)), 0);   // 左边框 (边距 40px 起, 厚 6px)
        QCOMPARE(qGray(img.pixel(50, 50)), 0);    // 定向三角内部
        QCOMPARE(qGray(img.pixel(348, 52)), 255); // 右上框内角无三角 (只在左上定向)
        QCOMPARE(qGray(img.pixel(20, 20)), 255);  // 边距白
    }

    void invertAndDpi()
    {
        CalibBoardSpec spec;
        spec.bInvert = true;
        const QImage img = CalibBoard::render(spec, 0.25);
        QCOMPARE(qGray(img.pixel(5, 5)), 0);      // 背景黑
        QCOMPARE(qGray(img.pixel(40, 40)), 255);  // 左上方格白
        QCOMPARE(img.dotsPerMeterX(), 4000);      // 0.25mm/px → 4000 点/米
    }

    void pdfAndDescribe()
    {
        QTemporaryDir dir;
        const QString strPath = dir.path() + QStringLiteral("/board.pdf");
        CalibBoardSpec spec;
        spec.eType = BoardType::HalconCircle;
        QString strErr;
        QVERIFY2(CalibBoard::exportPdf(spec, 10.0, strPath, strErr), qPrintable(strErr));
        QFile file(strPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(5) == "%PDF-");
        QVERIFY(!CalibBoard::exportPdf(spec, 0.0, strPath, strErr));
        const QString strDesc = CalibBoard::describe(spec, 10.0);
        QVERIFY(strDesc.contains(QStringLiteral("gen_caltab(10, 7, 0.01, 0.5")));
        spec.eType = BoardType::Chessboard;
        QVERIFY(CalibBoard::describe(spec, 5.0).contains(QStringLiteral("cv::Size(9, 6)")));
    }

    void dmChessboard()
    {
        // 20261103 ZJH DM 码棋盘格: 尺寸/角点同棋盘格, 码内容补零, 模块整数像素且与编码器逐模块一致
        CalibBoardSpec spec;
        spec.eType = BoardType::DmChessboard;
        spec.nPitchPx = 60;
        QCOMPARE(CalibBoard::boardSizePx(spec), QSize(720, 540));
        QCOMPARE(CalibBoard::featurePointsInPitch(spec).size(), size_t(54));
        QCOMPARE(CalibBoard::dmCodeText(spec, 0, 1), QStringLiteral("R00C01"));
        CalibBoardSpec specWide = spec;
        specWide.nCols = 120;
        QCOMPARE(CalibBoard::dmCodeText(specWide, 3, 7), QStringLiteral("R03C007"));

        // 20261103 ZJH 布局: 0.7×60 = 42px / 12 模块 → 3px 模块, 码 36px, 偏移 12px
        const CalibBoard::DmLayout layout = CalibBoard::dmLayout(spec);
        QCOMPARE(layout.nSymbolSize, 12);
        QCOMPARE(layout.dModulePx, 3.0);
        QCOMPARE(layout.dOffsetPx, 12.0);

        // 20261103 ZJH 第 0 行第 1 列白格 (含 1 格留白, 左上角像素 (120, 60)) 的码逐模块比对
        const QImage img = CalibBoard::render(spec);
        DmSymbol sym;
        std::string strErr;
        QVERIFY(DataMatrix::encode("R00C01", sym, strErr, layout.nSymbolSize));
        const int nX0 = 120 + 12, nY0 = 60 + 12;  // 码左上角像素
        for (int r = 0; r < sym.nSize; ++r) {
            for (int c = 0; c < sym.nSize; ++c) {
                const int nGray = qGray(img.pixel(nX0 + c * 3 + 1, nY0 + r * 3 + 1));  // 模块中心像素
                QCOMPARE(nGray < 128, sym.dark(r, c));
            }
        }
        // 20261103 ZJH 码四周静区为白, 黑格内无码
        QCOMPARE(qGray(img.pixel(120 + 5, 60 + 5)), 255);
        QCOMPARE(qGray(img.pixel(60 + 30, 60 + 30)), 0);
        // 20261103 ZJH 参数说明含 Halcon 读码与 OpenCV 角点
        const QString strDesc = CalibBoard::describe(spec, 5.0);
        QVERIFY(strDesc.contains(QStringLiteral("Data Matrix ECC 200")));
        QVERIFY(strDesc.contains(QStringLiteral("cv::Size(9, 6)")));
        QVERIFY(!strDesc.contains(QStringLiteral("警告")));
        spec.nPitchPx = 40;  // 28px / 12 → 2px 模块, 应警告
        QVERIFY(CalibBoard::describe(spec, 5.0).contains(QStringLiteral("警告")));
    }
};

QTEST_MAIN(TestBoard)
#include "test_board.moc"
