// 20261024 ZJH Lua 引擎测试: 寄存器读写、轴/位姿运动与 Int32 换算、拾取函数、停止死循环、语法检查
#include <QtTest>

#include "core/RegisterBank.h"
#include "motion/Stage.h"
#include "script/LuaEngine.h"
#include "sim/AlignScene.h"
#include "sim/PickScene.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <array>
#include <cmath>

class TestLua : public QObject
{
    Q_OBJECT
private:
    // 20261024 ZJH 每个用例一套独立对象
    struct Fixture {
        RegisterBank bank;
        Stage alignStage{ QStringLiteral("align") };
        Stage pickStage{ QStringLiteral("pick") };
        AlignScene alignScene{ &alignStage };
        PickScene pickScene{ &pickStage };
        QTemporaryDir logDir;  // 标定 CSV 输出目录 (用例结束自动删除)
        LuaEngine engine{ LuaContext{ &bank, &alignStage, &pickStage, &alignScene, &pickScene,
                                      QString::fromUtf8(ALIGNSIM_SCRIPTS_DIR), logDir.path() } };
    };

    // 20261025 ZJH 轮询等待条件成立 (不依赖事件循环); 超时返回 false
    template <typename Pred>
    static bool waitFor(Pred pred, int nTimeoutMs = 3000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < nTimeoutMs) {
            if (pred()) {
                return true;
            }
            QThread::msleep(5);
        }
        return false;
    }

    // 20261024 ZJH 同步运行脚本, 返回是否成功; strErr 输出错误
    static bool runSync(Fixture& fx, const QString& strCode, QString& strErr, int nTimeoutMs = 5000)
    {
        bool bOk = false;
        QString strFinishErr;
        QObject::connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                         [&](const QString&, bool b, const QString& e) { bOk = b; strFinishErr = e; }, Qt::DirectConnection);
        if (!fx.engine.start(QStringLiteral("t"), strCode, strErr)) {
            return false;
        }
        fx.engine.waitForDone(nTimeoutMs);
        strErr = strFinishErr;
        return bOk;
    }

