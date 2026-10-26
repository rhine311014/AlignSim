// 20261024 ZJH 场景单元测试: 对位残差 (单/多 Mark)、来料偏差、拾取命中与误差、产品跟随、随机摆放
#include <QtTest>

#include "motion/Stage.h"
#include "sim/AlignScene.h"
#include "sim/PickScene.h"

#include <cmath>

class TestScenes : public QObject
{
    Q_OBJECT
private slots:
    void align_singleMarkResidual()
    {
        Stage stage(QStringLiteral("align"));  // XYD, 旋转中心 (0,0)
        AlignScene scene(&stage);
        scene.setCameras({ AlignCamera{ QStringLiteral("相机1"), 0, 0, 20, 15 } });
        scene.setMarks({ MarkPair{ QStringLiteral("M1"), 0, 0, 0, 0, 0, 0, 0 } });
        scene.setObjectOffset(Pose{ 1.0, -0.5, 0.3 });
        AlignResidual res = scene.residual();
        QVERIFY(res.bValid);
        QVERIFY(std::abs(res.dX - 1.0) < 1e-9);
        QVERIFY(std::abs(res.dThetaDeg - 0.3) < 1e-9);
        // 20261024 ZJH 纯平移偏差: 平台反向平移即归零
        scene.setObjectOffset(Pose{ 1.0, -0.5, 0.0 });
        stage.movePoseAbs(Pose{ -1.0, 0.5, 0.0 });
        res = scene.residual();
        QVERIFY(std::abs(res.dX) < 1e-9);
        QVERIFY(std::abs(res.dY) < 1e-9);
        // 20261024 ZJH 纯旋转偏差且 Mark 在旋转中心: 平台反向旋转即归零
        // (Mark 不在旋转中心时, 旋转补偿会带出平移, 见 align_rotCenterOffset)
        scene.setObjectOffset(Pose{ 0.0, 0.0, 0.3 });
        stage.movePoseAbs(Pose{ 0.0, 0.0, -0.3 });
        res = scene.residual();
        QVERIFY(std::abs(res.dX) < 1e-9);
        QVERIFY(std::abs(res.dThetaDeg) < 1e-9);
    }

    void align_multiMarkRotation()
    {
        Stage stage(QStringLiteral("align"));
        AlignScene scene(&stage);
        scene.applyPresetDualCamera();  // 两 Mark 质心在 (0,0), 与旋转中心重合
        QCOMPARE(static_cast<int>(scene.cameras().size()), 2);
        scene.setObjectOffset(Pose{ 0.0, 0.0, 0.5 });
        AlignResidual res = scene.residual();
        QVERIFY(std::abs(res.dThetaDeg - 0.5) < 1e-9);
        QVERIFY(std::abs(res.dX) < 1e-9);
        QCOMPARE(static_cast<int>(res.vecMarkDelta.size()), 2);
        // 20261024 ZJH 单个 Mark 的位置差: 右侧 Mark 绕质心转 0.5° → Δy ≈ 100·sin(0.5°)
        QVERIFY(std::abs(res.vecMarkDelta[1].y() - 100.0 * std::sin(0.5 * 3.14159265358979323846 / 180.0)) < 1e-9);
        stage.movePoseRel(Pose{ 0.0, 0.0, -0.5 });
        res = scene.residual();
        QVERIFY(std::abs(res.dThetaDeg) < 1e-9);
    }

    void align_rotCenterOffset()
    {
        // 20261024 ZJH 旋转中心不在 Mark 质心: 只转不平移会产生平移残差 (真实平台的典型现象)
        Stage stage(QStringLiteral("align"));
        StageGeometry geo;
        geo.dRotCenterX = 0.0;
        geo.dRotCenterY = -50.0;
        stage.setGeometry(geo);
        AlignScene scene(&stage);
        scene.applyPresetDualCamera();
        stage.movePoseAbs(Pose{ 0.0, 0.0, 1.0 });
        const AlignResidual res = scene.residual();
        QVERIFY(std::abs(res.dThetaDeg - 1.0) < 1e-9);
        QVERIFY(std::abs(res.dX) > 0.5);  // 质心绕 (0,-50) 转 1° → Δx ≈ -0.87mm
    }

