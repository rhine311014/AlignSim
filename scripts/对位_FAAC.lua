-- 20261025 ZJH 对位 (FAAC=33) —— 流程与标定一致, 只换指令码
-- 先用「对位模拟」页的"随机偏差"模拟来料偏差, 再运行本脚本; 视觉拍照算出补偿后下发目标位置
-- 结束时打印模拟器算出的真实残差, 用于评估对位精度

local hs = require("handshake")

local dx0, dy0, dt0 = GetAlignResidual()
Log(string.format("对位前残差 dX=%.4f dY=%.4f dθ=%.4f", dx0, dy0, dt0))

local ok, nMoves, msg = hs.run{
    cmd      = 33,         -- FAAC
    moveCode = 101,
    stage    = "align",
    mode     = "axes",
    p1       = 1,
    p2       = 1,
}

local dx, dy, dt = GetAlignResidual()
Log(string.format("对位%s, 移动 %d 次, 残差 dX=%.4f dY=%.4f dθ=%.4f", ok and "完成" or "未完成", nMoves, dx, dy, dt))
-- 20261025 ZJH 残差回写 (Float), 便于上位机/视觉读取验证: D300 dX, D302 dY, D304 dθ
SetFloat("D300", dx)
SetFloat("D302", dy)
SetFloat("D304", dt)
