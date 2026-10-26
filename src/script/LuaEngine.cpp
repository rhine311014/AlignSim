// 20261024 ZJH LuaEngine 实现
// 注意: luaL_error 通过 longjmp 跳出, 不会调用 C++ 局部对象的析构函数
//       所以各包装函数里, 在可能报错的调用点之前不持有 QString/vector 等非平凡对象
#include "script/LuaEngine.h"

#include "core/RegisterBank.h"
#include "motion/Stage.h"
#include "sim/AlignScene.h"
#include "sim/CalibRecorder.h"
#include "sim/PickScene.h"

#include "lua.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {

const char* s_pszTaskKey = "alignsim.task";  // 注册表中保存任务状态指针的键

// 20261024 ZJH 单个脚本运行时状态 (存放在工作线程栈上, 通过注册表供包装函数取用)
struct TaskState {
    LuaContext ctx;                          // 可访问对象
    std::atomic_bool* pStop = nullptr;       // 停止标志
    Stage* pCurrentStage = nullptr;          // SelectStage 选中的平台
    LuaEngine* pEngine = nullptr;            // 引擎 (发日志)
    QString strName;                         // 脚本名
    CalibRecorder recorder;                  // 本脚本的标定记录 (脚本结束时析构自动关闭)
    CalibRecorder report;                    // 本脚本的通用 CSV 报告 (CsvBegin/CsvRow/CsvEnd)
};

// 20261024 ZJH 从注册表取任务状态
TaskState* taskOf(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, s_pszTaskKey);
    auto* pTask = static_cast<TaskState*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return pTask;  // 返回任务状态 (正常运行时不为空)
}

// 20261024 ZJH 解析寄存器地址参数: 整数 100, 或字符串 "D100" / "DM100" / "100"; 非法时 luaL_error
int checkAddr(lua_State* L, int nArg)
{
    // 20261024 ZJH 整数形式
    if (lua_type(L, nArg) == LUA_TNUMBER) {
        const lua_Integer nAddr = luaL_checkinteger(L, nArg);
        if (nAddr < 0 || nAddr >= RegisterBank::s_nRegisterCount) {
            luaL_error(L, "register address out of range: %d", static_cast<int>(nAddr));
        }
        return static_cast<int>(nAddr);  // 返回地址
    }
    const char* pszAddr = luaL_checkstring(L, nArg);  // 字符串形式
    const char* p = pszAddr;
    // 20261024 ZJH 跳过前缀 DM / D (大小写均可)
    if ((p[0] == 'D' || p[0] == 'd') && (p[1] == 'M' || p[1] == 'm')) {
        p += 2;
    } else if (p[0] == 'D' || p[0] == 'd') {
        p += 1;
    }
    // 20261024 ZJH 剩余部分必须是 1~6 位数字
    const size_t nDigits = std::strlen(p);
    bool bOk = nDigits > 0 && nDigits <= 6;
    for (size_t i = 0; bOk && i < nDigits; ++i) {
        bOk = (p[i] >= '0' && p[i] <= '9');
    }
    const long nAddr = bOk ? std::strtol(p, nullptr, 10) : -1;
    if (!bOk || nAddr >= RegisterBank::s_nRegisterCount) {
        luaL_error(L, "invalid register address: %s", pszAddr);
    }
    return static_cast<int>(nAddr);  // 返回地址
}

// 20261024 ZJH 取当前平台, 没有时报错
Stage* checkStage(lua_State* L)
{
    TaskState* pTask = taskOf(L);
    if (!pTask || !pTask->pCurrentStage) {
        luaL_error(L, "no stage selected");
    }
    return pTask->pCurrentStage;  // 返回当前平台
}

// ---------------- 寄存器 ----------------
int luaSetInt16(lua_State* L)
{
    const int nAddr = checkAddr(L, 1);
    const lua_Integer nValue = luaL_checkinteger(L, 2);
    taskOf(L)->ctx.pBank->setInt16(nAddr, static_cast<int16_t>(nValue));  // 截断为 16 位
    return 0;
}

int luaSetInt32(lua_State* L)
{
    const int nAddr = checkAddr(L, 1);
    const lua_Integer nValue = luaL_checkinteger(L, 2);
    taskOf(L)->ctx.pBank->setInt32(nAddr, static_cast<int32_t>(nValue));
    return 0;
}

int luaSetFloat(lua_State* L)
{
    const int nAddr = checkAddr(L, 1);
    const lua_Number dValue = luaL_checknumber(L, 2);
    taskOf(L)->ctx.pBank->setFloat(nAddr, static_cast<float>(dValue));
    return 0;
}

int luaGetInt16(lua_State* L)
{
    const int nAddr = checkAddr(L, 1);
    lua_pushinteger(L, taskOf(L)->ctx.pBank->getInt16(nAddr));
    return 1;  // 返回 1 个值
}

int luaGetInt32(lua_State* L)
{
    const int nAddr = checkAddr(L, 1);
    lua_pushinteger(L, taskOf(L)->ctx.pBank->getInt32(nAddr));
    return 1;
}

int luaGetFloat(lua_State* L)
{
    const int nAddr = checkAddr(L, 1);
    lua_pushnumber(L, taskOf(L)->ctx.pBank->getFloat(nAddr));
    return 1;
}

// ---------------- 流程 ----------------
int luaSleep(lua_State* L)
{
    const lua_Integer nMs = luaL_checkinteger(L, 1);
    std::atomic_bool* pStop = taskOf(L)->pStop;
    QElapsedTimer timer;
    timer.start();
    // 20261024 ZJH 分 10ms 小片睡眠, 期间可被停止打断
    while (timer.elapsed() < nMs) {
        if (pStop->load()) {
            return luaL_error(L, "script stopped");
        }
        QThread::msleep(static_cast<unsigned long>(std::min<qint64>(10, nMs - timer.elapsed() + 1)));
    }
    return 0;
}

int luaIsLoopValid(lua_State* L)
{
    lua_pushboolean(L, !taskOf(L)->pStop->load());  // 未请求停止即有效
    return 1;
}

// 20261026 ZJH 单调毫秒计时 (不受系统改时间影响), 脚本做超时判断用: 按真实耗时而不是 sleep 次数累加
int luaTickMs(lua_State* L)
{
    const auto nMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    lua_pushinteger(L, static_cast<lua_Integer>(nMs));
    return 1;  // 返回毫秒数
}