    void pick_hitAndFollow()
    {
        Stage head(QStringLiteral("pick"));  // XYD, 旋转中心 (0,0) → 吸嘴世界坐标 = 平移量
        PickScene scene(&head);
        PickSceneConfig cfg;
        cfg.nCount = 2;
        scene.setConfig(cfg);
        scene.spawnProducts(2, 7);
        QVERIFY(scene.setProductPose(0, Pose{ 100.0, 100.0, 30.0 }));
        QVERIFY(scene.setProductFlipped(0, false));
        QVERIFY(scene.setProductPose(1, Pose{ 400.0, 300.0, 0.0 }));
        head.movePoseAbs(Pose{ 101.0, 100.0, 31.0 });
        const PickResult res = scene.pick();
        QVERIFY(res.bHit);
        QCOMPARE(res.nProductId, 1);
        QVERIFY(std::abs(res.dX - 1.0) < 1e-9);
        QVERIFY(std::abs(res.dThetaDeg - 1.0) < 1e-9);
        QVERIFY(res.bInTolerance);
        QVERIFY(scene.isHolding());
        // 20261024 ZJH 吸着后再吸 → 不命中
        QVERIFY(!scene.pick().bHit);
        // 20261024 ZJH 拾取头平移 50mm, 产品跟着走, 相对关系不变
        head.movePoseRel(Pose{ 50.0, 0.0, 0.0 });
        std::vector<Product> vecProd = scene.products();
        QVERIFY(std::abs(vecProd[0].pose.dX - 150.0) < 1e-9);
        QVERIFY(std::abs(vecProd[0].pose.dThetaDeg - 30.0) < 1e-9);
        // 20261024 ZJH 拾取头转 10°, 产品绕吸嘴转
        head.movePoseRel(Pose{ 0.0, 0.0, 10.0 });
        vecProd = scene.products();
        QVERIFY(std::abs(vecProd[0].pose.dThetaDeg - 40.0) < 1e-9);
        QVERIFY(scene.place());
        QVERIFY(!scene.isHolding());
        QVERIFY(!scene.place());
        const PickStats stats = scene.stats();
        QCOMPARE(stats.nAttempts, 2);
        QCOMPARE(stats.nHits, 1);
        QVERIFY(std::abs(stats.dMaxDist - 1.0) < 1e-9);
    }

    void pick_missAndTolerance()
    {
        Stage head(QStringLiteral("pick"));
        PickScene scene(&head);
        scene.spawnProducts(1, 1);
        scene.setProductPose(0, Pose{ 200.0, 200.0, 0.0 });
        head.movePoseAbs(Pose{ 10.0, 10.0, 0.0 });
        QVERIFY(!scene.pick().bHit);  // 远离产品
        // 20261024 ZJH 落在产品轮廓内但偏离中心 20mm → 命中但超容差
        head.movePoseAbs(Pose{ 220.0, 200.0, 0.0 });
        const PickResult res = scene.pick();
        QVERIFY(res.bHit);
        QVERIFY(!res.bInTolerance);
    }

    void pick_spawnInsideField()
    {
        Stage head(QStringLiteral("pick"));
        PickScene scene(&head);
        PickSceneConfig cfg;
        cfg.nCount = 6;
        scene.setConfig(cfg);
        scene.spawnProducts(-1, 42);
        const std::vector<Product> vecProd = scene.products();
        QCOMPARE(static_cast<int>(vecProd.size()), 6);
        for (const Product& prod : vecProd) {
            QVERIFY(prod.pose.dX > 0 && prod.pose.dX < cfg.dFieldW);
            QVERIFY(prod.pose.dY > 0 && prod.pose.dY < cfg.dFieldH);
        }
        // 20261024 ZJH 相同种子可复现
        scene.spawnProducts(-1, 42);
        QCOMPARE(scene.products()[3].pose.dX, vecProd[3].pose.dX);
    }

