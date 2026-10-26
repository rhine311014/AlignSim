-- 20261024 ZJH 大视野拾取流程 —— 指令码/地址为占位约定, 按视觉软件协议修改
-- 支持单爪拾取与双爪先后拾取 (GetNozzleCount() == 2 时: 1号爪抓第1个 → 2号爪抓第2个 → 一起去放料位放下)
-- PLC → 视觉: D100 触发, D102 指令码, D104 场景, D106 当前爪号(1或2), D108/D110/D112 拾取头当前位姿 (Int32)
-- 视觉 → PLC: D150 回复触发, D152 回复码 (nCmdOk=成功 / nCmdNone=无产品), D154/D156/D158 拾取位姿 X/Y/θ (Int32, 视野坐标)
-- 结果回写: D200 是否吸到, D202/D204/D206 拾取误差 ΔX/ΔY/Δθ (Float), D208 是否反面

nCmdPick = 20      -- 拾取拍照指令码 (占位)
nCmdOk = 20        -- 视觉回复: 找到产品
nCmdNone = 21      -- 视觉回复: 没有产品了
nScene = 1
-- 20261026 ZJH 阵列码盘放料位 (2列×5行) 与 NG 抛料盒位置
trayBaseX, trayBaseY = 660, 80
trayStepX, trayStepY = 75, 55
trayCols = 2
ngX, ngY = 660, 360

local nPlacedBatch = 0
local function nextTrayPos()
    local nCol = nPlacedBatch % trayCols
    local nRow = math.floor(nPlacedBatch / trayCols)
    nPlacedBatch = nPlacedBatch + 1
    return trayBaseX + nCol * trayStepX, trayBaseY + nRow * trayStepY
end

SelectStage("pick")
SpawnProducts()            -- 重新随机摆放产品
Home()

local nNozzles = GetNozzleCount()  -- 1=单爪, 2=双爪
local nCurNozzle = 1
local nRound = 0
while IsLoopValid() do
    nRound = nRound + 1
    if nCurNozzle == 1 then
        Home()                                    -- 每轮首抓前回零避让, 不挡视野
    end
    WritePoseInt32("D108", "D110", "D112")
    SetInt32("D102", nCmdPick)
    SetInt32("D104", nScene)
    SetInt32("D106", nCurNozzle)                  -- 告知视觉当前使用 1号爪 还是 2号爪
    SetInt32("D150", 0)
    SetInt32("D100", 1)                           -- 触发拍照
    -- 等视觉回复 (5 秒超时, 20261026 ZJH 按真实耗时计时)
    local t0 = TickMs()
    while IsLoopValid() and GetInt32("D150") ~= 1 and TickMs() - t0 < 5000 do
        sleep(10)
    end
    if GetInt32("D150") ~= 1 then
        Log("等待视觉回复超时, 结束")
        break
    end
    SetInt32("D150", 0)
    SetInt32("D100", 0)
    local nAns = GetInt32("D152")
    if nAns == nCmdNone then
        if IsHolding() then
            local px, py = nextTrayPos()
            MoveTo(px, py, 0)
            sleep(200)
            PlaceAt()
        end
        Log("视觉回复无产品, 拾取结束, 共", nRound - 1, "个")
        break
    elseif nAns ~= nCmdOk then
        Log("视觉回复异常码", nAns)
        break
    end

    MovePoseAbsInt32("D154", "D156", "D158")      -- 移到视觉给的拾取头目标位姿
    sleep(200)
    local ok, dx, dy, dt, flipped = PickAt(nCurNozzle)
    SetInt32("D200", ok and 1 or 0)
    SetFloat("D202", dx)
    SetFloat("D204", dy)
    SetFloat("D206", dt)
    SetInt32("D208", flipped and 1 or 0)
    if not ok then
        Log(string.format("第 %d 次 (%d号爪): 未吸到产品", nRound, nCurNozzle))
    else
        Log(string.format("第 %d 次 (%d号爪): %s 拾取误差 dX=%.4f dY=%.4f dθ=%.4f", nRound, nCurNozzle,
            flipped and "[NG:误吸反面]" or "[正面]", dx, dy, dt))
        if flipped then
            -- 误吸反面产品 → 送往 NG 抛料盒单独丢弃
            MoveNozzleTo(nCurNozzle, ngX, ngY, 0)
            sleep(200)
            PlaceAt(nCurNozzle)
        elseif nCurNozzle < nNozzles then
            nCurNozzle = nCurNozzle + 1           -- 1号爪抓完, 切换 2号爪继续抓第2个
        else
            local px, py = nextTrayPos()
            MoveTo(px, py, 0)                     -- 抓满后一起搬到料盘下一格放下
            sleep(200)
            PlaceAt()
            nCurNozzle = 1
        end
    end
    sleep(200)
end