int luaLog(lua_State* L)
{
    const int nArgs = lua_gettop(L);
    luaL_Buffer buf;  // Lua 侧拼接, 避免 C++ 对象跨 longjmp
    luaL_buffinit(L, &buf);
    for (int i = 1; i <= nArgs; ++i) {  // 逐个参数 tostring 后用空格连接
        if (i > 1) {
            luaL_addchar(&buf, ' ');
        }
        luaL_tolstring(L, i, nullptr);
        luaL_addvalue(&buf);
    }
    luaL_pushresult(&buf);
    TaskState* pTask = taskOf(L);
    const char* pszMsg = lua_tostring(L, -1);
    // 20261024 ZJH 此后不再调用可能报错的 Lua API, 可以安全构造 QString
    pTask->pEngine->emitLog(QStringLiteral("[%1] %2").arg(pTask->strName, QString::fromUtf8(pszMsg)));
    return 0;
}

// ---------------- 平台 ----------------
int luaSelectStage(lua_State* L)
{
    const char* pszName = luaL_checkstring(L, 1);
    TaskState* pTask = taskOf(L);
    // 20261024 ZJH 按名称切换
    if (std::strcmp(pszName, "align") == 0) {
        pTask->pCurrentStage = pTask->ctx.pAlignStage;
    } else if (std::strcmp(pszName, "pick") == 0) {
        pTask->pCurrentStage = pTask->ctx.pPickStage;
    } else {
        return luaL_error(L, "unknown stage '%s' (use \"align\" or \"pick\")", pszName);
    }
    return 0;
}

// 20261024 ZJH 从三个寄存器读轴值; bInt32=true 时按各轴倍率换算, 否则读浮点
AxisValues readAxesFromRegs(lua_State* L, Stage* pStage, bool bInt32)
{
    const int arrAddr[3] = { checkAddr(L, 1), checkAddr(L, 2), checkAddr(L, 3) };
    RegisterBank* pBank = taskOf(L)->ctx.pBank;
    double arrVal[3] = {};
    for (int i = 0; i < 3; ++i) {  // 逐轴读取
        arrVal[i] = bInt32 ? pBank->getInt32(arrAddr[i]) / pStage->int32Divisor(i) : pBank->getFloat(arrAddr[i]);
    }
    return AxisValues{ arrVal[0], arrVal[1], arrVal[2] };  // 返回轴值
}

// 20261024 ZJH 从三个寄存器读位姿 (X/Y 用直线倍率, θ 用角度倍率)
Pose readPoseFromRegs(lua_State* L, Stage* pStage, bool bInt32)
{
    const int arrAddr[3] = { checkAddr(L, 1), checkAddr(L, 2), checkAddr(L, 3) };
    RegisterBank* pBank = taskOf(L)->ctx.pBank;
    // 20261024 ZJH Int32: 实际值 = 寄存器 / 10^n
    if (bInt32) {
        const double dLin = std::pow(10.0, pStage->linearPower());
        const double dAng = std::pow(10.0, pStage->anglePower());
        return Pose{ pBank->getInt32(arrAddr[0]) / dLin, pBank->getInt32(arrAddr[1]) / dLin,
                     pBank->getInt32(arrAddr[2]) / dAng };
    }
    return Pose{ pBank->getFloat(arrAddr[0]), pBank->getFloat(arrAddr[1]), pBank->getFloat(arrAddr[2]) };
}

// 20261024 ZJH 三个值写回寄存器; arrDivisor 为 Int32 倍率 (浮点时忽略)
void writeTriple(lua_State* L, const double arrVal[3], const double arrDivisor[3], bool bInt32)
{
    const int arrAddr[3] = { checkAddr(L, 1), checkAddr(L, 2), checkAddr(L, 3) };
    RegisterBank* pBank = taskOf(L)->ctx.pBank;
    for (int i = 0; i < 3; ++i) {  // 逐个写入
        if (bInt32) {
            pBank->setInt32(arrAddr[i], static_cast<int32_t>(std::llround(arrVal[i] * arrDivisor[i])));  // 四舍五入
        } else {
            pBank->setFloat(arrAddr[i], static_cast<float>(arrVal[i]));
        }
    }
}

// 20261025 ZJH 可被停止打断的睡眠 (只用平凡局部变量, 可安全 luaL_error)
void sleepInterruptible(lua_State* L, double dMs)
{
    std::atomic_bool* pStop = taskOf(L)->pStop;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < dMs) {
        if (pStop->load()) {
            luaL_error(L, "script stopped");
        }
        QThread::msleep(static_cast<unsigned long>(std::clamp<double>(dMs - static_cast<double>(timer.elapsed()), 1.0, 10.0)));
    }
}

// 20261025 ZJH 带运动耗时的移动 (误差模型开启且设置了速度时插补移动, 到位后等整定时间; 否则瞬间到位)
// 插补步长约 20ms, 视觉若在运动中拍照会看到中间位置 —— 与真机一致
void moveTimed(lua_State* L, Stage* pStage, const AxisValues& target)
{
    const StageErrorModel err = pStage->errorModel();
    const AxisValues start = pStage->axes();
    const double arrDelta[3] = { target.dA1 - start.dA1, target.dA2 - start.dA2, target.dA3 - start.dA3 };
    double dDurationMs = 0.0;  // 运动时长 = 各轴 行程/速度 的最大值
    if (err.bEnabled) {
        for (int i = 0; i < 3; ++i) {
            const double dSpeed = pStage->isAngleAxis(i) ? err.dSpeedDegPerS : err.dSpeedMmPerS;
            if (dSpeed > 0.0) {
                dDurationMs = std::max(dDurationMs, std::abs(arrDelta[i]) / dSpeed * 1000.0);
            }
        }
    }
    const int nSteps = std::max(1, static_cast<int>(std::ceil(dDurationMs / 20.0)));  // 插补步数
    for (int k = 1; k <= nSteps; ++k) {  // 逐步插补 (nSteps=1 即瞬间到位)
        const double dFrac = static_cast<double>(k) / nSteps;
        pStage->moveAxesAbs(AxisValues{ start.dA1 + arrDelta[0] * dFrac, start.dA2 + arrDelta[1] * dFrac,
                                        start.dA3 + arrDelta[2] * dFrac });
        if (nSteps > 1) {
            sleepInterruptible(L, dDurationMs / nSteps);
        }
    }
    // 20261025 ZJH 整定时间
    if (err.bEnabled && err.nSettleMs > 0) {
        sleepInterruptible(L, err.nSettleMs);
    }
}

// 20261025 ZJH 位姿相对量 → 指令轴目标 (叠加在当前指令位姿上)
AxisValues targetForPoseRel(Stage* pStage, const Pose& delta)
{
    const Pose poseCmd = pStage->commandedPose();
    return pStage->axesForPose(Pose{ poseCmd.dX + delta.dX, poseCmd.dY + delta.dY, poseCmd.dThetaDeg + delta.dThetaDeg });
}

