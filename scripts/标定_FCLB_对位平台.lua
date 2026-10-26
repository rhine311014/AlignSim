-- 20261025 ZJH 对位平台标定 (FCLB=10) —— 视觉逐点下发 9 点平移 + 旋转点, 模拟器移动并回复
-- 使用前:
--   1. 「对位模拟」页勾选「相机视图固定比例」, 把相机视图弹出到相机前的显示器并 F11 全屏, 之后不要再改比例/窗口
--   2. 「平台配置」页确认对位平台轴型 (XYD/UVW) 与 Int32 单位与视觉软件一致
--   3. 运行本脚本, 再在视觉软件里点"标定"
-- 每个点记录到 <exe目录>/logs/标定_*_FCLB_对位平台.csv

local hs = require("handshake")

-- 20261026 ZJH 标定时自动显示对位平台专用标定 Mark 并移到各相机视野中央 (隐藏目标/对象 Mark, 防止重合或多 Mark 干扰视觉识别)
SelectStage("align")
ShowAlignCalibMark(true)
ShowAlignTarget(false)
ShowAlignObject(false)
CenterAlignCalibMark()

local bRunOk, ok, nMoves, msg = pcall(hs.run, {
    cmd       = 10,        -- FCLB 标定
    moveCode  = 101,       -- 视觉"继续移动"回复码
    stage     = "align",
    mode      = "axes",    -- 视觉下发轴值 (XYD: X Y D / UVW: U V W); 若视觉下发 X Y θ 改 "pose"
    p1        = 1,         -- 场景号
    p2        = 1,         -- 相机组 / 参数 2
    record    = true,
    name      = "FCLB_对位平台",
    timeoutMs = 60000,     -- 标定每点视觉可能要拍多张, 给足时间
})

ShowAlignCalibMark(false)
ShowAlignTarget(true)
ShowAlignObject(true)
if not bRunOk then
    Log("对位平台标定中断: " .. tostring(ok))
    return
end
Home()

if ok then
    Log("对位平台标定完成, 共 " .. nMoves .. " 个点; 请在「对位模拟」页「标定真值」对照视觉结果")
else
    Log("对位平台标定未完成: " .. msg)
end
