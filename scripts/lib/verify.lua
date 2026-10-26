-- 20261025 ZJH 对位验证公共库 —— 重复性 / 精度测试 + 统计报告
--
-- 每一轮: 平台回零 → 设置来料偏差 → 对位 → 读模拟器真值残差
--   重复性 (kind="repeat")  : 每轮同一个来料偏差 offset
--   精度   (kind="accuracy"): 每轮在 ±range 内随机来料偏差
-- 对位方式:
--   useVision = true : 走 FAAC 握手 (handshake.lua), 由视觉软件拍照计算补偿
--   useVision = false: 理想视觉 —— 按真值测量 Mark, 用指定旋转中心计算补偿, 迭代到收敛
--       idealRotCenter = "actual"  : 视觉标定完美 (知道实际旋转中心), 残差只剩重复性/间隙等不可标定误差
--       idealRotCenter = "nominal" : 视觉只知道标称旋转中心, 用来观察旋转中心标定误差的影响
-- 报告: logs/验证_<时间>_<name>.csv, 明细行 + 统计汇总行; 汇总同时打印日志并写寄存器 (D310 起, Float)

local hs = require("handshake")
local M = {}

-- 20261025 ZJH 理想视觉对位: 返回移动次数
function M.idealAlign(opt)
    local maxIter = opt.maxIter or 5
    local tolMm = opt.alignTolMm or 0.0005     -- 迭代停止阈值 (mm)
    local tolDeg = opt.alignTolDeg or 0.0005   -- 迭代停止阈值 (°)
    local which = opt.idealRotCenter or "actual"
    local nMoves = 0
    for _ = 1, maxIter do
        local dx, dy, dt = GetAlignResidual()
        if math.abs(dx) < tolMm and math.abs(dy) < tolMm and math.abs(dt) < tolDeg then
            break
        end
        local ocx, ocy, tcx, tcy = GetAlignCentroids()
        local cx, cy = GetRotCenter(which)
        -- 补偿: 先绕 (cx,cy) 转 -dθ, 再把对象质心平移到目标质心
        local th = -math.rad(dt)
        local rx = math.cos(th) * (ocx - cx) - math.sin(th) * (ocy - cy) + cx
        local ry = math.sin(th) * (ocx - cx) + math.cos(th) * (ocy - cy) + cy
        MovePoseBy(tcx - rx, tcy - ry, -dt)
        nMoves = nMoves + 1
    end
    return nMoves
end

-- 20261025 ZJH 统计: 平均 / σ (总体) / 最大绝对值
local function stats(values)
    local n = #values
    if n == 0 then return 0, 0, 0 end
    local sum, maxAbs = 0, 0
    for _, v in ipairs(values) do
        sum = sum + v
        if math.abs(v) > maxAbs then maxAbs = math.abs(v) end
    end
    local mean = sum / n
    local var = 0
    for _, v in ipairs(values) do var = var + (v - mean) ^ 2 end
    return mean, math.sqrt(var / n), maxAbs
end

local function f(v, d) return string.format("%." .. (d or 4) .. "f", v) end

