// 20261024 ZJH 运动学单元测试: XYD 恒等映射、UVW 逆解数值、UVW 正逆往返、奇异几何、位姿变换
#include <QtTest>

#include "motion/StageKinematics.h"

#include <cmath>

namespace {
constexpr double s_dPi = 3.14159265358979323846;
}

class TestKinematics : public QObject
{
    Q_OBJECT
private slots:
    void xyd_identity()
    {
        StageGeometry geo;  // 默认 XYD
        auto pKin = createKinematics(geo);
        const AxisValues axes = pKin->poseToAxes(Pose{ 1.5, -2.0, 3.0 });
        QCOMPARE(axes.dA1, 1.5);
        QCOMPARE(axes.dA3, 3.0);
        bool bOk = false;
        const Pose pose = pKin->axesToPose(AxisValues{ 4.0, 5.0, -6.0 }, &bOk);
        QVERIFY(bOk);
        QCOMPARE(pose.dY, 5.0);
        QCOMPARE(pose.dThetaDeg, -6.0);
        QVERIFY(pKin->isAngleAxis(2));
        QVERIFY(!pKin->isAngleAxis(0));
    }

    void uvw_inverseTranslation()
    {
        StageGeometry geo;
        geo.eType = StageType::UVW;  // 默认吉嘉 G+ XXY250-90-250-SV-X: U(85,0,90°), V(0,85,180°), W(-85,0,-90°)
        auto pKin = createKinematics(geo);
        // 20261026 ZJH 纯平移 (dX=1, dY=2): U(+Y1)=+2, V(-X)=-1, W(-Y2)=-2
        const AxisValues axes = pKin->poseToAxes(Pose{ 1.0, 2.0, 0.0 });
        QVERIFY(std::abs(axes.dA1 - 2.0) < 1e-12);
        QVERIFY(std::abs(axes.dA2 + 1.0) < 1e-12);
        QVERIFY(std::abs(axes.dA3 + 2.0) < 1e-12);
        QVERIFY(!pKin->isAngleAxis(2));
    }

    void uvw_inverseRotationSign()
    {
        StageGeometry geo;
        geo.eType = StageType::UVW;
        geo.arrUvw = makeUvwPreset(UvwPreset::X1X2Y_Diag100);
        auto pKin = createKinematics(geo);
        // 20261024 ZJH 小角度: U(-100,100) 沿 X → ≈ -100θ; V(100,-100) → ≈ +100θ; W(100,100) 沿 Y → ≈ +100θ
        const double dTheta = 0.01 * s_dPi / 180.0;  // 0.01° 转弧度
        const AxisValues axes = pKin->poseToAxes(Pose{ 0.0, 0.0, 0.01 });
        QVERIFY(std::abs(axes.dA1 + 100.0 * dTheta) < 1e-5);
        QVERIFY(std::abs(axes.dA2 - 100.0 * dTheta) < 1e-5);
        QVERIFY(std::abs(axes.dA3 - 100.0 * dTheta) < 1e-5);
    }

    void uvw_gplusManualExample()
    {
        // 20261026 ZJH 严格核对《吉嘉精密 XXY250-90-250-SV-X 使用说明书》第 7 页「3-4 移动算法」计算示例
        // 各轴脚初始坐标:
        //   U: (25.456, 25.456), 方向 90° (Y1)
        //   V: (-25.456, 25.456), 方向 180° (X)
        //   W: (-25.456, -25.456), 方向 -90° (Y2)
        // 旋转中心: (at=0, bt=0)
        StageGeometry geo;
        geo.eType = StageType::UVW;
        geo.arrUvw = makeUvwPreset(UvwPreset::GPlus_Manual_25_456);
        auto pKin = createKinematics(geo);

        struct StepCase {
            Pose absStart;    // 动作前绝对移动量 (X, Y, θ)
            Pose relMove;     // 本步相对移动量 (X', Y', θ')
            double dExpU;     // 说明书表列 U 轴移动量 (Y1)
            double dExpV;     // 说明书表列 V 軸移動量 (X)
            double dExpW;     // 说明书表列 W 軸移動量 (Y2)
        };
        const StepCase arrSteps[5] = {
            { {  0,  0,  0 }, {  1,  2,  1 },  2.44039, -0.55961, -1.55961 },
            { {  1,  2,  1 }, {  2,  1, -2 },  0.11146, -2.88854, -1.88854 },
            { {  3,  3, -1 }, { -5, -4,  4 }, -2.25448,  6.74552,  5.74552 },
            { { -2, -1,  3 }, {  3,  2, -2 },  1.14301, -3.85699, -2.85699 },
            { {  1,  1,  1 }, { -2,  1, -1 },  0.55961,  1.55961, -1.44039 },
        };
        for (int i = 0; i < 5; ++i) {
            const StepCase& s = arrSteps[i];
            const Pose absEnd{ s.absStart.dX + s.relMove.dX,
                               s.absStart.dY + s.relMove.dY,
                               s.absStart.dThetaDeg + s.relMove.dThetaDeg };
            const AxisValues axStart = pKin->poseToAxes(s.absStart);
            const AxisValues axEnd = pKin->poseToAxes(absEnd);
            const double dU = axEnd.dA1 - axStart.dA1;
            const double dV = axEnd.dA2 - axStart.dA2;
            const double dW = axEnd.dA3 - axStart.dA3;
            QVERIFY2(std::abs(dU - s.dExpU) < 1e-5, qPrintable(QStringLiteral("Step %1 U=%2 exp=%3").arg(i + 1).arg(dU).arg(s.dExpU)));
            QVERIFY2(std::abs(dV - s.dExpV) < 1e-5, qPrintable(QStringLiteral("Step %1 V=%2 exp=%3").arg(i + 1).arg(dV).arg(s.dExpV)));
            QVERIFY2(std::abs(dW - s.dExpW) < 1e-5, qPrintable(QStringLiteral("Step %1 W=%2 exp=%3").arg(i + 1).arg(dW).arg(s.dExpW)));

            // 同时验证每一步终点轴值的正解能精确还原绝对位姿 absEnd
            bool bOk = false;
            const Pose poseSolved = pKin->axesToPose(axEnd, &bOk);
            QVERIFY(bOk);
            QVERIFY(std::abs(poseSolved.dX - absEnd.dX) < 1e-7);
            QVERIFY(std::abs(poseSolved.dY - absEnd.dY) < 1e-7);
            QVERIFY(std::abs(poseSolved.dThetaDeg - absEnd.dThetaDeg) < 1e-7);
        }

        // 验证非零旋转中心 (at=10, bt=-5) 下吉嘉公式一致且正逆往返精确
        geo.dRotCenterX = 10.0;
        geo.dRotCenterY = -5.0;
        auto pKinRc = createKinematics(geo);
        bool bOk = false;
        const Pose poseTarget{ 1.5, -2.0, 2.5 };
        const AxisValues axRc = pKinRc->poseToAxes(poseTarget);
        const Pose poseBack = pKinRc->axesToPose(axRc, &bOk);
        QVERIFY(bOk);
        QVERIFY(std::abs(poseBack.dX - poseTarget.dX) < 1e-7);
        QVERIFY(std::abs(poseBack.dY - poseTarget.dY) < 1e-7);
        QVERIFY(std::abs(poseBack.dThetaDeg - poseTarget.dThetaDeg) < 1e-7);
    }

