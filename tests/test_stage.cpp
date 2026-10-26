// 20261025 ZJH 平台误差模型测试: 关闭不变、比例误差、反向间隙、重复性统计、旋转中心偏差、垂直度、UVW 安装误差、种子复现
#include <QtTest>

#include "motion/Stage.h"

#include <cmath>

namespace {
constexpr double s_dPi = 3.14159265358979323846;
}

class TestStage : public QObject
{
    Q_OBJECT
private slots:
    void disabled_identical()
    {
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.dRepeatUm = 50.0;   // 参数非 0 但总开关关闭
        err.dRotCenterErrX = 3.0;
        stage.setErrorModel(err);
        stage.moveAxesAbs(AxisValues{ 1.5, -2.0, 0.3 });
        const Pose poseAct = stage.pose();
        const Pose poseCmd = stage.commandedPose();
        QCOMPARE(poseAct.dX, 1.5);
        QCOMPARE(poseAct.dThetaDeg, 0.3);
        QCOMPARE(poseCmd.dY, -2.0);
        QCOMPARE(stage.actualRotCenter(), stage.rotCenter());
    }

    void scalePpm()
    {
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.bEnabled = true;
        err.arrScalePpm = { 1000.0, 0.0, 0.0 };  // X 轴 +0.1%
        stage.setErrorModel(err);
        stage.moveAxesAbs(AxisValues{ 100.0, 100.0, 0.0 });
        QVERIFY(std::abs(stage.actualAxes().dA1 - 100.1) < 1e-9);
        QVERIFY(std::abs(stage.actualAxes().dA2 - 100.0) < 1e-9);
        QCOMPARE(stage.axes().dA1, 100.0);  // 指令位置不受影响
        QVERIFY(std::abs(stage.pose().dX - 100.1) < 1e-9);
        QVERIFY(std::abs(stage.commandedPose().dX - 100.0) < 1e-9);
    }

    void backlashHysteresis()
    {
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.bEnabled = true;
        err.dBacklashUm = 10.0;  // 0.01mm
        stage.setErrorModel(err);
        stage.moveAxesAbs(AxisValues{ 1.0, 0.0, 0.0 });  // 正向
        QVERIFY(std::abs(stage.actualAxes().dA1 - 1.0) < 1e-12);
        stage.moveAxesAbs(AxisValues{ 0.0, 0.0, 0.0 });  // 负向回到 0: 实际停在 +0.01
        QVERIFY(std::abs(stage.actualAxes().dA1 - 0.01) < 1e-12);
        stage.moveAxesAbs(AxisValues{ 0.0, 0.0, 0.0 });  // 不动: 间隙状态保持
        QVERIFY(std::abs(stage.actualAxes().dA1 - 0.01) < 1e-12);
        stage.moveAxesRel(AxisValues{ 0.5, 0.0, 0.0 });  // 正向: 间隙消除
        QVERIFY(std::abs(stage.actualAxes().dA1 - 0.5) < 1e-12);
    }

    void repeatabilityStatistics()
    {
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.bEnabled = true;
        err.dRepeatUm = 2.0;  // σ = 0.002mm
        err.nSeed = 7;
        stage.setErrorModel(err);
        const int nCount = 4000;
        double dSum = 0.0, dSum2 = 0.0;
        for (int i = 0; i < nCount; ++i) {  // 先离开再回到同一点 (原地重复下发不算移动, 不重新抽样)
            stage.moveAxesAbs(AxisValues{ 4.0, 0.0, 0.0 });
            stage.moveAxesAbs(AxisValues{ 5.0, 0.0, 0.0 });
            const double d = stage.actualAxes().dA1 - 5.0;
            dSum += d;
            dSum2 += d * d;
        }
        const double dMean = dSum / nCount;
        const double dSigma = std::sqrt(dSum2 / nCount - dMean * dMean);
        QVERIFY2(std::abs(dMean) < 0.0002, qPrintable(QString::number(dMean)));
        QVERIFY2(std::abs(dSigma - 0.002) < 0.0002, qPrintable(QString::number(dSigma)));
    }