    void align_screenOffsetPx()
    {
        Stage stage(QStringLiteral("align"));
        AlignScene scene(&stage);  // 默认单相机 (0,0), Mark1 (-20,0)
        QPointF ptPx;
        QVERIFY(!scene.objectMarkScreenOffset(0, ptPx));  // 未开固定比例 → 无效
        AlignDisplay disp;
        disp.bFixedScale = true;
        disp.dPxPerMm = 10.0;
        scene.setDisplay(disp);
        QVERIFY(scene.objectMarkScreenOffset(0, ptPx));
        QCOMPARE(ptPx, QPointF(-200.0, 0.0));
        // 20261025 ZJH 平台 Y+1mm: Y 朝上显示时屏幕向上 10px (屏幕 Y 减小)
        stage.movePoseAbs(Pose{ 0.0, 1.0, 0.0 });
        QVERIFY(scene.objectMarkScreenOffset(0, ptPx));
        QVERIFY(std::abs(ptPx.y() + 10.0) < 1e-9);
        disp.bYDown = true;
        scene.setDisplay(disp);
        QVERIFY(scene.objectMarkScreenOffset(0, ptPx));
        QVERIFY(std::abs(ptPx.y() - 10.0) < 1e-9);
        QVERIFY(!scene.objectMarkScreenOffset(5, ptPx));  // 越界
    }

    void pick_calibMark()
    {
        Stage head(QStringLiteral("pick"));
        PickScene scene(&head);  // 视野 600x400, 中心 (300,200)
        PickDisplay disp;
        disp.dCalibOffsetX = 20.0;
        disp.bFixedScale = true;
        disp.dPxPerMm = 1.5;
        scene.setDisplay(disp);
        head.movePoseAbs(Pose{ 100.0, 50.0, 0.0 });
        Pose poseMark = scene.calibMarkPose();
        QVERIFY(std::abs(poseMark.dX - 120.0) < 1e-9);
        QPointF ptPx;
        QVERIFY(scene.calibMarkScreenOffset(ptPx));
        QVERIFY(std::abs(ptPx.x() + 270.0) < 1e-9);  // (120-300)*1.5
        QVERIFY(std::abs(ptPx.y() - 225.0) < 1e-9);  // -(50-200)*1.5
        // 20261025 ZJH 拾取头转 90°: 偏移跟着转, Mark 到 (100, 70)
        head.movePoseAbs(Pose{ 100.0, 50.0, 90.0 });
        poseMark = scene.calibMarkPose();
        QVERIFY(std::abs(poseMark.dX - 100.0) < 1e-9);
        QVERIFY(std::abs(poseMark.dY - 70.0) < 1e-9);
        // 20261026 ZJH moveCalibMarkToFieldCenter: 标定 Mark 正好落在视野中心 (300, 200)
        scene.moveCalibMarkToFieldCenter();
        poseMark = scene.calibMarkPose();
        QVERIFY(std::abs(poseMark.dX - 300.0) < 1e-9);
        QVERIFY(std::abs(poseMark.dY - 200.0) < 1e-9);
    }

    void pick_uvwHead()
    {
        // 20261024 ZJH UVW 拾取头: 用位姿接口移动 (纯平移, 不依赖正解迭代精度)
        Stage head(QStringLiteral("pick"));
        StageGeometry geo;
        geo.eType = StageType::UVW;
        head.setGeometry(geo);
        PickScene scene(&head);
        scene.spawnProducts(1, 3);
        scene.setProductPose(0, Pose{ 50.0, 60.0, 0.0 });
        head.movePoseAbs(Pose{ 50.5, 60.0, 0.0 });
        const PickResult res = scene.pick();
        QVERIFY(res.bHit);
        QVERIFY( std::abs(res.dX - 0.5) < 1e-6);
    }

    void pick_flippedProduct()
    {
        // 20261026 ZJH 反面产品: 随机摆放包含正反, 手动切换反面后拾取判为不合格并计入 nFlippedPicked
        Stage head(QStringLiteral("pick"));
        PickScene scene(&head);
        PickSceneConfig cfg;
        cfg.nCount = 4;
        cfg.bRandomFlip = true;
        scene.setConfig(cfg);
        scene.spawnProducts(4, 11);
        int nFront = 0, nBack = 0;
        for (const Product& p : scene.products()) {
            if (p.bFlipped) ++nBack; else ++nFront;
        }
        QVERIFY(nFront > 0 && nBack > 0);
        // 把第 0 个设为反面并精确对准拾取 → 命中但因反面判为 NG
        QVERIFY(scene.setProductPose(0, Pose{ 100.0, 100.0, 0.0 }));
        QVERIFY(scene.setProductFlipped(0, true));
        head.movePoseAbs(Pose{ 100.0, 100.0, 0.0 });
        const PickResult res = scene.pick();
        QVERIFY(res.bHit);
        QVERIFY(res.bFlipped);
        QVERIFY(!res.bInTolerance);
        QCOMPARE(scene.stats().nFlippedPicked, 1);
    }

