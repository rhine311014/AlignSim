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

    // 20261126 ZJH 读入 观微_握手.lua, 前面拼上全局变量设置 (基址 100, 超时和等心跳时间调短, CSV 写到临时目录)
    // nWaitSec: 等心跳最多多少秒 (GW_WAIT); 读不到脚本返回空串, 由调用方 QVERIFY 报错
    static QString guanweiScript(const Fixture& fx, int nCount, int nTimeoutMs, double dWaitSec = 30.0)
    {
        QFile fileScript(QString::fromUtf8(ALIGNSIM_SCRIPTS_DIR) + QStringLiteral("/观微_握手.lua"));
        if (!fileScript.open(QIODevice::ReadOnly)) {
            return QString();  // 读不到脚本
        }
        return QStringLiteral("GW_BASE = 100 GW_COUNT = %1 GW_TIMEOUT = %2 GW_WAIT = %3 GW_CSV = '%4/gw.csv'\n")
                   .arg(nCount).arg(nTimeoutMs).arg(dWaitSec).arg(fx.logDir.path())
               + QString::fromUtf8(fileScript.readAll());  // 返回完整脚本文本
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

    void setIntRejectsOverflow()
    {
        // 20261126 ZJH 回归: SetInt16 / SetInt32 原先对超范围的值静默截位 (70000 落成 4464), 现在必须报错且不写寄存器
        Fixture fx;
        QString strErr;
        // 20261126 ZJH 边界值照常能写
        QVERIFY2(runSync(fx, "SetInt16('D100', 32767) SetInt16('D101', -32768)\n"
                             "SetInt32('D110', 2147483647) SetInt32('D112', -2147483648)\n", strErr), qPrintable(strErr));
        QCOMPARE(fx.bank.getInt16(100), int16_t(32767));
        QCOMPARE(fx.bank.getInt16(101), int16_t(-32768));
        QCOMPARE(fx.bank.getInt32(110), int32_t(2147483647));
        QCOMPARE(fx.bank.getInt32(112), int32_t(-2147483647 - 1));
        // 20261126 ZJH 上溢 / 下溢: 每条都应报错 (用 pcall 接住, 一个脚本里跑完), 报错信息带上函数名和原值
        fx.bank.setInt16(120, 7);
        fx.bank.setInt32(130, 9);
        QVERIFY2(runSync(fx, "local function bad(f, addr, v, key)\n"
                             "    local ok, err = pcall(f, addr, v)\n"
                             "    assert(not ok, 'no error for ' .. tostring(v))\n"
                             "    assert(tostring(err):find(key, 1, true), tostring(err))\n"
                             "    assert(tostring(err):find(tostring(v), 1, true), tostring(err))\n"
                             "end\n"
                             "bad(SetInt16, 'D120', 70000, 'SetInt16 value out of range')\n"
                             "bad(SetInt16, 'D120', 32768, 'SetInt16 value out of range')\n"
                             "bad(SetInt16, 'D120', -32769, 'SetInt16 value out of range')\n"
                             "bad(SetInt16, 'D120', 65535, 'SetInt16 value out of range')\n"
                             "bad(SetInt32, 'D130', 2147483648, 'SetInt32 value out of range')\n"
                             "bad(SetInt32, 'D130', -2147483649, 'SetInt32 value out of range')\n"
                             "bad(SetInt32, 'D130', 4294967295, 'SetInt32 value out of range')\n", strErr), qPrintable(strErr));
        // 20261126 ZJH 报错的那些写入都没落到寄存器上
        QCOMPARE(fx.bank.getInt16(120), int16_t(7));
        QCOMPARE(fx.bank.getInt32(130), int32_t(9));
        // 20261126 ZJH 不接 pcall 时脚本以失败结束, 错误信息传到 scriptFinished
        QVERIFY(!runSync(fx, "SetInt16('D120', 70000)", strErr));
        QVERIFY2(strErr.contains("SetInt16 value out of range"), qPrintable(strErr));
    }

    void guanweiHandshakeNormal()
    {
        // 20261126 ZJH 观微_握手 正常路径: 测试线程扮演观微 (写心跳、受理、回复), 开始前故意留下脏寄存器
        Fixture fx;
        RegisterBank& bank = fx.bank;
        bank.setInt16(100, 1);       // 上一轮留下的触发
        bank.setInt16(150, 1);       // 上一轮留下的迟到回复
        bank.setInt32(106, 5555);    // 别的脚本留下的参数 2 (观微当腔体号, 超出 0~999)
        bank.setInt16(108, 77);      // 别的脚本留下的序列号
        bank.setInt16(117, 88);
        bool bFinishedOk = false;
        QString strFinishErr;
        connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                [&](const QString&, bool b, const QString& e) { bFinishedOk = b; strFinishErr = e; }, Qt::DirectConnection);
        const QString strCode = guanweiScript(fx, 3, 2000);
        QVERIFY(!strCode.isEmpty());
        QString strErr;
        QVERIFY2(fx.engine.start(QStringLiteral("gw"), strCode, strErr), qPrintable(strErr));
        // 20261126 ZJH 脚本开头应清掉 +0 / +50 / +8~+17 (此时假观微还没写心跳, 脚本停在等心跳)
        QVERIFY(waitFor([&] { return bank.getInt16(100) == 0 && bank.getInt16(150) == 0
                                  && bank.getInt16(108) == 0 && bank.getInt16(117) == 0; }));
        int nParam2Bad = 0;  // 受理时看到参数 2 不是 0 的次数
        int nAccepted = 0;   // 受理的指令条数
        QElapsedTimer timer;
        timer.start();
        while (fx.engine.isRunning(QStringLiteral("gw")) && timer.elapsed() < 15000) {
            bank.setInt16(149, static_cast<int16_t>(timer.elapsed() / 50 % 30000));  // 心跳计数在涨
            bank.setInt16(148, 1);                                                  // 视觉状态: 运行
            if (bank.getInt16(100) == 1) {  // 有指令: 按观微的顺序受理并回复
                const int nCode = bank.getInt16(102);
                const int nParam1 = bank.getInt32(104);
                if (bank.getInt32(106) != 0) {
                    ++nParam2Bad;
                }
                bank.setInt16(150, 0);  // 受理: 先清 +50
                bank.setInt16(100, 0);  // 再清 +0
                int nResult = -9;       // 不认识的指令
                if (nCode == 1 || nCode == 6) {
                    nResult = 1;
                } else if (nCode == 5) {
                    nResult = (nParam1 == 99999) ? -2 : 1;
                } else if (nCode == 10) {
                    nResult = (nParam1 == 0) ? -2 : 1;
                }
                bank.setInt32(156, nCode == 6 ? 1 : nParam1);  // 回复数据: 查品种回品种号, 检测回拍照号
                bank.setInt16(152, static_cast<int16_t>(nCode));
                bank.setInt16(154, static_cast<int16_t>(nResult));
                bank.setInt16(150, 1);
                ++nAccepted;
            }
            QThread::msleep(2);
        }
        QVERIFY(fx.engine.waitForDone(3000));
        QVERIFY2(bFinishedOk, qPrintable(strFinishErr));
        QCOMPARE(nAccepted, 6 + 3);  // 6 条别的指令 + 3 次检测
        QCOMPARE(nParam2Bad, 0);     // 每条都把参数 2 写成 0
        QCOMPARE(bank.getInt16(100), int16_t(0));
        QCOMPARE(bank.getInt16(150), int16_t(0));  // 回复都被取走
        QFile fileCsv(fx.logDir.path() + QStringLiteral("/gw.csv"));
        QVERIFY(fileCsv.open(QIODevice::ReadOnly));
        const QString strCsv = QString::fromUtf8(fileCsv.readAll());
        QVERIFY2(strCsv.contains(QStringLiteral("检测 3 次，OK 3，NG 0，负数码 0，回复不对或超时 0，迟到回复 0")), qPrintable(strCsv));
    }

    void guanweiHandshakeNotAccepted()
    {
        // 20261126 ZJH 回归: 视觉有心跳但不受理 → 原先超时后 +0 留着 1、而且 50 条全部等满超时;
        // 现在每条超时都收回触发, 连续 5 条没受理就提前结束
        Fixture fx;
        RegisterBank& bank = fx.bank;
        bool bFinishedOk = false;
        QString strFinishErr;
        connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                [&](const QString&, bool b, const QString& e) { bFinishedOk = b; strFinishErr = e; }, Qt::DirectConnection);
        const QString strCode = guanweiScript(fx, 50, 150);
        QVERIFY(!strCode.isEmpty());
        QString strErr;
        QVERIFY2(fx.engine.start(QStringLiteral("gw"), strCode, strErr), qPrintable(strErr));
        bool bSawTrigger = false;  // 是否看到过 +0 = 1 (脚本确实发过指令)
        QElapsedTimer timer;
        timer.start();
        while (fx.engine.isRunning(QStringLiteral("gw")) && timer.elapsed() < 15000) {
            bank.setInt16(149, static_cast<int16_t>(timer.elapsed() / 50 % 30000));  // 只写心跳, 不受理
            bSawTrigger = bSawTrigger || bank.getInt16(100) == 1;
            QThread::msleep(2);
        }
        QVERIFY(fx.engine.waitForDone(3000));
        QVERIFY2(bFinishedOk, qPrintable(strFinishErr));
        QVERIFY(bSawTrigger);
        QCOMPARE(bank.getInt16(100), int16_t(0));  // 触发已收回, 观微重连后不会执行没人等的指令
        QFile fileCsv(fx.logDir.path() + QStringLiteral("/gw.csv"));
        QVERIFY(fileCsv.open(QIODevice::ReadOnly));
        const QString strCsv = QString::fromUtf8(fileCsv.readAll());
        QVERIFY2(strCsv.contains(QStringLiteral("提前结束")), qPrintable(strCsv));
        // 20261126 ZJH 只发了 5 条就放弃 (没有把 6 + 50 条都等满): CSV 里数据行 (不以 # 开头, 去掉表头) 正好 5 行
        int nRows = 0;  // 数据行数
        const QStringList lstLines = strCsv.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& strLine : lstLines) {
            if (!strLine.startsWith(QLatin1Char('#'))) {
                ++nRows;
            }
        }
        QCOMPARE(nRows, 1 + 5);  // 表头 + 5 条
    }

    void guanweiHandshakeNoHeartbeat()
    {
        // 20261126 ZJH 回归: 心跳一直不动 → 原先等满后照样往下发指令; 现在记日志后直接结束, 一条指令都不发
        Fixture fx;
        RegisterBank& bank = fx.bank;
        bool bFinishedOk = false;
        QString strFinishErr;
        connect(&fx.engine, &LuaEngine::scriptFinished, &fx.engine,
                [&](const QString&, bool b, const QString& e) { bFinishedOk = b; strFinishErr = e; }, Qt::DirectConnection);
        const QString strCode = guanweiScript(fx, 50, 150, 0.3);  // 等心跳只等 0.3 秒
        QVERIFY(!strCode.isEmpty());
        QString strErr;
        QVERIFY2(fx.engine.start(QStringLiteral("gw"), strCode, strErr), qPrintable(strErr));
        bool bSawTrigger = false;  // 是否看到过 +0 = 1
        QElapsedTimer timer;
        timer.start();
        while (fx.engine.isRunning(QStringLiteral("gw")) && timer.elapsed() < 5000) {
            bSawTrigger = bSawTrigger || bank.getInt16(100) == 1;
            QThread::msleep(2);
        }
        QVERIFY(fx.engine.waitForDone(3000));
        QVERIFY2(bFinishedOk, qPrintable(strFinishErr));
        QVERIFY(!bSawTrigger);                // 没发过指令
        QVERIFY(timer.elapsed() < 3000);      // 没有去等后面每条的超时
        QFile fileCsv(fx.logDir.path() + QStringLiteral("/gw.csv"));
        QVERIFY(fileCsv.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(fileCsv.readAll()).contains(QStringLiteral("心跳")));
    }

    void doubleWordAddrLimit()
    {
        // 20261126 ZJH 回归: 双字 / 浮点接口原先放行地址 65535 (RegisterBank 读回 0、写被忽略), 现在要求 ≤ 65534
        Fixture fx;
        QString strErr;
        // 20261126 ZJH 65534 是双字能用的最后一个地址; 单字接口 65535 仍然可用
        QVERIFY2(runSync(fx, "SetInt32('D65534', 123456)\n"
                             "assert(GetInt32(65534) == 123456)\n"
                             "SetFloat(65534, 1.5)\n"
                             "assert(GetFloat('D65534') == 1.5)\n"
                             "SetInt16('D65535', 5)\n"
                             "assert(GetInt16(65535) == 5)\n", strErr), qPrintable(strErr));
        // 20261126 ZJH 65535 的双字 / 浮点读写、以及三地址的平台接口: 一律报错 (用 pcall 接住, 一个脚本里跑完)
        QVERIFY2(runSync(fx, "local function bad(name, f, ...)\n"
                             "    local ok, err = pcall(f, ...)\n"
                             "    assert(not ok, 'no error for ' .. name)\n"
                             "    assert(tostring(err):find('out of range for 32-bit access', 1, true), name .. ': ' .. tostring(err))\n"
                             "end\n"
                             "bad('SetInt32 str', SetInt32, 'D65535', 1)\n"
                             "bad('SetInt32 int', SetInt32, 65535, 1)\n"
                             "bad('GetInt32', GetInt32, 'D65535')\n"
                             "bad('SetFloat', SetFloat, 'DM65535', 1.0)\n"
                             "bad('GetFloat', GetFloat, 65535)\n"
                             "bad('WriteCurrentPosInt32', WriteCurrentPosInt32, 'D10', 'D12', 'D65535')\n"
                             "bad('WritePoseFloat', WritePoseFloat, 'D65535', 'D12', 'D14')\n"
                             "bad('MoveAbsInt32', MoveAbsInt32, 'D65535', 'D12', 'D14')\n"
                             "bad('MovePoseAbsFloat', MovePoseAbsFloat, 'D10', 'D65535', 'D14')\n", strErr), qPrintable(strErr));
        // 20261126 ZJH 不接 pcall 时脚本以失败结束
        QVERIFY(!runSync(fx, "SetInt32('D65535', 1)", strErr));
        QVERIFY2(strErr.contains("out of range for 32-bit access"), qPrintable(strErr));
    }
};

QTEST_MAIN(TestLua)
#include "test_lua.moc"