    void seedReproducible()
    {
        StageErrorModel err;
        err.bEnabled = true;
        err.dRepeatUm = 5.0;
        err.nSeed = 42;
        Stage a(QStringLiteral("a"));
        Stage b(QStringLiteral("b"));
        a.setErrorModel(err);
        b.setErrorModel(err);
        for (int i = 0; i < 10; ++i) {
            a.moveAxesAbs(AxisValues{ double(i), 1.0, 0.0 });
            b.moveAxesAbs(AxisValues{ double(i), 1.0, 0.0 });
        }
        QCOMPARE(a.actualAxes().dA1, b.actualAxes().dA1);
        QCOMPARE(a.actualAxes().dA2, b.actualAxes().dA2);
    }

    void rotCenterError()
    {
        // 20261025 ZJH 标称旋转中心 (0,0), 实际 (1,0): 指令纯转 90°, 原点处的点实际转到 (1,-1)
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.bEnabled = true;
        err.dRotCenterErrX = 1.0;
        stage.setErrorModel(err);
        QCOMPARE(stage.actualRotCenter(), QPointF(1.0, 0.0));
        stage.movePoseAbs(Pose{ 0.0, 0.0, 90.0 });
        const QPointF pt = stage.mapFromHome(QPointF(0.0, 0.0));
        QVERIFY(std::abs(pt.x() - 1.0) < 1e-9);
        QVERIFY(std::abs(pt.y() + 1.0) < 1e-9);
        QVERIFY(std::abs(stage.commandedPose().dThetaDeg - 90.0) < 1e-12);
    }

    void skewXyd()
    {
        // 20261025 ZJH 垂直度 1°: 指令 Y=+100 → 实际 X 偏 −100·sin1°, Y = 100·cos1°
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.bEnabled = true;
        err.dSkewDeg = 1.0;
        stage.setErrorModel(err);
        stage.moveAxesAbs(AxisValues{ 0.0, 100.0, 0.0 });
        const double dS = 1.0 * s_dPi / 180.0;
        QVERIFY(std::abs(stage.pose().dX + 100.0 * std::sin(dS)) < 1e-9);
        QVERIFY(std::abs(stage.pose().dY - 100.0 * std::cos(dS)) < 1e-9);
    }

    void uvwMountError()
    {
        // 20261025 ZJH UVW 安装误差: 指令纯平移仍是纯平移 (安装点偏差只影响转动), 指令转动时实际转角/平移有偏差
        Stage stage(QStringLiteral("s"));
        StageGeometry geo;
        geo.eType = StageType::UVW;
        stage.setGeometry(geo);
        StageErrorModel err;
        err.bEnabled = true;
        err.arrUvwMountErrUm = { 500.0, 500.0, 0.0, 0.0, 0.0, 0.0 };  // U 安装点 X/Y 偏 0.5mm (改变力臂)
        stage.setErrorModel(err);
        stage.movePoseAbs(Pose{ 2.0, 3.0, 0.0 });
        QVERIFY(std::abs(stage.pose().dX - 2.0) < 1e-9);
        QVERIFY(std::abs(stage.pose().dThetaDeg) < 1e-9);
        stage.movePoseAbs(Pose{ 0.0, 0.0, 1.0 });
        QVERIFY(stage.isPoseValid());
        QVERIFY(std::abs(stage.pose().dThetaDeg - 1.0) > 1e-4);       // 实际转角偏离指令
        QVERIFY(std::abs(stage.commandedPose().dThetaDeg - 1.0) < 1e-9);  // 指令位姿不变
    }

    void geometryChangeKeepsModel()
    {
        Stage stage(QStringLiteral("s"));
        StageErrorModel err;
        err.bEnabled = true;
        err.dRotCenterErrY = 2.0;
        stage.setErrorModel(err);
        StageGeometry geo;
        geo.dRotCenterX = 10.0;
        stage.setGeometry(geo);
        QCOMPARE(stage.actualRotCenter(), QPointF(10.0, 2.0));
        QCOMPARE(stage.errorModel().dRotCenterErrY, 2.0);
    }
};

QTEST_APPLESS_MAIN(TestStage)
#include "test_stage.moc"