    void pick_dualGripperSequential()
    {
        // 20261026 ZJH 双爪先后拾取测试: 1号爪(-30,0) 先抓产品 1, 再移 2号爪(+30,0) 抓产品 2 (产品 1 跟随平移+旋转), 最后一起放下
        Stage head(QStringLiteral("pick"));
        PickScene scene(&head);
        PickSceneConfig cfg;
        cfg.nCount = 2;
        cfg.bDualGripper = true;
        cfg.dNozzle1OffX = -30.0;
        cfg.dNozzle1OffY = 0.0;
        cfg.dNozzle2OffX = 30.0;
        cfg.dNozzle2OffY = 0.0;
        scene.setConfig(cfg);
        QCOMPARE(scene.nozzleCount(), 2);
        scene.spawnProducts(2, 5);
        QVERIFY(scene.setProductPose(0, Pose{ 120.0, 150.0, 0.0 }));
        QVERIFY(scene.setProductFlipped(0, false));
        QVERIFY(scene.setProductPose(1, Pose{ 320.0, 250.0, 90.0 }));
        QVERIFY(scene.setProductFlipped(1, false));

        // 1. 移动拾取头使 1号爪对准产品 1 (120, 150, 0°)
        head.movePoseAbs(scene.headPoseForNozzleTarget(1, Pose{ 120.0, 150.0, 0.0 }));
        const Pose poseN1 = scene.nozzlePose(1);
        QVERIFY(std::abs(poseN1.dX - 120.0) < 1e-9);
        QVERIFY(std::abs(poseN1.dY - 150.0) < 1e-9);
        const PickResult r1 = scene.pick(1);
        QVERIFY(r1.bHit);
        QCOMPARE(r1.nNozzle, 1);
        QCOMPARE(r1.nProductId, 1);
        QVERIFY(scene.isHolding(1));
        QVERIFY(!scene.isHolding(2));
        QVERIFY(scene.isHolding(0));

        // 2. 移动拾取头使 2号爪对准产品 2 (320, 250, 90°), 验证 1号爪上的产品 1 跟随旋转中心同步旋转和平移
        head.movePoseAbs(scene.headPoseForNozzleTarget(2, Pose{ 320.0, 250.0, 90.0 }));
        const Pose poseN2 = scene.nozzlePose(2);
        QVERIFY(std::abs(poseN2.dX - 320.0) < 1e-9);
        QVERIFY(std::abs(poseN2.dY - 250.0) < 1e-9);
        QVERIFY(std::abs(poseN2.dThetaDeg - 90.0) < 1e-9);
        const PickResult r2 = scene.pick(2);
        QVERIFY(r2.bHit);
        QCOMPARE(r2.nNozzle, 2);
        QCOMPARE(r2.nProductId, 2);
        QVERIFY(scene.isHolding(1));
        QVERIFY(scene.isHolding(2));

        // 3. 移到放料位 (650, 200, 0°) 并一次性放下两个产品
        head.movePoseAbs(Pose{ 650.0, 200.0, 0.0 });
        const std::vector<Product> vecBeforePlace = scene.products();
        // 此时 θ=0, 1号爪在 (620, 200), 2号爪在 (680, 200)
        QVERIFY(std::abs(vecBeforePlace[0].pose.dX - 620.0) < 1e-9);
        QVERIFY(std::abs(vecBeforePlace[0].pose.dY - 200.0) < 1e-9);
        QVERIFY(std::abs(vecBeforePlace[1].pose.dX - 680.0) < 1e-9);
        QVERIFY(std::abs(vecBeforePlace[1].pose.dY - 200.0) < 1e-9);
        QVERIFY(scene.place(0));
        QVERIFY(!scene.isHolding(0));
        QVERIFY(!scene.isHolding(1));
        QVERIFY(!scene.isHolding(2));
    }
};

QTEST_APPLESS_MAIN(TestScenes)
#include "test_scenes.moc"