// 20261025 ZJH 轴相对量 → 指令轴目标
AxisValues targetForAxesRel(Stage* pStage, const AxisValues& delta)
{
    const AxisValues now = pStage->axes();
    return AxisValues{ now.dA1 + delta.dA1, now.dA2 + delta.dA2, now.dA3 + delta.dA3 };
}

int luaMoveAbsInt32(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, readAxesFromRegs(L, p, true)); return 0; }
int luaMoveAbsFloat(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, readAxesFromRegs(L, p, false)); return 0; }
int luaMoveRelInt32(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, targetForAxesRel(p, readAxesFromRegs(L, p, true))); return 0; }
int luaMoveRelFloat(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, targetForAxesRel(p, readAxesFromRegs(L, p, false))); return 0; }
int luaMovePoseAbsInt32(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, p->axesForPose(readPoseFromRegs(L, p, true))); return 0; }
int luaMovePoseAbsFloat(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, p->axesForPose(readPoseFromRegs(L, p, false))); return 0; }
int luaMovePoseRelInt32(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, targetForPoseRel(p, readPoseFromRegs(L, p, true))); return 0; }
int luaMovePoseRelFloat(lua_State* L) { Stage* p = checkStage(L); moveTimed(L, p, targetForPoseRel(p, readPoseFromRegs(L, p, false))); return 0; }

int luaMovePoseBy(lua_State* L)
{
    Stage* pStage = checkStage(L);
    const Pose delta{ luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3) };
    moveTimed(L, pStage, targetForPoseRel(pStage, delta));
    return 0;
}

// 20261024 ZJH 写当前轴值
int writeAxes(lua_State* L, bool bInt32)
{
    Stage* pStage = checkStage(L);
    const AxisValues axes = pStage->axes();
    const double arrVal[3] = { axes.dA1, axes.dA2, axes.dA3 };
    const double arrDiv[3] = { pStage->int32Divisor(0), pStage->int32Divisor(1), pStage->int32Divisor(2) };
    writeTriple(L, arrVal, arrDiv, bInt32);
    return 0;
}
int luaWriteCurrentPosInt32(lua_State* L) { return writeAxes(L, true); }
int luaWriteCurrentPosFloat(lua_State* L) { return writeAxes(L, false); }

// 20261024 ZJH 写当前位姿
int writePose(lua_State* L, bool bInt32)
{
    Stage* pStage = checkStage(L);
    const Pose pose = pStage->commandedPose();  // PLC 回写指令位姿 (视觉看不到实际误差)
    const double arrVal[3] = { pose.dX, pose.dY, pose.dThetaDeg };
    const double dLin = std::pow(10.0, pStage->linearPower());
    const double dAng = std::pow(10.0, pStage->anglePower());
    const double arrDiv[3] = { dLin, dLin, dAng };
    writeTriple(L, arrVal, arrDiv, bInt32);
    return 0;
}
int luaWritePoseInt32(lua_State* L) { return writePose(L, true); }
int luaWritePoseFloat(lua_State* L) { return writePose(L, false); }

int luaMoveTo(lua_State* L)
{
    Stage* pStage = checkStage(L);
    moveTimed(L, pStage, AxisValues{ luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3) });
    return 0;
}

int luaMoveBy(lua_State* L)
{
    Stage* pStage = checkStage(L);
    moveTimed(L, pStage, targetForAxesRel(pStage, AxisValues{ luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3) }));
    return 0;
}

int luaHome(lua_State* L)
{
    moveTimed(L, checkStage(L), AxisValues{});
    return 0;
}

// 20261025 ZJH 旋转中心当前世界坐标; 参数 "nominal" (默认, PLC/视觉以为的) | "actual" (真值)
int luaGetRotCenter(lua_State* L)
{
    const char* pszWhich = luaL_optstring(L, 1, "nominal");
    Stage* pStage = checkStage(L);
    QPointF ptCenter;
    // 20261025 ZJH 标称: 标称中心 + 指令平移 (绕自身旋转不移动); 实际: 实际中心随实际运动
    if (std::strcmp(pszWhich, "actual") == 0) {
        ptCenter = pStage->mapFromHome(pStage->actualRotCenter());
    } else {
        const Pose poseCmd = pStage->commandedPose();
        ptCenter = pStage->rotCenter() + QPointF(poseCmd.dX, poseCmd.dY);
    }
    lua_pushnumber(L, ptCenter.x());
    lua_pushnumber(L, ptCenter.y());
    return 2;  // 返回 x, y
}

int luaGetAxes(lua_State* L)
{
    const AxisValues axes = checkStage(L)->axes();
    lua_pushnumber(L, axes.dA1);
    lua_pushnumber(L, axes.dA2);
    lua_pushnumber(L, axes.dA3);
    return 3;  // 返回三轴值
}

int luaGetPose(lua_State* L)
{
    const Pose pose = checkStage(L)->commandedPose();  // 指令位姿
    lua_pushnumber(L, pose.dX);
    lua_pushnumber(L, pose.dY);
    lua_pushnumber(L, pose.dThetaDeg);
    return 3;  // 返回 x y θ
}

int luaGetActualPose(lua_State* L)
{
    const Pose pose = checkStage(L)->pose();  // 实际位姿 (真值, 含误差注入)
    lua_pushnumber(L, pose.dX);
    lua_pushnumber(L, pose.dY);
    lua_pushnumber(L, pose.dThetaDeg);
    return 3;  // 返回实际 x y θ
}

// ---------------- 对位 ----------------
int luaGetAlignResidual(lua_State* L)
{
    const AlignResidual res = taskOf(L)->ctx.pAlignScene->residual();
    lua_pushnumber(L, res.dX);
    lua_pushnumber(L, res.dY);
    lua_pushnumber(L, res.dThetaDeg);
    return 3;  // 返回整体残差
}

