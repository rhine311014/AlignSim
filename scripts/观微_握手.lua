-- 20261123 ZJH 观微（Guanwei）握手联调脚本：AlignSim 扮演 PLC，按观微的寄存器表发指令、核对回复
--
-- 观微的寄存器表（docs/05-P2-通讯与相机.md 第 3 节），每个工位占 100 个字，偏移相对工位基址：
--   PLC 写：+0 触发（1 = 有指令）、+2 指令码、+4/+5 参数 1（32 位，低字在前）、+6/+7 参数 2
--   视觉写：+48 视觉状态（1 运行、2 停止、3 退出）、+49 心跳计数、+50 视觉触发、+52 回显指令码、+54 结果码、
--           +56/+57 回复数据（检测回拍照号、查品种回品种号）、+60 起判定项（每项 32 位 ×10^n）
-- 握手：PLC 写参数和指令码、置 +0 = 1 → 视觉清 +0（受理）→ 视觉写数据、回显、结果码，最后置 +50 = 1 → PLC 读完清 +50
-- 指令：1 复位、5 换品种、6 查当前品种、10 检测；别的码视觉回 −9
--
-- 可改的全局变量（在脚本页或调用前设）：
--   GW_BASE    工位基址（缺省 100）
--   GW_SHOT    检测的拍照号（缺省 1）
--   GW_RECIPE  换品种用的品种号（缺省 1）
--   GW_COUNT   检测次数（缺省 1000）
--   GW_PERIOD  两次检测之间隔多少毫秒（缺省 0，回完就发下一条）
--   GW_TIMEOUT 每条等视觉多久（毫秒，缺省 5000）
--   GW_CSV     结果写到哪个文件（缺省 当前目录下 guanwei_handshake.csv）
-- 结束时日志打印汇总，同时写进 CSV 的最后一行

local nBase = GW_BASE or 100
local nShot = GW_SHOT or 1
local nRecipe = GW_RECIPE or 1
local nCount = GW_COUNT or 1000
local nPeriod = GW_PERIOD or 0
local nTimeout = GW_TIMEOUT or 5000
local strCsv = GW_CSV or "guanwei_handshake.csv"  -- 20261123 ZJH 文件名用英文：Lua 的 io.open 按本机代码页打开，中文名会乱码

-- 20261123 ZJH 相对基址的寄存器名
local function reg(nOffset)
    return string.format("D%d", nBase + nOffset)
end

-- 20261123 ZJH 发一条指令并等回复：返回 ok, 回显, 结果码, 回复数据, 用时 ms, 说明
local function send(nCode, nParam1)
    local t0 = TickMs()
    SetInt32(reg(4), nParam1 or 0)   -- 参数 1（32 位）
    SetInt16(reg(2), nCode)          -- 指令码
    SetInt16(reg(0), 1)              -- 置触发
    -- 等视觉受理（清 +0）
    while GetInt16(reg(0)) ~= 0 do
        if not IsLoopValid() then return false, 0, 0, 0, 0, "脚本被停止" end
        if TickMs() - t0 > nTimeout then return false, 0, 0, 0, TickMs() - t0, "视觉没受理（+0 没被清）" end
        sleep(1)
    end
    -- 等视觉触发
    while GetInt16(reg(50)) ~= 1 do
        if not IsLoopValid() then return false, 0, 0, 0, 0, "脚本被停止" end
        if TickMs() - t0 > nTimeout then return false, 0, 0, 0, TickMs() - t0, "视觉触发没置 1" end
        sleep(1)
    end
    local nEcho = GetInt16(reg(52))
    local nResult = GetInt16(reg(54))
    local nData = GetInt32(reg(56))
    SetInt16(reg(50), 0)             -- 取走：清视觉触发
    local nMs = TickMs() - t0
    if nEcho ~= nCode then
        return false, nEcho, nResult, nData, nMs, string.format("回显 %d 和发的 %d 对不上", nEcho, nCode)
    end
    return true, nEcho, nResult, nData, nMs, ""
end

