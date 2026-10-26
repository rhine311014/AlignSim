-- 20261025 ZJH 对位重复性测试 —— 同一来料偏差反复对位 N 次, 统计残差平均 / 3σ / 最大值
-- 使用:
--   1. 「平台配置」页按真机参数开启误差注入 (如 重复定位 σ=2µm, 反向间隙 3µm)
--   2. 接视觉软件时先完成标定 (标定_FCLB_对位平台), useVision = true
--      不接视觉时 useVision = false, 结果即这台平台在"完美视觉"下的精度下限
-- 报告: logs/验证_<时间>_重复性.csv

local verify = require("verify")

verify.run{
    kind           = "repeat",
    count          = 30,                    -- 轮数
    offset         = { 0.5, -0.3, 0.2 },    -- 固定来料偏差 ΔX(mm) ΔY(mm) Δθ(°)
    tolUm          = 5.0,                   -- 合格标准 ±µm
    tolDeg         = 0.005,                 -- 合格标准 ±°
    useVision      = false,                 -- true: 走 FAAC 握手由视觉软件对位
    visionCmd      = 33,                    -- 视觉对位指令码 (FAAC=33 / FAAL=32)
    idealRotCenter = "actual",              -- 理想视觉用的旋转中心: "actual" 标定完美 / "nominal" 未标定
    maxIter        = 5,                     -- 理想视觉最多迭代次数
}