private slots:
    void registers()
    {
        Fixture fx;
        QString strErr;
        QVERIFY2(runSync(fx, "SetInt32('D100', 123456)\n"
                             "local x = GetInt32('DM100')\n"
                             "SetInt16(200, x % 1000)\n"
                             "SetFloat('D300', 1.25)\n", strErr), qPrintable(strErr));
        QCOMPARE(fx.bank.getInt32(100), 123456);
        QCOMPARE(fx.bank.getInt16(200), int16_t(456));
        QCOMPARE(fx.bank.getFloat(300), 1.25f);
    }

    void axisMovesInt32()
    {
        Fixture fx;
        fx.alignStage.setUnitPowers(3, 4);  // 直线 0.001mm, 角度 0.0001°
        fx.bank.setInt32(10, 1500);
        fx.bank.setInt32(12, -2000);
        fx.bank.setInt32(14, 5000);  // 0.5°
        QString strErr;
        QVERIFY2(runSync(fx, "MoveAbsInt32('D10','D12','D14')\n"
                             "MoveRelativeInt32('D10','D12','D14')\n"
                             "WriteCurrentPosInt32('D20','D22','D24')\n"
                             "WritePoseFloat('D30','D32','D34')\n", strErr), qPrintable(strErr));
        QVERIFY(std::abs(fx.alignStage.axes().dA1 - 3.0) < 1e-9);
        QCOMPARE(fx.bank.getInt32(24), 10000);
        QVERIFY(std::abs(fx.bank.getFloat(32) + 4.0f) < 1e-6f);
    }

    void selectStageAndPick()
    {
        Fixture fx;
        QString strErr;
        QVERIFY2(runSync(fx, "SelectStage('pick')\n"
                             "SpawnProducts(1)\n"
                             "local x, y, t = GetProductPose(1)\n"
                             "MoveTo(x + 0.25, y, t)\n"
                             "local ok, dx, dy, dt = PickAt()\n"
                             "assert(ok, 'pick failed')\n"
                             "SetFloat('D400', dx)\n"
                             "assert(IsHolding())\n"
                             "MoveBy(10, 0, 0)\n"
                             "assert(PlaceAt())\n", strErr), qPrintable(strErr));
        QVERIFY(std::abs(fx.bank.getFloat(400) - 0.25f) < 1e-5f);
        QVERIFY(std::abs(fx.alignStage.axes().dA1) < 1e-12);  // 对位平台未被移动
        QCOMPARE(fx.pickScene.stats().nHits, 1);
    }

    void alignResidualFunctions()
    {
        Fixture fx;
        QString strErr;
        QVERIFY2(runSync(fx, "SetObjectOffset(0.5, 0, 0)\n"
                             "local dx, dy, dt = GetAlignResidual()\n"
                             "MovePoseRelFloat('D0','D2','D4')\n"
                             "SetFloat('D10', dx)\n", strErr), qPrintable(strErr));
        QVERIFY(std::abs(fx.bank.getFloat(10) - 0.5f) < 1e-6f);
    }

    void stopInfiniteLoop()
    {
        Fixture fx;
        QString strErr;
        bool bFinishedOk = true;
        QString strFinishErr;
        connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                [&](const QString&, bool b, const QString& e) { bFinishedOk = b; strFinishErr = e; }, Qt::DirectConnection);
        QVERIFY(fx.engine.start(QStringLiteral("loop"), "while true do end", strErr));
        QVERIFY(fx.engine.isRunning(QStringLiteral("loop")));
        // 20261024 ZJH 同名脚本不能重复启动
        QVERIFY(!fx.engine.start(QStringLiteral("loop"), "x=1", strErr));
        QTest::qWait(100);
        fx.engine.stop(QStringLiteral("loop"));
        QVERIFY(fx.engine.waitForDone(2000));
        QVERIFY(!bFinishedOk);
        QVERIFY(strFinishErr.contains("stopped"));
        QVERIFY(!fx.engine.isRunning(QStringLiteral("loop")));
    }

    void handshakeCalibrationFclb()
    {
        // 20261025 ZJH 模拟视觉软件跑 FCLB 标定: 9 点平移 (步长 1mm) + 2 个旋转点 (±1°), 单位 0.001
        Fixture fx;
        AlignDisplay disp;
        disp.bFixedScale = true;  // 开启固定比例, CSV 才有像素列
        disp.dPxPerMm = 10.0;
        fx.alignScene.setDisplay(disp);
        bool bFinishedOk = false;
        QString strFinishErr;
        connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                [&](const QString&, bool b, const QString& e) { bFinishedOk = b; strFinishErr = e; }, Qt::DirectConnection);
        QString strErr;
        QVERIFY2(fx.engine.start(QStringLiteral("fclb"),
                                 "local hs = require('handshake')\n"
                                 "assert(hs.run{ cmd = 10, stage = 'align', record = true, name = 'test', settleMs = 10 })\n",
                                 strErr), qPrintable(strErr));

        RegisterBank& bank = fx.bank;
        // 20261025 ZJH 等 PLC 触发 FCLB
        QVERIFY(waitFor([&] { return bank.getInt32(100) == 1 && bank.getInt32(102) == 10; }));
        std::vector<std::array<int32_t, 3>> vecPoints;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vecPoints.push_back({ x * 1000, y * 1000, 0 });
            }
        }
        vecPoints.push_back({ 0, 0, 1000 });
        vecPoints.push_back({ 0, 0, -1000 });
        for (const auto& pt : vecPoints) {  // 逐点下发
            bank.setInt32(154, pt[0]);
            bank.setInt32(156, pt[1]);
            bank.setInt32(158, pt[2]);
            bank.setInt32(152, 101);  // 继续移动
            bank.setInt32(150, 1);    // 回复触发
            // 20261025 ZJH 等 PLC 消费回复、移动到位、回写位置并重新触发 "移动完成"
            QVERIFY2(waitFor([&] {
                return bank.getInt32(150) == 0 && bank.getInt32(100) == 1 && bank.getInt32(102) == 101
                    && bank.getInt32(108) == pt[0] && bank.getInt32(110) == pt[1] && bank.getInt32(112) == pt[2];
            }), qPrintable(QStringLiteral("point %1,%2,%3").arg(pt[0]).arg(pt[1]).arg(pt[2])));
        }
        bank.setInt32(152, 10);  // 完成
        bank.setInt32(150, 1);
        QVERIFY(fx.engine.waitForDone(3000));
        QVERIFY2(bFinishedOk, qPrintable(strFinishErr));

        // 20261025 ZJH 检查 CSV: 表头 + 起始点 + 11 个移动点
        const QStringList lstFiles = QDir(fx.logDir.path()).entryList({ QStringLiteral("*.csv") }, QDir::Files);
        QCOMPARE(lstFiles.size(), 1);
        QFile file(fx.logDir.path() + QLatin1Char('/') + lstFiles.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lstLines = QString::fromUtf8(file.readAll()).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lstLines.size(), 1 + 1 + 11);
        QVERIFY(lstLines[0].contains(QStringLiteral("Mark1_屏幕px_X")));
        // 20261025 ZJH 第 1 个移动点 (-1,-1): Mark1 原点 (-20,0) → (-21,-1); 相对相机中心 (0,0), 10px/mm, Y 朝上 → (-210, 10)
        const QStringList lstCells = lstLines[2].split(QLatin1Char(','));
        QCOMPARE(lstCells.value(3).toDouble(), -1000.0);  // 目标1 (寄存器原值)
        QVERIFY(std::abs(lstCells.value(15).toDouble() + 21.0) < 1e-6);   // Mark1_X (前 15 列: 序号..实际位姿θ)
        QVERIFY(std::abs(lstCells.value(18).toDouble() + 210.0) < 1e-6);  // Mark1_屏幕px_X
        QVERIFY(std::abs(lstCells.value(19).toDouble() - 10.0) < 1e-6);   // Mark1_屏幕px_Y
    }

    void handshakeTimeoutAndBadCode()
    {
        Fixture fx;
        bool bFinishedOk = true;
        QString strFinishErr;
        connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                [&](const QString&, bool b, const QString& e) { bFinishedOk = b; strFinishErr = e; }, Qt::DirectConnection);
        QString strErr;
        // 20261025 ZJH 视觉回未知码 → run 返回 false 并给出原因
        QVERIFY(fx.engine.start(QStringLiteral("bad"),
                                "local hs = require('handshake')\n"
                                "local ok, n, msg = hs.run{ cmd = 33 }\n"
                                "SetInt32('D400', ok and 1 or 0)\n"
                                "assert(msg:find('异常码'), msg)\n", strErr));
        QVERIFY(waitFor([&] { return fx.bank.getInt32(100) == 1 && fx.bank.getInt32(102) == 33; }));
        fx.bank.setInt32(152, 999);
        fx.bank.setInt32(150, 1);
        QVERIFY(fx.engine.waitForDone(3000));
        QVERIFY2(bFinishedOk, qPrintable(strFinishErr));
        QCOMPARE(fx.bank.getInt32(400), 0);
        QCOMPARE(fx.bank.getInt32(100), 0);  // 结束时清触发
        // 20261025 ZJH 超时: 视觉不回复
        QVERIFY(fx.engine.start(QStringLiteral("timeout"),
                                "local hs = require('handshake')\n"
                                "local ok, n, msg = hs.run{ cmd = 10, timeoutMs = 200 }\n"
                                "assert(not ok and msg:find('超时'), msg)\n", strErr));
        QVERIFY(fx.engine.waitForDone(3000));
        QVERIFY2(bFinishedOk, qPrintable(strFinishErr));
    }

    void verifyIdealNoError()
    {
        // 20261025 ZJH 无误差 + 理想视觉: 随机来料偏差下残差应≈0, 合格率 100%, 报告含统计行
        Fixture fx;
        QString strErr;
        QVERIFY2(runSync(fx, "local v = require('verify')\n"
                             "local r = v.run{ kind = 'accuracy', count = 5, logEach = false }\n"
                             "assert(r.n == 5 and r.passRate == 100, 'passRate ' .. r.passRate)\n"
                             "SetFloat('D500', r.maxX) SetFloat('D502', r.maxT)\n", strErr, 20000), qPrintable(strErr));
        QVERIFY(fx.bank.getFloat(500) < 1e-3f);   // µm
        QVERIFY(fx.bank.getFloat(502) < 1e-6f);   // °
        QCOMPARE(fx.bank.getFloat(316), 100.0f);  // 合格率寄存器
        const QStringList lstFiles = QDir(fx.logDir.path()).entryList({ QStringLiteral("验证_*.csv") }, QDir::Files);
        QCOMPARE(lstFiles.size(), 1);
        QFile file(fx.logDir.path() + QLatin1Char('/') + lstFiles.first());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString strCsv = QString::fromUtf8(file.readAll());
        QVERIFY(strCsv.contains(QStringLiteral("3σ")));
        QVERIFY(strCsv.contains(QStringLiteral("合格率,100.0%")));
    }

    void verifyIdealWithError()
    {
        // 20261025 ZJH 开误差: 重复定位 σ=2µm + 间隙 3µm + 旋转中心偏差
        Fixture fx;
        StageErrorModel err;
        err.bEnabled = true;
        err.dRepeatUm = 2.0;
        err.dBacklashUm = 3.0;
        err.dRotCenterErrX = 0.5;
        err.dRotCenterErrY = 0.2;
        err.nSeed = 3;
        fx.alignStage.setErrorModel(err);
        QString strErr;
        // 20261025 ZJH 标定完美 (actual): 残差来自随机误差, σ>0 且最大值在合理范围
        QVERIFY2(runSync(fx, "local v = require('verify')\n"
                             "local r = v.run{ kind = 'repeat', count = 20, logEach = false, idealRotCenter = 'actual' }\n"
                             "SetFloat('D500', r.sigmaX) SetFloat('D502', r.maxX)\n", strErr, 20000), qPrintable(strErr));
        QVERIFY2(fx.bank.getFloat(500) > 0.1f, qPrintable(QString::number(fx.bank.getFloat(500))));
        QVERIFY2(fx.bank.getFloat(502) < 20.0f, qPrintable(QString::number(fx.bank.getFloat(502))));
        // 20261025 ZJH 只知道标称旋转中心且只允许 1 次移动: 转 1° 时旋转补偿带出平移误差
        // (R−I)·e ≈ θ·(−ey, ex) = 0.01745 × (−0.2, 0.5) mm ≈ (−3.5, 8.7) µm → Y 超出 ±5µm, 合格率应很低
        QVERIFY2(runSync(fx, "local v = require('verify')\n"
                             "local r = v.run{ kind = 'repeat', count = 5, logEach = false, idealRotCenter = 'nominal',\n"
                             "                 maxIter = 1, offset = { 0, 0, 1.0 } }\n"
                             "SetFloat('D504', r.passRate) SetFloat('D506', r.maxY)\n", strErr, 20000), qPrintable(strErr));
        QVERIFY2(fx.bank.getFloat(504) < 50.0f, qPrintable(QString::number(fx.bank.getFloat(504))));
        QVERIFY2(fx.bank.getFloat(506) > 5.0f, qPrintable(QString::number(fx.bank.getFloat(506))));
    }

    void motionTiming()
    {
        // 20261025 ZJH 速度 100mm/s 走 10mm + 整定 50ms → 至少约 150ms; 插补结束停在目标 (无随机误差)
        Fixture fx;
        StageErrorModel err;
        err.bEnabled = true;
        err.dSpeedMmPerS = 100.0;
        err.nSettleMs = 50;
        fx.alignStage.setErrorModel(err);
        QString strErr;
        QElapsedTimer timer;
        timer.start();
        QVERIFY2(runSync(fx, "MoveTo(10, 0, 0)", strErr), qPrintable(strErr));
        QVERIFY2(timer.elapsed() >= 140, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(std::abs(fx.alignStage.actualAxes().dA1 - 10.0) < 1e-12);
        // 20261025 ZJH 关闭速度后瞬间到位
        err.dSpeedMmPerS = 0.0;
        err.nSettleMs = 0;
        fx.alignStage.setErrorModel(err);
        timer.restart();
        QVERIFY2(runSync(fx, "MoveTo(0, 0, 0)", strErr), qPrintable(strErr));
        QVERIFY(timer.elapsed() < 100);
    }

    void requireMissingModule()
    {
        Fixture fx;
        QString strErr;
        QVERIFY(!runSync(fx, "require('no_such_module')", strErr));
        QVERIFY(strErr.contains("no_such_module"));
    }

    void errors()
    {
        Fixture fx;
        QString strErr;
        QVERIFY(!LuaEngine::checkSyntax("if then", strErr));
        QVERIFY(!strErr.isEmpty());
        QVERIFY(!runSync(fx, "SetInt16('X100', 1)", strErr));
        QVERIFY(strErr.contains("invalid register address"));
        QVERIFY(!runSync(fx, "SelectStage('foo')", strErr));
        QVERIFY(LuaEngine::functionNames().contains("MovePoseAbsInt32"));
    }
};

QTEST_MAIN(TestLua)
#include "test_lua.moc"