    void uvw_roundTrip_data()
    {
        QTest::addColumn<double>("dX");
        QTest::addColumn<double>("dY");
        QTest::addColumn<double>("dTheta");
        QTest::newRow("零位") << 0.0 << 0.0 << 0.0;
        QTest::newRow("纯平移") << 5.0 << -3.0 << 0.0;
        QTest::newRow("小角度") << 0.2 << 0.1 << 0.05;
        QTest::newRow("大角度3度") << 5.0 << -3.0 << 3.0;
        QTest::newRow("负角度5度") << -8.0 << 12.0 << -5.0;
        QTest::newRow("超大角度20度") << 30.0 << -20.0 << 20.0;
    }

    void uvw_roundTrip()
    {
        QFETCH(double, dX);
        QFETCH(double, dY);
        QFETCH(double, dTheta);
        StageGeometry geo;
        geo.eType = StageType::UVW;
        auto pKin = createKinematics(geo);
        const Pose poseIn{ dX, dY, dTheta };
        bool bOk = false;
        const Pose poseOut = pKin->axesToPose(pKin->poseToAxes(poseIn), &bOk);
        QVERIFY(bOk);
        // 20261024 ZJH 往返误差 < 1e-6 mm / 1e-6 度
        QVERIFY2(std::abs(poseOut.dX - dX) < 1e-6, qPrintable(QString::number(poseOut.dX - dX)));
        QVERIFY2(std::abs(poseOut.dY - dY) < 1e-6, qPrintable(QString::number(poseOut.dY - dY)));
        QVERIFY2(std::abs(poseOut.dThetaDeg - dTheta) < 1e-6, qPrintable(QString::number(poseOut.dThetaDeg - dTheta)));
    }

    void uvw_customXY1Y2()
    {
        // 20261024 ZJH 另一种结构: U 沿 X, V/W 沿 Y, 也要能往返
        StageGeometry geo;
        geo.eType = StageType::UVW;
        geo.arrUvw = { UvwAxisMount{ 0.0, -80.0, 0.0 }, UvwAxisMount{ -80.0, 0.0, 90.0 }, UvwAxisMount{ 80.0, 0.0, 90.0 } };
        QVERIFY(isUvwGeometryValid(geo));
        auto pKin = createKinematics(geo);
        bool bOk = false;
        const Pose poseOut = pKin->axesToPose(pKin->poseToAxes(Pose{ 1.0, 2.0, 2.0 }), &bOk);
        QVERIFY(bOk);
        QVERIFY(std::abs(poseOut.dThetaDeg - 2.0) < 1e-6);
    }

    void uvw_singular()
    {
        // 20261024 ZJH 三轴全沿 X: Y 方向不可控, 几何无效, 正解失败
        StageGeometry geo;
        geo.eType = StageType::UVW;
        geo.arrUvw = { UvwAxisMount{ 0.0, 0.0, 0.0 }, UvwAxisMount{ 0.0, 50.0, 0.0 }, UvwAxisMount{ 0.0, -50.0, 0.0 } };
        QVERIFY(!isUvwGeometryValid(geo));
        auto pKin = createKinematics(geo);
        bool bOk = true;
        pKin->axesToPose(AxisValues{ 1.0, 2.0, 3.0 }, &bOk);
        QVERIFY(!bOk);
    }

    void applyPose_rotateAboutCenter()
    {
        // 20261024 ZJH 旋转中心 (10,0), 点 (20,0) 转 90° → (10,10); 再平移 (1,2) → (11,12)
        const QPointF pt = applyPose(Pose{ 1.0, 2.0, 90.0 }, QPointF(10, 0), QPointF(20, 0));
        QVERIFY(std::abs(pt.x() - 11.0) < 1e-9);
        QVERIFY(std::abs(pt.y() - 12.0) < 1e-9);
        QCOMPARE(normalizeDeg(270.0), -90.0);
        QCOMPARE(normalizeDeg(-180.0), 180.0);
    }
};

QTEST_APPLESS_MAIN(TestKinematics)
#include "test_kinematics.moc"
