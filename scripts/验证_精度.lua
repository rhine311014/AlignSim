-- 20261025 ZJH 对位精度测试 —— 随机来料偏差对位 N 次, 统计残差与合格率
-- 使用前提同 验证_重复性.lua
-- 报告: logs/验证_<时间>_精度.csv

local verify = require("verify")

verify.run{
    kind           = "accuracy",
    count          = 50,                    -- 轮数
    range          = { 2.0, 1.0 },          -- 随机来料偏差范围 ±mm, ±°
    tolUm          = 5.0,
    tolDeg         = 0.005,
    useVision      = false,
    visionCmd      = 33,
    idealRotCenter = "actual",
    maxIter        = 5,
}
