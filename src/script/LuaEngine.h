// 20261024 ZJH LuaEngine —— Lua 脚本引擎
// 每个脚本在线程池中独立运行, 拥有自己的 lua_State (互不干扰, 可同时跑对位脚本和拾取脚本)
// 停止机制: 设置停止标志 + 指令计数钩子, 即使脚本是不调 IsLoopValid 的死循环也能被打断
//
// 注册到 Lua 的函数 (寄存器地址可写 "D100" / "DM100" / 100):
//   寄存器: SetInt16 SetInt32 SetFloat GetInt16 GetInt32 GetFloat
//   流程  : sleep(ms) IsLoopValid() Log(...) TickMs()→单调毫秒 (超时判断用)
//   平台  : SelectStage("align"|"pick")  —— 选择后续运动指令作用的平台, 默认 align
//           MoveAbsInt32/MoveAbsFloat/MoveRelativeInt32/MoveRelativeFloat(r1,r2,r3)  轴空间 (XYD: X Y D / UVW: U V W)
//           WriteCurrentPosInt32/WriteCurrentPosFloat(r1,r2,r3)                         写当前轴值
//           MovePoseAbsInt32/MovePoseAbsFloat/MovePoseRelInt32/MovePoseRelFloat(r1,r2,r3) 位姿空间 (X Y θ)
//           WritePoseInt32/WritePoseFloat(r1,r2,r3)                                     写当前位姿
//           MoveTo(a,b,c) MoveBy(a,b,c) Home() GetAxes()→a,b,c GetPose()→x,y,θ          直接数值
//   对位  : GetAlignResidual()→dx,dy,dθ  RandomObjectOffset(maxXY,maxθ)  SetObjectOffset(x,y,θ)
//           ShowAlignTarget(bool) ShowAlignObject(bool)    —— 单独显示/隐藏目标 Mark 或对象 Mark
//   拾取  : SpawnProducts([n]) StepConveyor([stepX,newCount]) PickAt([nozzle])→ok,dx,dy,dθ,flipped PlaceAt([nozzle])→ok IsHolding([nozzle])
//           GetNozzleCount()→1|2 GetNozzleOffset(nozzle)→ox,oy GetNozzlePose([nozzle])→x,y,θ MoveNozzleTo(nozzle,x,y,θ)
//           GetProductCount() GetProductPose(i)→x,y,θ,flipped SetProductFlipped(i,bool) (i 从 1 开始)
//           ShowPickCalibMark(bool) CenterPickCalibMark() SetPickCalibMount(0..3) ShowProducts(bool) —— 拾取头标定显示/居中/切换挂载爪/隐藏产品
//           GetFieldSize()→w,h  TransferPickToAlign()→dx,dy,dθ (将拾取误差作为来料偏差传递到对位平台)
//   标定  : CalibBegin(name)→path  CalibRecord([t1,t2,t3])→行数  CalibEnd()→path,行数
//           (记录当前平台每个标定点的轴值/位姿/Mark 世界坐标与屏幕像素, CSV 存 logs 目录)
//   验证  : GetActualPose()→x,y,θ (实际, 含误差)  MovePoseBy(dx,dy,dθ)  GetRotCenter(["nominal"|"actual"])→x,y
//           GetAlignCentroids()→对象质心x,y,目标质心x,y   CsvBegin(name,{表头})→path  CsvRow({值})  CsvEnd()→path,行数
//   说明  : GetPose/WritePose 为指令位姿; 误差注入开启且设了速度时, 所有脚本移动按速度插补并等整定时间
//   模块  : require("handshake") 从 scripts/ 与 scripts/lib/ 加载 (支持中文路径)
#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <memory>

class RegisterBank;
class Stage;
class AlignScene;
class PickScene;

// 20261024 ZJH 脚本可访问的对象集合 (均不持有)
struct LuaContext {
    RegisterBank* pBank = nullptr;
    Stage* pAlignStage = nullptr;
    Stage* pPickStage = nullptr;
    AlignScene* pAlignScene = nullptr;
    PickScene* pPickScene = nullptr;
    QString strScriptDir;  // require 模块搜索目录 (scripts)
    QString strLogDir;     // 标定记录 CSV 目录 (logs)
};

class LuaEngine : public QObject
{
    Q_OBJECT
public:
    explicit LuaEngine(const LuaContext& ctx, QObject* pParent = nullptr);
    ~LuaEngine() override;

    // 20261024 ZJH 启动脚本; 同名脚本正在运行或语法错误时返回 false (strErr 给出原因)
    bool start(const QString& strName, const QString& strCode, QString& strErr);
    // 20261024 ZJH 请求停止 (异步, 脚本结束时发 scriptFinished)
    void stop(const QString& strName);
    void stopAll();
    bool isRunning(const QString& strName) const;
    // 20261024 ZJH 等待所有脚本结束 (测试/退出用); 超时返回 false
    bool waitForDone(int nTimeoutMs);

    // 20261024 ZJH 语法检查 (只编译不运行)
    static bool checkSyntax(const QString& strCode, QString& strErr);
    // 20261024 ZJH 已注册函数名 (编辑器提示用)
    static QStringList functionNames();

    // 20261024 ZJH 供工作线程调用: 脚本结束登记 + 发信号 (内部使用)
    void notifyFinished(const QString& strName, bool bOk, const QString& strErr);
    void emitLog(const QString& strMsg) { emit logMessage(strMsg); }

signals:
    // 20261024 ZJH 脚本开始运行
    void scriptStarted(const QString& strName);
    // 20261024 ZJH 脚本结束; bOk=false 时 strErr 为错误信息 (含被停止)
    void scriptFinished(const QString& strName, bool bOk, const QString& strErr);
    // 20261024 ZJH 脚本 Log() 输出或引擎日志 (来自工作线程)
    void logMessage(const QString& strMsg);

private:
    LuaContext m_ctx;                                              // 可访问对象
    QThreadPool m_pool;                                            // 脚本线程池
    mutable QMutex m_mutex;                                        // 保护 m_hashStopFlags
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_hashStopFlags;  // 运行中脚本 → 停止标志
};