// 20261025 ZJH 对象 Mark 实际质心与目标 Mark 质心 (模拟"理想视觉"测到的位置)
int luaGetAlignCentroids(lua_State* L)
{
    AlignScene* pScene = taskOf(L)->ctx.pAlignScene;
    double dOx = 0.0, dOy = 0.0, dTx = 0.0, dTy = 0.0;  // 累加和
    int nCount = 0;
    {
        const std::vector<MarkPair> vecMarks = pScene->marks();
        nCount = static_cast<int>(vecMarks.size());
        for (int i = 0; i < nCount; ++i) {  // 累加两组坐标
            const MarkWorldPose obj = pScene->objectMarkWorld(i);
            dOx += obj.ptPos.x();
            dOy += obj.ptPos.y();
            dTx += vecMarks[i].dTargetX;
            dTy += vecMarks[i].dTargetY;
        }
    }
    // 20261025 ZJH 无 Mark 时返回 4 个 0
    const double dDiv = nCount > 0 ? nCount : 1.0;
    lua_pushnumber(L, dOx / dDiv);
    lua_pushnumber(L, dOy / dDiv);
    lua_pushnumber(L, dTx / dDiv);
    lua_pushnumber(L, dTy / dDiv);
    return 4;  // 返回 对象质心 x,y, 目标质心 x,y
}

// ---------------- 通用 CSV 报告 ----------------
// 20261025 ZJH 把 Lua 表 (数组) 的每个元素转成字符串, 用 \x1F 连接后压栈 (纯 Lua API, 不持有 Qt 对象)
void pushJoinedTable(lua_State* L, int nArg)
{
    luaL_checktype(L, nArg, LUA_TTABLE);
    const lua_Integer nLen = luaL_len(L, nArg);
    luaL_Buffer buf;
    luaL_buffinit(L, &buf);
    for (lua_Integer i = 1; i <= nLen; ++i) {  // 逐元素 tostring
        if (i > 1) {
            luaL_addchar(&buf, '\x1F');
        }
        lua_geti(L, nArg, i);
        luaL_tolstring(L, -1, nullptr);
        lua_remove(L, -2);   // 移除原值, 保留字符串
        luaL_addvalue(&buf);
    }
    luaL_pushresult(&buf);
}

int luaCsvBegin(lua_State* L)
{
    const char* pszName = luaL_checkstring(L, 1);
    pushJoinedTable(L, 2);                     // 表头
    const char* pszHeader = lua_tostring(L, -1);
    TaskState* pTask = taskOf(L);
    bool bOk = false;
    QByteArray baResult;
    {
        QString strErr;
        const QStringList lstHeader = QString::fromUtf8(pszHeader).split(QChar(0x1F));
        bOk = pTask->report.begin(pTask->ctx.strLogDir, QString::fromUtf8(pszName), lstHeader, strErr, QStringLiteral("验证"));
        baResult = (bOk ? pTask->report.path() : strErr).toUtf8();
    }
    if (bOk) {
        lua_pushlstring(L, baResult.constData(), static_cast<size_t>(baResult.size()));
        return 1;  // 返回路径
    }
    lua_pushnil(L);
    lua_pushlstring(L, baResult.constData(), static_cast<size_t>(baResult.size()));
    return 2;  // 返回 nil, 错误
}

int luaCsvRow(lua_State* L)
{
    pushJoinedTable(L, 1);
    const char* pszRow = lua_tostring(L, -1);
    TaskState* pTask = taskOf(L);
    int nRows = 0;
    {
        pTask->report.writeRow(QString::fromUtf8(pszRow).split(QChar(0x1F)));
        nRows = pTask->report.rowCount();
    }
    lua_pushinteger(L, nRows);
    return 1;  // 返回已写行数
}

int luaCsvEnd(lua_State* L)
{
    TaskState* pTask = taskOf(L);
    const int nRows = pTask->report.rowCount();
    QByteArray baPath;
    {
        const QString strPath = pTask->report.end();
        if (!strPath.isEmpty()) {
            pTask->pEngine->emitLog(QStringLiteral("[%1] 报告已保存 (%2 行): %3").arg(pTask->strName).arg(nRows).arg(strPath));
        }
        baPath = strPath.toUtf8();
    }
    lua_pushlstring(L, baPath.constData(), static_cast<size_t>(baPath.size()));
    lua_pushinteger(L, nRows);
    return 2;  // 返回 路径, 行数
}

int luaRandomObjectOffset(lua_State* L)
{
    const lua_Number dMaxXY = luaL_checknumber(L, 1);
    const lua_Number dMaxTheta = luaL_checknumber(L, 2);
    taskOf(L)->ctx.pAlignScene->randomizeObjectOffset(std::abs(dMaxXY), std::abs(dMaxTheta));
    return 0;
}

int luaSetObjectOffset(lua_State* L)
{
    const Pose offset{ luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3) };
    taskOf(L)->ctx.pAlignScene->setObjectOffset(offset);
    return 0;
}

// ---------------- 拾取 ----------------
int luaSpawnProducts(lua_State* L)
{
    const lua_Integer nCount = luaL_optinteger(L, 1, -1);  // 不传参用配置数量
    taskOf(L)->ctx.pPickScene->spawnProducts(static_cast<int>(nCount));
    return 0;
}

int luaPickAt(lua_State* L)
{
    const int nNozzle = static_cast<int>(luaL_optinteger(L, 1, 1));  // 默认 1号爪
    const PickResult res = taskOf(L)->ctx.pPickScene->pick(nNozzle);
    lua_pushboolean(L, res.bHit);
    lua_pushnumber(L, res.dX);
    lua_pushnumber(L, res.dY);
    lua_pushnumber(L, res.dThetaDeg);
    lua_pushboolean(L, res.bFlipped);  // 20261026 ZJH 第 5 返回值: 是否反面
    return 5;  // 返回 是否吸到, Δx, Δy, Δθ, 是否反面
}

int luaPlaceAt(lua_State* L)
{
    const int nNozzle = static_cast<int>(luaL_optinteger(L, 1, 0));  // 默认 0: 全部爪一起放下
    lua_pushboolean(L, taskOf(L)->ctx.pPickScene->place(nNozzle));
    return 1;
}

int luaIsHolding(lua_State* L)
{
    const int nNozzle = static_cast<int>(luaL_optinteger(L, 1, 0));  // 默认 0: 任一爪吸着即返回 true
    lua_pushboolean(L, taskOf(L)->ctx.pPickScene->isHolding(nNozzle));
    return 1;
}

// 20261026 ZJH 双爪拾取辅助接口: 查询爪数量、偏移、当前爪位姿、移动指定爪对准目标位姿
int luaGetNozzleCount(lua_State* L)
{
    lua_pushinteger(L, taskOf(L)->ctx.pPickScene->nozzleCount());
    return 1;
}

int luaGetNozzleOffset(lua_State* L)
{
    const int nNozzle = static_cast<int>(luaL_optinteger(L, 1, 1));
    const QPointF ptOff = taskOf(L)->ctx.pPickScene->nozzleLocalOffset(nNozzle);
    lua_pushnumber(L, ptOff.x());
    lua_pushnumber(L, ptOff.y());
    return 2;
}

