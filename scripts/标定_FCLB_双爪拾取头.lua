-- 20261026 ZJH FCLB 双爪拾取头手眼标定 (指令码 1: 先后标定 1号爪与 2号爪)
-- 流程:
--   第 1 轮 (D106=1): 标定 Mark 挂载在 1号爪上 → 居中到视野中央 → 走完 9点+旋转标定
--   第 2 轮 (D106=2): 标定 Mark 挂载在 2号爪上 → 居中到视野中央 → 走完 9点+旋转标定
local hs = require("handshake")

SelectStage("pick")
ShowProducts(false)
ShowPickCalibMark(true)

for nNozzle = 1, 2 do
    if not IsLoopValid() then break end
    Log(string.format("==== 开始标定 %d 号爪 ====", nNozzle))
    SetInt32("D106", nNozzle)
    SetPickCalibMount(nNozzle)      -- 将标定 Mark 挂到第 nNozzle 号爪上
    CenterPickCalibMark()           -- 自动移到视野正中央 (W/2, H/2)
    sleep(200)
    hs.run({
        name    = string.format("FCLB_拾取头_%d号爪标定", nNozzle),
        stage   = "pick",
        cmd     = hs.CMD_FCLB,
        scene   = nNozzle,
        record  = true,
        resetHome = false,
    })
end

ShowPickCalibMark(false)
ShowProducts(true)
SetPickCalibMount(0)
Home()
