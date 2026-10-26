-- 20261026 ZJH 主从双工位联动自测: 大视野粗定位拾取 → 搬运至精密对位平台 → 二次精对位贴合
-- 演示真实机台完整工艺链:
--   工位 1 (大视野拾取头): 识别正面产品 → 移动吸嘴抓取 (存在 ±0.8mm / ±0.6° 粗拾取偏差)
--   工位交接 (TransferPickToAlign): 拾取头把产品放到 UVW/XYD 精对位平台上, 粗拾取偏差转化为对位平台来料偏差
--   工位 2 (精密对位平台): 多相机检测 Mark 残差 → 围绕旋转中心迭代补偿 → 达到微米级贴合精度 → 放料码盘

local pickNoiseXY = 0.8     -- 大视野粗定位误差 ±mm
local pickNoiseDeg = 0.6    -- 大视野粗定位角度误差 ±°
local rad = math.pi / 180.0

SelectStage("pick")
SpawnProducts()
local nCount = GetProductCount()
Log(string.format("==== 开始双工位 [大视野拾取 → 精密对位贴合] 联动 (产品数=%d) ====", nCount))

for i = 1, nCount do
    if not IsLoopValid() then return end
    SelectStage("pick")
    local x, y, t, flipped = GetProductPose(i)
    if flipped then
        Log(string.format("[工位1-拾取] 产品#%d 为反面(无丝印字符), 跳过不抓", i))
    else
        -- 1. 大视野粗定位抓取
        local vx = x + (math.random() * 2 - 1) * pickNoiseXY
        local vy = y + (math.random() * 2 - 1) * pickNoiseXY
        local vt = t + (math.random() * 2 - 1) * pickNoiseDeg
        MoveNozzleTo(1, vx, vy, vt)
        sleep(250)
        local ok, dx, dy, dt = PickAt(1)
        if ok then
            Log(string.format("[工位1-拾取] 产品#%d 抓取成功, 粗定位偏差 dX=%.3fmm dY=%.3fmm dθ=%.3f°", i, dx, dy, dt))
            -- 2. 搬运到对位工位交接位
            MoveTo(660, 80 + (i - 1) * 55, 0)
            sleep(250)
            PlaceAt(1)
            local ox, oy, ot = TransferPickToAlign()
            Log(string.format("[工位交接] 产品#%d 已送入精密对位台, 初始来料偏差=(%.3fmm, %.3fmm, %.3f°)", i, ox, oy, ot))

            -- 3. 切换到工位 2 (精密对位平台, 如吉嘉 G+ UVW 平台) 执行闭环精对位
            SelectStage("align")
            for iter = 1, 2 do
                local _, _, _, _, _ = nil
                local rx, ry, rt = GetAlignResidual()
                local cx, cy = GetRotCenter("nominal")
                local oxMean, oyMean, txMean, tyMean = GetAlignCentroids()
                local dTheta = -rt * rad
                local cosT, sinT = math.cos(dTheta), math.sin(dTheta)
                local rotOx = cosT * (oxMean - cx) - sinT * (oyMean - cy) + cx
                local rotOy = sinT * (oxMean - cx) + cosT * (oyMean - cy) + cy
                MovePoseBy(txMean - rotOx, tyMean - rotOy, -rt)
                sleep(200)
            end
            local fx, fy, ft = GetAlignResidual()
            Log(string.format("[工位2-精对位] 产品#%d 贴合完成! 最终残差: ΔX=%.2fµm ΔY=%.2fµm Δθ=%.5f°",
                i, fx * 1000.0, fy * 1000.0, ft))
            sleep(300)
        end
    end
end

SelectStage("pick")
Home()
Log("==== 双工位联动流程全部完成 ====")