int luaGetNozzlePose(lua_State* L)
{
    const int nNozzle = static_cast<int>(luaL_optinteger(L, 1, 1));
    const Pose pose = taskOf(L)->ctx.pPickScene->nozzlePose(nNozzle);
    lua_pushnumber(L, pose.dX);
    lua_pushnumber(L, pose.dY);
    lua_pushnumber(L, pose.dThetaDeg);
    return 3;
}

int luaMoveNozzleTo(lua_State* L)
{
    const int nNozzle = static_cast<int>(luaL_checkinteger(L, 1));
    const Pose targetPose{ luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 0.0) };
    TaskState* pTask = taskOf(L);
    PickScene* pPickScene = pTask->ctx.pPickScene;
    Stage* pHead = pPickScene->head();
    const Pose headPose = pPickScene->headPoseForNozzleTarget(nNozzle, targetPose);
    moveTimed(L, pHead, pHead->axesForPose(headPose));
    return 0;
}

int luaGetProductCount(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(taskOf(L)->ctx.pPickScene->products().size()));
    return 1;
}

int luaGetProductPose(lua_State* L)
{
    const lua_Integer nIndex = luaL_checkinteger(L, 1);  // 从 1 开始
    const std::vector<Product> vecProd = taskOf(L)->ctx.pPickScene->products();
    // 20261024 ZJH 越界: 返回 nil (此处 vector 已构造, 不能用 luaL_error 跳出)
    if (nIndex < 1 || nIndex > static_cast<lua_Integer>(vecProd.size())) {
        lua_pushnil(L);
        return 1;
    }
    const Product& prod = vecProd[static_cast<size_t>(nIndex - 1)];
    lua_pushnumber(L, prod.pose.dX);
    lua_pushnumber(L, prod.pose.dY);
    lua_pushnumber(L, prod.pose.dThetaDeg);
    lua_pushboolean(L, prod.bFlipped);  // 20261026 ZJH 第 4 返回值: 是否反面 (兼容只收 3 个返回值的旧脚本)
    return 4;  // 返回产品位姿 x, y, θ, flipped
}

// 20261026 ZJH 设置第 i 个产品 (从 1 开始) 是否为反面
int luaSetProductFlipped(lua_State* L)
{
    const lua_Integer nIndex = luaL_checkinteger(L, 1);
    const bool bFlipped = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, taskOf(L)->ctx.pPickScene->setProductFlipped(static_cast<int>(nIndex - 1), bFlipped));
    return 1;
}

// 20261026 ZJH 对位场景单独显示/隐藏目标 Mark 或对象 Mark (标定或拍目标模板时避免两者重合互相干扰)
int luaShowAlignTarget(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    AlignScene* pScene = taskOf(L)->ctx.pAlignScene;
    AlignDisplay disp = pScene->display();
    disp.bShowTarget = bShow;
    pScene->setDisplay(disp);
    return 0;
}

int luaShowAlignObject(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    AlignScene* pScene = taskOf(L)->ctx.pAlignScene;
    AlignDisplay disp = pScene->display();
    disp.bShowObject = bShow;
    pScene->setDisplay(disp);
    return 0;
}

int luaShowAlignCalibMark(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    AlignScene* pScene = taskOf(L)->ctx.pAlignScene;
    AlignDisplay disp = pScene->display();
    disp.bShowCalibMark = bShow;
    pScene->setDisplay(disp);
    return 0;
}

int luaCenterAlignCalibMark(lua_State* L)
{
    taskOf(L)->ctx.pAlignScene->centerCalibMark();
    return 0;
}

int luaShowAlignStage(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    AlignScene* pScene = taskOf(L)->ctx.pAlignScene;
    AlignDisplay disp = pScene->display();
    disp.bShowStage = bShow;
    pScene->setDisplay(disp);
    return 0;
}

int luaShowPickStage(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    PickScene* pScene = taskOf(L)->ctx.pPickScene;
    PickDisplay disp = pScene->display();
    disp.bShowStage = bShow;
    pScene->setDisplay(disp);
    return 0;
}
int luaShowPickCalibMark(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    PickScene* pScene = taskOf(L)->ctx.pPickScene;
    PickDisplay disp = pScene->display();
    disp.bShowCalibMark = bShow;
    pScene->setDisplay(disp);
    return 0;
}

// 20261026 ZJH 移动拾取头使标定 Mark 位于视野正中央 (W/2, H/2)
int luaCenterPickCalibMark(lua_State* L)
{
    taskOf(L)->ctx.pPickScene->moveCalibMarkToFieldCenter();
    return 0;
}

// 20261026 ZJH 设置拾取头标定 Mark 挂载位置: 0=旋转中心, 1=1号爪, 2=2号爪, 3=双爪同时显示
int luaSetPickCalibMount(lua_State* L)
{
    const int nMount = std::clamp(static_cast<int>(luaL_checkinteger(L, 1)), 0, 3);
    PickScene* pScene = taskOf(L)->ctx.pPickScene;
    PickDisplay disp = pScene->display();
    disp.nCalibMount = nMount;
    pScene->setDisplay(disp);
    return 0;
}

// 20261026 ZJH 传送带步进上料模拟: 视野内待拾取产品整体沿 +X 平移 dStepX, 并在左侧补入 nNewCount 个新产品
int luaStepConveyor(lua_State* L)
{
    const double dStepX = luaL_optnumber(L, 1, 140.0);
    const int nNewCount = static_cast<int>(luaL_optinteger(L, 2, 2));
    taskOf(L)->ctx.pPickScene->stepConveyor(dStepX, nNewCount);
    return 0;
}

// 20261026 ZJH 主从双工位联动: 将最近一次拾取的偏差 (-dX, -dY, -dThetaDeg) 作为产品放到对位平台上的来料偏差, 并回零对位平台
int luaTransferPickToAlign(lua_State* L)
{
    TaskState* pTask = taskOf(L);
    const PickResult res = pTask->ctx.pPickScene->lastResult();
    const Pose incomingOffset{ -res.dX, -res.dY, -res.dThetaDeg };
    pTask->ctx.pAlignScene->setObjectOffset(incomingOffset);
    if (pTask->ctx.pAlignStage) {
        pTask->ctx.pAlignStage->home();
    }
    lua_pushnumber(L, incomingOffset.dX);
    lua_pushnumber(L, incomingOffset.dY);
    lua_pushnumber(L, incomingOffset.dThetaDeg);
    return 3;
}