local file = io.open(strCsv, "w")
if file then file:write("序号,指令,参数,回显,结果码,回复数据,用时ms,说明\n") end
local function record(nIndex, nCode, nParam, ok, nEcho, nResult, nData, nMs, strWhy)
    if file then
        file:write(string.format("%d,%d,%d,%d,%d,%d,%d,%s\n", nIndex, nCode, nParam or 0, nEcho, nResult, nData, nMs,
                                 ok and "" or strWhy))
    end
end

Log(string.format("观微握手联调开始：基址 D%d，拍照号 %d，检测 %d 次", nBase, nShot, nCount))

-- 20261123 ZJH 等观微连上：心跳 +49 动起来（最多等 30 秒；观微断开后每秒重连一次）
local nBeatStart = GetInt16(reg(49))
local tWait = TickMs()
while GetInt16(reg(49)) == nBeatStart and TickMs() - tWait < 30000 and IsLoopValid() do
    sleep(50)
end
-- 20261123 ZJH 心跳：+49 在涨、+48 是运行
local nBeat0 = GetInt16(reg(49))
sleep(1500)
local nBeat1 = GetInt16(reg(49))
local bBeat = nBeat1 ~= nBeat0
local nState = GetInt16(reg(48))
Log(string.format("心跳 %d → %d（%s），视觉状态 %d（1 运行）", nBeat0, nBeat1, bBeat and "在涨" or "没涨", nState))

local nFail = 0       -- 回复对不上或超时的条数
local nOk, nNg, nBad = 0, 0, 0  -- 检测 OK、NG、负数码
local nMaxMs, nSumMs = 0, 0

-- 20261123 ZJH 先走一遍别的指令：复位、换品种、查品种、不认识的码
local tChecks = {
    { 1, 0, 1, "复位回 1" },
    { 5, nRecipe, 1, "换品种回 1" },
    { 6, 0, 1, "查当前品种回 1" },
    { 5, 99999, -2, "换到不存在的品种回 −2" },
    { 10, 0, -2, "检测拍照号 0 回 −2" },
    { 77, 0, -9, "不认识的指令回 −9" },
}
for i, t in ipairs(tChecks) do
    local ok, nEcho, nResult, nData, nMs, strWhy = send(t[1], t[2])
    if ok and nResult ~= t[3] then
        ok = false
        strWhy = string.format("%s，实际回 %d", t[4], nResult)
    end
    if t[1] == 6 and ok and nData ~= nRecipe then
        ok = false
        strWhy = string.format("查当前品种回 %d，应为 %d", nData, nRecipe)
    end
    record(-i, t[1], t[2], ok, nEcho, nResult, nData, nMs, strWhy)
    if not ok then
        nFail = nFail + 1
        Log("不对：" .. strWhy)
    end
end

-- 20261123 ZJH 检测 nCount 次
for i = 1, nCount do
    if not IsLoopValid() then break end
    local ok, nEcho, nResult, nData, nMs, strWhy = send(10, nShot)
    if ok and nData ~= nShot then
        ok = false
        strWhy = string.format("回复数据 %d 应为拍照号 %d", nData, nShot)
    end
    record(i, 10, nShot, ok, nEcho, nResult, nData, nMs, strWhy)
    if not ok then
        nFail = nFail + 1
        if nFail <= 20 then Log(string.format("第 %d 次不对：%s", i, strWhy)) end
    elseif nResult == 1 then
        nOk = nOk + 1
    elseif nResult == 2 then
        nNg = nNg + 1
    else
        nBad = nBad + 1
    end
    nSumMs = nSumMs + nMs
    if nMs > nMaxMs then nMaxMs = nMs end
    if nPeriod > 0 then sleep(nPeriod) end
end

local strSummary = string.format("汇总：检测 %d 次，OK %d，NG %d，负数码 %d，回复不对或超时 %d，平均 %.1f ms，最慢 %d ms，心跳%s",
                                 nCount, nOk, nNg, nBad, nFail, nSumMs / math.max(1, nCount), nMaxMs,
                                 bBeat and "在涨" or "没涨")
Log(strSummary)
if file then
    file:write("# " .. strSummary .. "\n")
    file:close()
end
