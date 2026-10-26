-- 20261025 ZJH 视觉握手公共库 —— 参考工程 FCLB/FAAC/FAAL 共用流程
--
-- 流程 (PLC = 本模拟器, 视觉 = 视觉软件):
--   1. PLC 写当前位置 → 写指令码/参数 → 置触发
--   2. 视觉置回复触发, 回复码:
--        = moveCode (101)  : 视觉要求移动到目标位置 → PLC 移动, 回写位置, 回复 "移动完成" (指令码=moveCode) 并再次触发
--        = cmd 本身        : 流程完成
--        其他              : 异常, 结束
--   标定 (FCLB) 时视觉逐点下发 9 点平移 + 若干旋转点; 对位 (FAAC/FAAL) 时下发补偿后的目标位置
--
-- 用法:
--   local hs = require("handshake")
--   local ok, nMoves = hs.run{ cmd = 10, stage = "align", record = true, name = "FCLB" }
--
-- 参数 (都有默认值):
--   cmd        指令码 (FCLB=10 / FAAC=33 / FAAL=32)
--   moveCode   视觉"继续移动"回复码, 默认 101
--   stage      "align" | "pick"
--   mode       "axes" 目标/当前位置为轴值 (XYD: X Y D, UVW: U V W) | "pose" 为位姿 X Y θ
--   relative   true 时目标为相对量 (默认 false 绝对)
--   p1, p2     触发时写入的参数 (默认 1, 1; 参考工程: 场景号/相机组)
--   p1Reply    回复"移动完成"时写入 p1 的值 (默认 0)
--   timeoutMs  等待视觉回复超时 (默认 30000, 标定时视觉可能要拍多张图)
--   settleMs   移动后等待稳定再回复 (默认 100, 给视觉留出"平台已停稳"的余量)
--   pollMs     轮询回复触发的间隔 (默认 10)
--   record     true 时每个点写标定记录 CSV
--   name       记录文件名后缀
--   regs       寄存器地址覆盖表, 见下方 DEFAULT_REGS
--   onMoved    每次移动完成后的回调 function(nIndex)
-- 返回: ok (是否正常完成), nMoves (移动次数), strMsg (结果说明)

local M = {}

M.DEFAULT_REGS = {
    trig     = "D100",                     -- PLC → 视觉: 触发
    cmd      = "D102",                     -- PLC → 视觉: 指令码
    p1       = "D104",                     -- PLC → 视觉: 参数 1
    p2       = "D106",                     -- PLC → 视觉: 参数 2
    pos      = { "D108", "D110", "D112" }, -- PLC → 视觉: 当前位置 (Int32)
    ansTrig  = "D150",                     -- 视觉 → PLC: 回复触发
    ansCode  = "D152",                     -- 视觉 → PLC: 回复码
    target   = { "D154", "D156", "D158" }, -- 视觉 → PLC: 目标位置 (Int32)
}

-- 20261025 ZJH 合并寄存器表: 用户只需覆盖关心的地址
local function mergeRegs(user)
    local regs = {}
    for k, v in pairs(M.DEFAULT_REGS) do regs[k] = v end
    if user then
        for k, v in pairs(user) do regs[k] = v end
    end
    return regs
end

-- 20261025 ZJH 写当前位置 (按 mode 选轴值或位姿)
local function writePos(regs, mode)
    if mode == "pose" then
        WritePoseInt32(regs.pos[1], regs.pos[2], regs.pos[3])
    else
        WriteCurrentPosInt32(regs.pos[1], regs.pos[2], regs.pos[3])
    end
end

-- 20261025 ZJH 按视觉下发的目标移动
local function moveToTarget(regs, mode, relative)
    local t = regs.target
    if mode == "pose" then
        if relative then MovePoseRelInt32(t[1], t[2], t[3]) else MovePoseAbsInt32(t[1], t[2], t[3]) end
    else
        if relative then MoveRelativeInt32(t[1], t[2], t[3]) else MoveAbsInt32(t[1], t[2], t[3]) end
    end
end

-- 20261025 ZJH 等待视觉回复触发, 返回 true=收到, false=超时/被停止
-- 20261026 ZJH 超时按真实耗时 (TickMs) 计算: sleep(10) 实际常常不止 10ms, 按次数累加会让超时被拉长
local function waitAnswer(regs, timeoutMs, pollMs)
    local t0 = TickMs()
    while IsLoopValid() do
        if GetInt32(regs.ansTrig) == 1 then
            return true
        end
        if TickMs() - t0 >= timeoutMs then
            return false
        end
        sleep(pollMs)
    end
    return false
end

function M.run(opt)
    opt = opt or {}
    local cmd       = opt.cmd or 10
    local moveCode  = opt.moveCode or 101
    local mode      = opt.mode or "axes"
    local relative  = opt.relative or false
    local timeoutMs = opt.timeoutMs or 30000
    local settleMs  = opt.settleMs or 100
    local pollMs    = opt.pollMs or 10
    local regs      = mergeRegs(opt.regs)

    SelectStage(opt.stage or "align")
    if opt.record then
        CalibBegin(opt.name or ("cmd" .. cmd))
        CalibRecord()                                   -- 第 0 点: 起始位置
    end

    -- 1. 写当前位置与指令, 置触发
    SetInt32(regs.ansTrig, 0)                           -- 清掉上一轮残留的回复触发
    writePos(regs, mode)
    SetInt32(regs.cmd, cmd)
    SetInt32(regs.p1, opt.p1 or 1)
    SetInt32(regs.p2, opt.p2 or 1)
    SetInt32(regs.trig, 1)
    Log(string.format("握手开始 指令码=%d 平台=%s 模式=%s", cmd, opt.stage or "align", mode))

    local nMoves = 0
    local ok, msg = false, ""
    while true do
        if not waitAnswer(regs, timeoutMs, pollMs) then
            msg = IsLoopValid() and string.format("等待视觉回复超时 (%d ms)", timeoutMs) or "脚本被停止"
            break
        end
        SetInt32(regs.ansTrig, 0)                       -- 清回复触发
        SetInt32(regs.trig, 0)
        sleep(50)                                       -- 等视觉把回复码和目标写全
        local ans = GetInt32(regs.ansCode)
        if ans == moveCode then
            moveToTarget(regs, mode, relative)
            sleep(settleMs)
            writePos(regs, mode)
            nMoves = nMoves + 1
            if opt.record then
                CalibRecord(GetInt32(regs.target[1]), GetInt32(regs.target[2]), GetInt32(regs.target[3]))
            end
            local a1, a2, a3 = GetAxes()
            Log(string.format("  第 %d 次移动 轴=(%.4f, %.4f, %.4f)", nMoves, a1, a2, a3))
            if opt.onMoved then opt.onMoved(nMoves) end
            SetInt32(regs.cmd, moveCode)                -- 回复 "移动完成"
            SetInt32(regs.p1, opt.p1Reply or 0)
            SetInt32(regs.trig, 1)
        elseif ans == cmd then
            ok = true
            msg = string.format("完成, 共移动 %d 次", nMoves)
            break
        else
            msg = string.format("视觉回复异常码 %d", ans)
            break
        end
    end

    if opt.record then
        CalibEnd()
    end
    SetInt32(regs.trig, 0)
    Log("握手结束: " .. msg)
    return ok, nMoves, msg
end

return M