int luaShowProducts(lua_State* L)
{
    const bool bShow = lua_toboolean(L, 1) != 0;
    PickScene* pScene = taskOf(L)->ctx.pPickScene;
    PickDisplay disp = pScene->display();
    disp.bShowProducts = bShow;
    pScene->setDisplay(disp);
    return 0;
}

int luaGetFieldSize(lua_State* L)
{
    const PickSceneConfig cfg = taskOf(L)->ctx.pPickScene->config();
    lua_pushnumber(L, cfg.dFieldW);
    lua_pushnumber(L, cfg.dFieldH);
    return 2;  // 返回 视野宽, 视野高 (mm)
}

// ---------------- 标定记录 ----------------
// 20261025 ZJH 数值 → CSV 文本 (NaN 表示未提供, 写空)
QString numText(double dValue, int nDecimals = 6)
{
    return std::isnan(dValue) ? QString() : QString::number(dValue, 'f', nDecimals);
}

// 20261025 ZJH 当前平台是否为拾取头
bool isPickStage(const TaskState* pTask)
{
    return pTask->pCurrentStage == pTask->ctx.pPickStage;
}

int luaCalibBegin(lua_State* L)
{
    const char* pszName = luaL_optstring(L, 1, "calib");  // 先取参数 (可能报错), 之后才构造 Qt 对象
    TaskState* pTask = taskOf(L);
    Stage* pStage = checkStage(L);
    bool bOk = false;
    QByteArray baResult;  // 成功: 文件路径; 失败: 错误信息 (UTF-8)
    {
        const QStringList lstAxis = pStage->axisNames();
        QStringList lstHeader = { QStringLiteral("序号"), QStringLiteral("时间"), QStringLiteral("平台"),
                                  QStringLiteral("目标1"), QStringLiteral("目标2"), QStringLiteral("目标3"),
                                  QStringLiteral("轴") + lstAxis.value(0), QStringLiteral("轴") + lstAxis.value(1),
                                  QStringLiteral("轴") + lstAxis.value(2),
                                  QStringLiteral("位姿X"), QStringLiteral("位姿Y"), QStringLiteral("位姿θ"),
                                  QStringLiteral("实际位姿X"), QStringLiteral("实际位姿Y"), QStringLiteral("实际位姿θ") };
        // 20261025 ZJH 拾取头: 一个标定 Mark; 对位平台: 每个对象 Mark
        if (isPickStage(pTask)) {
            lstHeader << QStringLiteral("标定Mark_X") << QStringLiteral("标定Mark_Y") << QStringLiteral("标定Mark_θ")
                      << QStringLiteral("标定Mark_屏幕px_X") << QStringLiteral("标定Mark_屏幕px_Y");
        } else {
            for (const MarkPair& mark : pTask->ctx.pAlignScene->marks()) {
                lstHeader << mark.strName + QStringLiteral("_X") << mark.strName + QStringLiteral("_Y")
                          << mark.strName + QStringLiteral("_θ")
                          << mark.strName + QStringLiteral("_屏幕px_X") << mark.strName + QStringLiteral("_屏幕px_Y");
            }
        }
        QString strErr;
        bOk = pTask->recorder.begin(pTask->ctx.strLogDir, QString::fromUtf8(pszName), lstHeader, strErr);
        baResult = (bOk ? pTask->recorder.path() : strErr).toUtf8();
        pTask->pEngine->emitLog(QStringLiteral("[%1] %2").arg(pTask->strName,
            bOk ? QStringLiteral("开始标定记录 ") + pTask->recorder.path() : strErr));
    }
    // 20261025 ZJH 成功返回路径; 失败返回 nil, 错误信息
    if (bOk) {
        lua_pushlstring(L, baResult.constData(), static_cast<size_t>(baResult.size()));
        return 1;
    }
    lua_pushnil(L);
    lua_pushlstring(L, baResult.constData(), static_cast<size_t>(baResult.size()));
    return 2;
}

