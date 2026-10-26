-- 20261025 ZJH 大视野拾取头标定 (FCLB=10) —— 拾取头带标定 Mark, 视觉下发走点, 大视野相机拍 Mark 做手眼标定
-- 使用前:
--   1. 「大视野拾取」页勾选「固定比例」, 弹出视图到相机前显示器并 F11 全屏
--   2. 标定 Mark 相对吸嘴的偏移不要为 0 (默认 X=20mm), 否则旋转标定求不出旋转中心
--   3. 运行本脚本 (自动显示标定 Mark、隐藏产品), 再在视觉软件里点"标定"
-- 场景号 p1=2 与对位平台标定区分, 按视觉软件实际场景修改

local hs = require("handshake")

SelectStage("pick")
-- 20261026 ZJH 把拾取头移到使【标定 Mark】正好落在视野正中央 (W/2, H/2) 的位置
CenterPickCalibMark()
ShowPickCalibMark(true)
ShowProducts(false)

-- 20261025 ZJH pcall 包裹: 脚本被停止 (抛 "script stopped") 时仍能执行下面的显示恢复
local bRunOk, ok, nMoves, msg = pcall(hs.run, {
    cmd       = 10,
    moveCode  = 101,
    stage     = "pick",
    mode      = "axes",
    p1        = 2,          -- 场景号 (占位)
    p2        = 1,
    record    = true,
    name      = "FCLB_拾取头",
    timeoutMs = 60000,
})

-- 20261025 ZJH 恢复显示 (被停止时也执行, 避免产品一直隐藏; 若仍未恢复, 在页面上勾回「显示产品」)
ShowProducts(true)
ShowPickCalibMark(false)
if not bRunOk then
    Log("拾取头标定中断: " .. tostring(ok))
    return
end
Home()
Log(ok and ("拾取头标定完成, 共 " .. nMoves .. " 个点") or ("拾取头标定未完成: " .. msg))
