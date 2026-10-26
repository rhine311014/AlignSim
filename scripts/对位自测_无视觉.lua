-- 20261024 ZJH 对位闭环自测 (不需要视觉软件): 用场景真值代替视觉, 验证平台/运动学/残差计算
-- 每轮: 随机来料偏差 → 迭代 "按残差反向补偿" 直到收敛
-- 注意: 旋转中心不在 Mark 质心时, 只转角会带出平移, 所以需要多次迭代 (真实视觉对位同理)

SelectStage("align")
for nRound = 1, 3 do
    Home()
    RandomObjectOffset(2.0, 1.0)              -- ±2mm, ±1°
    local dx, dy, dt = GetAlignResidual()
    Log(string.format("第 %d 轮 初始残差 dX=%.4f dY=%.4f dθ=%.4f", nRound, dx, dy, dt))
    for nIter = 1, 8 do
        if not IsLoopValid() then return end
        SetFloat("D0", -dx)
        SetFloat("D2", -dy)
        SetFloat("D4", -dt)
        MovePoseRelFloat("D0", "D2", "D4")    -- 位姿空间相对补偿 (UVW 平台内部逆解)
        sleep(300)
        dx, dy, dt = GetAlignResidual()
        Log(string.format("  迭代 %d  dX=%.5f dY=%.5f dθ=%.5f", nIter, dx, dy, dt))
        if math.abs(dx) < 0.001 and math.abs(dy) < 0.001 and math.abs(dt) < 0.001 then
            Log("  收敛")
            break
        end
    end
    sleep(800)
end