int luaCalibRecord(lua_State* L)
{
    const double arrTarget[3] = { luaL_optnumber(L, 1, NAN), luaL_optnumber(L, 2, NAN), luaL_optnumber(L, 3, NAN) };
    TaskState* pTask = taskOf(L);
    Stage* pStage = checkStage(L);
    // 20261025 ZJH 未 CalibBegin 时返回 0 (不报错, 方便公共库无条件调用)
    if (!pTask->recorder.isOpen()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int nRows = 0;
    {
        const AxisValues axes = pStage->axes();
        const Pose pose = pStage->commandedPose();   // 指令位姿 (回写视觉的)
        const Pose poseAct = pStage->pose();          // 实际位姿 (误差注入后的真值)
        QStringList lstRow = { QString::number(pTask->recorder.rowCount() + 1),
                               QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                               pStage->name(),
                               numText(arrTarget[0]), numText(arrTarget[1]), numText(arrTarget[2]),
                               numText(axes.dA1), numText(axes.dA2), numText(axes.dA3),
                               numText(pose.dX), numText(pose.dY), numText(pose.dThetaDeg),
                               numText(poseAct.dX), numText(poseAct.dY), numText(poseAct.dThetaDeg) };
        if (isPickStage(pTask)) {
            const Pose poseMark = pTask->ctx.pPickScene->calibMarkPose();
            QPointF ptPx;
            const bool bPx = pTask->ctx.pPickScene->calibMarkScreenOffset(ptPx);  // 非固定比例时像素列留空
            lstRow << numText(poseMark.dX) << numText(poseMark.dY) << numText(poseMark.dThetaDeg)
                   << (bPx ? numText(ptPx.x(), 2) : QString()) << (bPx ? numText(ptPx.y(), 2) : QString());
        } else {
            AlignScene* pScene = pTask->ctx.pAlignScene;
            const int nMarks = static_cast<int>(pScene->marks().size());
            for (int i = 0; i < nMarks; ++i) {  // 每个对象 Mark
                const MarkWorldPose obj = pScene->objectMarkWorld(i);
                QPointF ptPx;
                const bool bPx = pScene->objectMarkScreenOffset(i, ptPx);
                lstRow << numText(obj.ptPos.x()) << numText(obj.ptPos.y()) << numText(obj.dAngleDeg)
                       << (bPx ? numText(ptPx.x(), 2) : QString()) << (bPx ? numText(ptPx.y(), 2) : QString());
            }
        }
        pTask->recorder.writeRow(lstRow);
        nRows = pTask->recorder.rowCount();
    }
    lua_pushinteger(L, nRows);
    return 1;  // 返回已记录行数
}

int luaCalibEnd(lua_State* L)
{
    TaskState* pTask = taskOf(L);
    const int nRows = pTask->recorder.rowCount();
    QByteArray baPath;
    {
        const QString strPath = pTask->recorder.end();
        // 20261025 ZJH 有记录才写日志
        if (!strPath.isEmpty()) {
            pTask->pEngine->emitLog(QStringLiteral("[%1] 标定记录完成 %2 点: %3").arg(pTask->strName).arg(nRows).arg(strPath));
        }
        baPath = strPath.toUtf8();
    }
    lua_pushlstring(L, baPath.constData(), static_cast<size_t>(baPath.size()));
    lua_pushinteger(L, nRows);
    return 2;  // 返回 路径, 行数
}

// ---------------- 模块加载 ----------------
// 20261025 ZJH package.searchers 搜索器: 用 QFile 从 scripts/ 与 scripts/lib/ 读取模块
// (Lua 自带搜索器用 fopen, Windows 下 UTF-8 中文路径打不开)
int luaModuleSearcher(lua_State* L)
{
    const char* pszName = luaL_checkstring(L, 1);
    TaskState* pTask = taskOf(L);
    bool bFound = false;
    {
        const QString strRel = QString::fromUtf8(pszName).replace(QLatin1Char('.'), QLatin1Char('/'));
        const QStringList lstCandidates = { pTask->ctx.strScriptDir + QStringLiteral("/") + strRel + QStringLiteral(".lua"),
                                            pTask->ctx.strScriptDir + QStringLiteral("/lib/") + strRel + QStringLiteral(".lua") };
        for (const QString& strPath : lstCandidates) {  // 依次尝试
            QFile file(strPath);
            if (!pTask->ctx.strScriptDir.isEmpty() && file.open(QIODevice::ReadOnly)) {
                const QByteArray baCode = file.readAll();
                const QByteArray baChunk = ("@" + strPath).toUtf8();
                lua_pushlstring(L, baCode.constData(), static_cast<size_t>(baCode.size()));    // 源码
                lua_pushlstring(L, baChunk.constData(), static_cast<size_t>(baChunk.size()));  // 块名
                bFound = true;
                break;
            }
        }
    }
    // 20261025 ZJH 没找到: 按约定返回一段说明文字, require 会汇总所有搜索器的说明报错
    if (!bFound) {
        lua_pushfstring(L, "\n\tno module '%s' in scripts dir", pszName);
        return 1;
    }
    size_t nLen = 0;
    const char* pszCode = lua_tolstring(L, -2, &nLen);
    const char* pszChunk = lua_tostring(L, -1);
    // 20261025 ZJH 编译失败: 抛出语法错误 (此时已无 Qt 局部对象)
    if (luaL_loadbuffer(L, pszCode, nLen, pszChunk) != LUA_OK) {
        return lua_error(L);
    }
    lua_pushstring(L, pszChunk + 1);  // 第二返回值: 模块文件路径 (传给加载器)
    return 2;
}

// 20261024 ZJH 函数注册表 (名称 → 实现)
const luaL_Reg s_arrFuncs[] = {
    { "SetInt16", luaSetInt16 }, { "SetInt32", luaSetInt32 }, { "SetFloat", luaSetFloat },
    { "GetInt16", luaGetInt16 }, { "GetInt32", luaGetInt32 }, { "GetFloat", luaGetFloat },
    { "sleep", luaSleep }, { "IsLoopValid", luaIsLoopValid }, { "Log", luaLog }, { "TickMs", luaTickMs },
    { "SelectStage", luaSelectStage },
    { "MoveAbsInt32", luaMoveAbsInt32 }, { "MoveAbsFloat", luaMoveAbsFloat },
    { "MoveRelativeInt32", luaMoveRelInt32 }, { "MoveRelativeFloat", luaMoveRelFloat },
    { "WriteCurrentPosInt32", luaWriteCurrentPosInt32 }, { "WriteCurrentPosFloat", luaWriteCurrentPosFloat },
    { "MovePoseAbsInt32", luaMovePoseAbsInt32 }, { "MovePoseAbsFloat", luaMovePoseAbsFloat },
    { "MovePoseRelInt32", luaMovePoseRelInt32 }, { "MovePoseRelFloat", luaMovePoseRelFloat },
    { "WritePoseInt32", luaWritePoseInt32 }, { "WritePoseFloat", luaWritePoseFloat },
    { "MoveTo", luaMoveTo }, { "MoveBy", luaMoveBy }, { "Home", luaHome },
    { "GetAxes", luaGetAxes }, { "GetPose", luaGetPose },
    { "GetAlignResidual", luaGetAlignResidual }, { "RandomObjectOffset", luaRandomObjectOffset },
    { "SetObjectOffset", luaSetObjectOffset }, { "ShowAlignTarget", luaShowAlignTarget }, { "ShowAlignObject", luaShowAlignObject },
    { "ShowAlignCalibMark", luaShowAlignCalibMark }, { "CenterAlignCalibMark", luaCenterAlignCalibMark },
    { "ShowAlignStage", luaShowAlignStage }, { "ShowPickStage", luaShowPickStage },
    { "SpawnProducts", luaSpawnProducts }, { "StepConveyor", luaStepConveyor },
    { "PickAt", luaPickAt }, { "PlaceAt", luaPlaceAt },
    { "IsHolding", luaIsHolding }, { "GetNozzleCount", luaGetNozzleCount }, { "GetNozzleOffset", luaGetNozzleOffset },
    { "GetNozzlePose", luaGetNozzlePose }, { "MoveNozzleTo", luaMoveNozzleTo },
    { "GetProductCount", luaGetProductCount }, { "GetProductPose", luaGetProductPose },
    { "SetProductFlipped", luaSetProductFlipped },
    { "ShowPickCalibMark", luaShowPickCalibMark }, { "CenterPickCalibMark", luaCenterPickCalibMark },
    { "SetPickCalibMount", luaSetPickCalibMount }, { "TransferPickToAlign", luaTransferPickToAlign },
    { "ShowProducts", luaShowProducts }, { "GetFieldSize", luaGetFieldSize },
    { "CalibBegin", luaCalibBegin }, { "CalibRecord", luaCalibRecord }, { "CalibEnd", luaCalibEnd },
    { "GetActualPose", luaGetActualPose }, { "MovePoseBy", luaMovePoseBy }, { "GetRotCenter", luaGetRotCenter },
    { "GetAlignCentroids", luaGetAlignCentroids },
    { "CsvBegin", luaCsvBegin }, { "CsvRow", luaCsvRow }, { "CsvEnd", luaCsvEnd },
    { nullptr, nullptr },
};

// 20261024 ZJH 指令计数钩子: 每执行 1000 条指令检查一次停止标志, 用于打断死循环
void stopHook(lua_State* L, lua_Debug*)
{
    TaskState* pTask = taskOf(L);
    if (pTask && pTask->pStop->load()) {
        luaL_error(L, "script stopped");
    }
}

// 20261024 ZJH 线程池任务: 运行一个脚本
class ScriptTask : public QRunnable
{
public:
    ScriptTask(LuaEngine* pEngine, const LuaContext& ctx, const QString& strName, const QString& strCode,
               std::shared_ptr<std::atomic_bool> pStop)
        : m_pEngine(pEngine), m_ctx(ctx), m_strName(strName), m_baCode(strCode.toUtf8()), m_pStop(std::move(pStop))
    {
    }

    void run() override
    {
        TaskState task;  // 生命周期覆盖整个 lua_State
        task.ctx = m_ctx;
        task.pStop = m_pStop.get();
        task.pCurrentStage = m_ctx.pAlignStage;  // 默认作用于对位平台
        task.pEngine = m_pEngine;
        task.strName = m_strName;

        lua_State* L = luaL_newstate();
        // 20261024 ZJH 内存不足无法创建状态机
        if (!L) {
            m_pEngine->notifyFinished(m_strName, false, QStringLiteral("无法创建 Lua 状态机"));
            return;
        }
        luaL_openlibs(L);
        lua_pushlightuserdata(L, &task);
        lua_setfield(L, LUA_REGISTRYINDEX, s_pszTaskKey);  // 任务状态放入注册表
        lua_pushglobaltable(L);
        luaL_setfuncs(L, s_arrFuncs, 0);  // 注册全部函数到全局表
        lua_pop(L, 1);
        // 20261025 ZJH 把自定义模块搜索器插到 package.searchers 第 2 位 (preload 之后, 自带文件搜索器之前)
        lua_pushcfunction(L, luaModuleSearcher);
        lua_setglobal(L, "__alignsim_searcher");
        luaL_dostring(L, "table.insert(package.searchers, 2, __alignsim_searcher); __alignsim_searcher = nil");
        lua_sethook(L, stopHook, LUA_MASKCOUNT, 1000);

        bool bOk = false;  // 运行结果
        QString strErr;    // 错误信息
        const QByteArray baChunk = QStringLiteral("=%1").arg(m_strName).toUtf8();  // 错误信息里的脚本名
        // 20261024 ZJH 编译 + 保护模式执行
        if (luaL_loadbuffer(L, m_baCode.constData(), static_cast<size_t>(m_baCode.size()), baChunk.constData()) == LUA_OK
            && lua_pcall(L, 0, 0, 0) == LUA_OK) {
            bOk = true;
        } else {
            strErr = QString::fromUtf8(lua_tostring(L, -1));
        }
        lua_close(L);
        m_pEngine->notifyFinished(m_strName, bOk, strErr);
    }

private:
    LuaEngine* m_pEngine;                       // 引擎
    LuaContext m_ctx;                           // 可访问对象
    QString m_strName;                          // 脚本名
    QByteArray m_baCode;                        // 脚本源码 (UTF-8)
    std::shared_ptr<std::atomic_bool> m_pStop;  // 停止标志
};

}  // namespace

