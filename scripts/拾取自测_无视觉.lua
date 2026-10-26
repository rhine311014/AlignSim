-- 20261024 ZJH 大视野拾取自测 (不需要视觉软件): 用产品真值 + 人为噪声模拟视觉结果
-- 验证拾取头运动、拾取误差统计与产品搬运显示; 支持单爪拾取与双爪先后拾取 (在「大视野拾取」页勾选启用双爪模式)

noiseXY = 0.5      -- 模拟视觉定位误差 ±mm
noiseTheta = 0.5   -- 模拟视觉角度误差 ±°
-- 20261026 ZJH 码盘阵列放料位 (视野右侧 2列×5行 料盘格位, 避免所有产品叠在同一点) 与 NG 抛料盒位置
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
SpawnProducts()
local nCount = GetProductCount()
local nNozzles = GetNozzleCount()  -- 1=单爪, 2=双爪(1号爪、2号爪先后抓)
local nCurNozzle = 1
Log(string.format("开始拾取自测: 产品数=%d, 模式=%s", nCount, nNozzles == 2 and "双爪先后拾取" or "单爪拾取"))

for i = 1, nCount do
    if not IsLoopValid() then return end
    local x, y, t, flipped = GetProductPose(i)
    if flipped then
        Log(string.format("产品 %d  反面(镜像), 跳过不拾取", i))
    else
        local vx = x + (math.random() * 2 - 1) * noiseXY
        local vy = y + (math.random() * 2 - 1) * noiseXY
        local vt = t + (math.random() * 2 - 1) * noiseTheta
        MoveNozzleTo(nCurNozzle, vx, vy, vt)  -- 自动换算使 nCurNozzle 号爪对准目标产品
        sleep(350)
        local ok, dx, dy, dt, isBack = PickAt(nCurNozzle)
        Log(string.format("产品 %d [正面] %d号爪吸到=%s  dX=%.4f dY=%.4f dθ=%.4f",
            i, nCurNozzle, tostring(ok), dx, dy, dt))
        if ok then
            if isBack then
                -- 若误吸到反面产品, 单独移到 NG 抛料位丢弃
                MoveNozzleTo(nCurNozzle, ngX, ngY, 0)
                sleep(300)
                PlaceAt(nCurNozzle)
            elseif nCurNozzle < nNozzles then
                -- 双爪模式下 1号爪吸完切到 2号爪继续去抓下一个产品
                nCurNozzle = nCurNozzle + 1
            else
                -- 已抓满 (单爪抓1个 / 双爪抓满2个), 一起移到下一个料盘格位放下
                local px, py = nextTrayPos()
                MoveTo(px, py, 0)
                sleep(350)
                PlaceAt()
                nCurNozzle = 1
            end
        end
    end
end
-- 若双爪最后只剩 1 个正面产品在 1号爪上, 补一次放料
if IsHolding() then
    local px, py = nextTrayPos()
    MoveTo(px, py, 0)
    sleep(350)
    PlaceAt()
end
Home()
