-- 20261025 ZJH 对位 (FAAL=32) —— 与 FAAC 相同流程, 指令码 32

local hs = require("handshake")

local ok, nMoves, msg = hs.run{
    cmd      = 32,         -- FAAL
    moveCode = 101,
    stage    = "align",
    mode     = "axes",
    p1       = 1,
    p2       = 1,
}

local dx, dy, dt = GetAlignResidual()
Log(string.format("FAAL %s, 移动 %d 次, 残差 dX=%.4f dY=%.4f dθ=%.4f", ok and "完成" or "未完成", nMoves, dx, dy, dt))
SetFloat("D300", dx)
SetFloat("D302", dy)
SetFloat("D304", dt)