LuaEngine::LuaEngine(const LuaContext& ctx, QObject* pParent)
    : QObject(pParent)
    , m_ctx(ctx)
{
    m_pool.setMaxThreadCount(16);  // 最多 16 个脚本同时运行
}

LuaEngine::~LuaEngine()
{
    stopAll();
    m_pool.waitForDone(3000);  // 等脚本退出, 防止工作线程访问已析构对象
}

bool LuaEngine::start(const QString& strName, const QString& strCode, QString& strErr)
{
    // 20261024 ZJH 先做语法检查, 有错直接返回
    if (!checkSyntax(strCode, strErr)) {
        return false;
    }
    std::shared_ptr<std::atomic_bool> pStop;
    {
        QMutexLocker locker(&m_mutex);
        // 20261024 ZJH 同名脚本只能运行一个
        if (m_hashStopFlags.contains(strName)) {
            strErr = QStringLiteral("脚本 %1 正在运行").arg(strName);
            return false;
        }
        pStop = std::make_shared<std::atomic_bool>(false);
        m_hashStopFlags.insert(strName, pStop);
    }
    emit scriptStarted(strName);
    m_pool.start(new ScriptTask(this, m_ctx, strName, strCode, pStop));  // 线程池接管任务所有权
    return true;  // 返回已启动
}

void LuaEngine::stop(const QString& strName)
{
    QMutexLocker locker(&m_mutex);
    // 20261024 ZJH 只设标志, 脚本在 sleep/钩子/IsLoopValid 处自行退出
    if (auto it = m_hashStopFlags.find(strName); it != m_hashStopFlags.end()) {
        it.value()->store(true);
    }
}

void LuaEngine::stopAll()
{
    QMutexLocker locker(&m_mutex);
    for (auto& pStop : m_hashStopFlags) {  // 所有运行中脚本置停止
        pStop->store(true);
    }
}

bool LuaEngine::isRunning(const QString& strName) const
{
    QMutexLocker locker(&m_mutex);
    return m_hashStopFlags.contains(strName);
}

bool LuaEngine::waitForDone(int nTimeoutMs)
{
    return m_pool.waitForDone(nTimeoutMs);
}

void LuaEngine::notifyFinished(const QString& strName, bool bOk, const QString& strErr)
{
    {
        QMutexLocker locker(&m_mutex);
        m_hashStopFlags.remove(strName);  // 登记结束
    }
    emit scriptFinished(strName, bOk, strErr);
}

bool LuaEngine::checkSyntax(const QString& strCode, QString& strErr)
{
    lua_State* L = luaL_newstate();
    // 20261024 ZJH 创建失败视为检查失败
    if (!L) {
        strErr = QStringLiteral("无法创建 Lua 状态机");
        return false;
    }
    const QByteArray baCode = strCode.toUtf8();
    const bool bOk = luaL_loadbuffer(L, baCode.constData(), static_cast<size_t>(baCode.size()), "=script") == LUA_OK;
    // 20261024 ZJH 编译失败取错误信息
    if (!bOk) {
        strErr = QString::fromUtf8(lua_tostring(L, -1));
    }
    lua_close(L);
    return bOk;  // 返回语法是否正确
}

QStringList LuaEngine::functionNames()
{
    QStringList lstNames;
    for (const luaL_Reg* p = s_arrFuncs; p->name; ++p) {  // 遍历注册表
        lstNames << QString::fromLatin1(p->name);
    }
    return lstNames;  // 返回全部函数名
}