function M.run(opt)
    opt = opt or {}
    local kind = opt.kind or "repeat"
    local count = opt.count or 30
    local tolUm = opt.tolUm or 5.0            -- 合格: |Δx|,|Δy| ≤ tolUm (µm)
    local tolDeg = opt.tolDeg or 0.005        -- 合格: |Δθ| ≤ tolDeg (°)
    local offset = opt.offset or { 0.5, -0.3, 0.2 }
    local range = opt.range or { 2.0, 1.0 }
    local name = opt.name or (kind == "repeat" and "重复性" or "精度")
    local mode = opt.useVision and "视觉软件(FAAC)" or ("理想视觉/" .. (opt.idealRotCenter or "actual"))

    SelectStage("align")
    CsvBegin(name, { "序号", "来料ΔX(mm)", "来料ΔY(mm)", "来料Δθ(°)", "对位前ΔX(mm)", "对位前ΔY(mm)", "对位前Δθ(°)",
                     "移动次数", "残差ΔX(µm)", "残差ΔY(µm)", "残差Δθ(°)", "残差距离(µm)", "合格" })
    Log(string.format("%s测试开始: %d 轮, 对位方式=%s, 合格标准 ±%.3fµm ±%.4f°", name, count, mode, tolUm, tolDeg))

    local vx, vy, vt, vd = {}, {}, {}, {}
    local nPass, nFail = 0, 0
    for i = 1, count do
        if not IsLoopValid() then break end
        Home()
        local ox, oy, ot
        if kind == "repeat" then
            ox, oy, ot = offset[1], offset[2], offset[3]
        else
            ox = (math.random() * 2 - 1) * range[1]
            oy = (math.random() * 2 - 1) * range[1]
            ot = (math.random() * 2 - 1) * range[2]
        end
        SetObjectOffset(ox, oy, ot)
        local bx, by, bt = GetAlignResidual()

        local nMoves = 0
        if opt.useVision then
            local ok, n, msg = hs.run{ cmd = opt.visionCmd or 33, stage = "align", mode = opt.visionMode or "axes",
                                       timeoutMs = opt.timeoutMs or 30000, regs = opt.regs }
            nMoves = n
            if not ok then
                Log(string.format("第 %d 轮视觉对位失败: %s, 测试中止", i, msg))
                break
            end
        else
            nMoves = M.idealAlign(opt)
        end

        local dx, dy, dt = GetAlignResidual()
        local dxu, dyu = dx * 1000, dy * 1000
        local dist = math.sqrt(dxu * dxu + dyu * dyu)
        local pass = math.abs(dxu) <= tolUm and math.abs(dyu) <= tolUm and math.abs(dt) <= tolDeg
        if pass then nPass = nPass + 1 else nFail = nFail + 1 end
        vx[#vx + 1], vy[#vy + 1], vt[#vt + 1], vd[#vd + 1] = dxu, dyu, dt, dist
        CsvRow({ i, f(ox), f(oy), f(ot), f(bx), f(by), f(bt), nMoves, f(dxu, 3), f(dyu, 3), f(dt, 5), f(dist, 3),
                 pass and "OK" or "NG" })
        if opt.logEach ~= false then
            Log(string.format("  第 %d 轮 移动%d次 残差 ΔX=%.3fµm ΔY=%.3fµm Δθ=%.5f° %s", i, nMoves, dxu, dyu, dt, pass and "OK" or "NG"))
        end
    end

    -- 20261025 ZJH 统计汇总
    local n = #vx
    local mx, sx, ax = stats(vx)
    local my, sy, ay = stats(vy)
    local mt, st, at = stats(vt)
    local md, sd, ad = stats(vd)
    local passRate = n > 0 and nPass / n * 100 or 0
    CsvRow({})
    CsvRow({ "统计", "有效轮数", n, "", "", "", "", "", "ΔX(µm)", "ΔY(µm)", "Δθ(°)", "距离(µm)" })
    CsvRow({ "平均", "", "", "", "", "", "", "", f(mx, 3), f(my, 3), f(mt, 5), f(md, 3) })
    CsvRow({ "σ", "", "", "", "", "", "", "", f(sx, 3), f(sy, 3), f(st, 5), f(sd, 3) })
    CsvRow({ "3σ", "", "", "", "", "", "", "", f(3 * sx, 3), f(3 * sy, 3), f(3 * st, 5), f(3 * sd, 3) })
    CsvRow({ "最大|值|", "", "", "", "", "", "", "", f(ax, 3), f(ay, 3), f(at, 5), f(ad, 3) })
    CsvRow({ "合格率", f(passRate, 1) .. "%", "合格 " .. nPass, "不合格 " .. nFail, "标准",
             "±" .. tolUm .. "µm", "±" .. tolDeg .. "°", "对位方式", mode })
    local path = CsvEnd()

    Log(string.format("%s测试结束 (%d 轮, %s)", name, n, mode))
    Log(string.format("  ΔX  平均 %.3f  3σ %.3f  最大 %.3f µm", mx, 3 * sx, ax))
    Log(string.format("  ΔY  平均 %.3f  3σ %.3f  最大 %.3f µm", my, 3 * sy, ay))
    Log(string.format("  Δθ  平均 %.5f  3σ %.5f  最大 %.5f °", mt, 3 * st, at))
    Log(string.format("  合格率 %.1f%% (%d/%d)  报告 %s", passRate, nPass, n, path))

    -- 20261025 ZJH 汇总写寄存器 (Float): D310 3σX(µm) D312 3σY(µm) D314 3σθ(°) D316 合格率(%) D318 有效轮数
    SetFloat("D310", 3 * sx)
    SetFloat("D312", 3 * sy)
    SetFloat("D314", 3 * st)
    SetFloat("D316", passRate)
    SetFloat("D318", n)
    return { n = n, passRate = passRate, sigmaX = sx, sigmaY = sy, sigmaT = st, maxX = ax, maxY = ay, maxT = at, path = path }
end

return M
