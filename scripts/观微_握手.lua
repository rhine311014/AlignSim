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
--   GW_SECONDS 设了就按时间跑：检测这么多秒（连跑 1 小时用 3600），GW_COUNT 不管用
--   GW_PERIOD  两次检测之间隔多少毫秒（缺省 0，回完就发下一条）
--   GW_TIMEOUT 每条等视觉多久（毫秒，缺省 8000；要比观微自己等 PLC 取走结果的 5000 ms 长）
--   GW_WAIT    开头等观微心跳最多等多少秒（缺省 30；等不到就结束，不发指令）
--   GW_CSV     结果写到哪个文件（缺省 当前目录下 guanwei_handshake.csv）
-- 结束时日志打印汇总，同时写进 CSV 的最后一行

local nBase = GW_BASE or 100
local nShot = GW_SHOT or 1
local nRecipe = GW_RECIPE or 1
local nCount = GW_COUNT or 1000
local nSeconds = GW_SECONDS or 0
local nPeriod = GW_PERIOD or 0
-- 20261126 ZJH 缺省超时由 5000 提到 8000：原先不大于观微自己的回复超时（约 5000 ms），
-- 观微慢一拍时这边先超时、下一条又撞上迟到的回复，一次慢拍变成两三条假失败
local nTimeout = GW_TIMEOUT or 8000
local strCsv = GW_CSV or "guanwei_handshake.csv"  -- 20261123 ZJH 文件名用英文：Lua 的 io.open 按本机代码页打开，中文名会乱码

-- 20261123 ZJH 相对基址的寄存器名
local function reg(nOffset)
    return string.format("D%d", nBase + nOffset)
end

-- 20261126 ZJH “视觉没受理”的说明文字单独成常量：主循环靠它数连续几条没受理
local WHY_NOT_ACCEPTED = "视觉没受理（+0 没被清）"
local nLate = 0  -- 20261126 ZJH 清掉的迟到回复条数（上一条超时之后视觉才置的 +50）

-- 20261123 ZJH 发一条指令并等回复：返回 ok, 回显, 结果码, 回复数据, 用时 ms, 说明
-- 20261126 ZJH 加第三个参数 nParam2（可不传，不传写 0）
local function send(nCode, nParam1, nParam2)
    local t0 = TickMs()
    SetInt32(reg(4), nParam1 or 0)   -- 参数 1（32 位）
    -- 20261126 ZJH 原先不写参数 2，会带上别的脚本留在 +6/+7 的值（观微检测时把它当腔体号，超出 0~999 回 −2）；现在每条都写
    SetInt32(reg(6), nParam2 or 0)   -- 参数 2（32 位）
    SetInt16(reg(2), nCode)          -- 指令码
    SetInt16(reg(0), 1)              -- 置触发
    -- 等视觉受理（清 +0）
    while GetInt16(reg(0)) ~= 0 do
        -- 20261126 ZJH 原先这两处直接 return，+0 还是 1、+2 还是指令码：观微重连后轮询读到 +0 = 1，
        -- 就会去执行这条早就没人等的指令。现在放弃之前先把触发收回（清 +0）
        if not IsLoopValid() then
            SetInt16(reg(0), 0)          -- 脚本被停：收回触发
            return false, 0, 0, 0, 0, "脚本被停止"
        end
        if TickMs() - t0 > nTimeout then
            SetInt16(reg(0), 0)          -- 超时放弃：收回触发
            return false, 0, 0, 0, TickMs() - t0, WHY_NOT_ACCEPTED
        end
        -- 20261126 ZJH +50 是 1 而 +0 还是 1：视觉受理时先清 +50 再清 +0，所以这不是本条的回复，
        -- 是上一条超时之后才到的迟到回复。清掉 +50（观微在等 PLC 取走）、记一条日志、继续等本条受理
        -- 先读 +50 再读 +0：若 +50 是本条的回复，那时 +0 早已被清，不会误判
        if GetInt16(reg(50)) == 1 and GetInt16(reg(0)) ~= 0 then
            local nLateEcho = GetInt16(reg(52))  -- 迟到回复的回显指令码，只用来写日志
            SetInt16(reg(50), 0)
            nLate = nLate + 1
            if nLate <= 20 then
                Log(string.format("清掉一条迟到的回复（回显 %d），继续等指令 %d 受理", nLateEcho, nCode))
            end
        end
        sleep(1)
    end
    -- 等视觉触发
    while GetInt16(reg(50)) ~= 1 do
        if not IsLoopValid() then return false, 0, 0, 0, 0, "脚本被停止" end
        if TickMs() - t0 > nTimeout then
            SetInt16(reg(0), 0)          -- 20261126 ZJH 超时放弃：+0 此时本应已被视觉清掉，再清一次保证不留触发
            return false, 0, 0, 0, TickMs() - t0, "视觉触发没置 1"
        end
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

-- 20261126 ZJH 开头先把上一轮（或别的脚本）留下的东西清干净，原先什么都不清：
--   +0 触发：上一轮被停或超时留下的 1，观微一连上就会当成新指令受理
--   +50 视觉触发：上一轮没取走的迟到回复，不清的话第一条指令会把它当成自己的回复
--   +8 ~ +17 序列号：这个脚本不用，清 0 免得带上别的脚本写过的值
SetInt16(reg(0), 0)
if GetInt16(reg(50)) == 1 then
    Log(string.format("开始前 +50 是 1：上一轮留下的迟到回复（回显 %d），已清掉", GetInt16(reg(52))))
    SetInt16(reg(50), 0)
end
for nOffset = 8, 17 do
    SetInt16(reg(nOffset), 0)
end

-- 20261123 ZJH 等观微连上：心跳 +49 动起来（最多等 30 秒；观微断开后每秒重连一次）
local nBeatStart = GetInt16(reg(49))
local nStateStart = GetInt16(reg(48))  -- 20261126 ZJH 视觉状态 +48 也一起看：观微连上后每秒写 +48、+49
local tWait = TickMs()
local nWaitMs = math.floor((GW_WAIT or 30) * 1000)  -- 20261126 ZJH 等多久可用 GW_WAIT（秒）改，缺省还是 30 秒
while GetInt16(reg(49)) == nBeatStart and TickMs() - tWait < nWaitMs and IsLoopValid() do
    sleep(50)
end
-- 20261126 ZJH 原先等满 30 秒不管连没连上都往下走，之后每条指令各等满超时，上千条要 80 多分钟才结束；
-- 现在心跳 +49 和状态 +48 都没动过就认定观微没连上：记日志、关文件、直接结束
if GetInt16(reg(49)) == nBeatStart and GetInt16(reg(48)) == nStateStart then
    local strNoBeat = string.format("等了 %.0f 秒心跳（+49）没动：观微没连上或工位基址不对，脚本结束，一条指令都没发", nWaitMs / 1000)
    Log(strNoBeat)
    if file then
        file:write("# " .. strNoBeat .. "\n")
        file:close()
    end
    return
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

-- 20261126 ZJH 连续“没受理”的条数：原先中途断线后每条照样等满超时，剩下的上千条要跑 80 多分钟；
-- 现在连续 5 条都没受理就认定观微断了，提前结束并说明
local nNoAcceptMax = 5     -- 连续几条没受理就放弃
local nNoAccept = 0        -- 当前连续没受理的条数
local bGiveUp = false      -- 是否已因连续没受理而放弃
local function countNoAccept(ok, strWhy)
    if not ok and strWhy == WHY_NOT_ACCEPTED then
        nNoAccept = nNoAccept + 1    -- 又一条没受理
    else
        nNoAccept = 0                -- 受理了（哪怕结果不对）就重新数
    end
    if nNoAccept >= nNoAcceptMax then bGiveUp = true end
    return bGiveUp                   -- true = 该放弃了
end

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
    local bStop = countNoAccept(ok, strWhy)  -- 20261126 ZJH 在 strWhy 被下面改写之前先数连续没受理
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
    if bStop then break end  -- 20261126 ZJH 连续 5 条没受理：后面的指令不发了
end

-- 20261123 ZJH 检测 nCount 次
local tRun = TickMs()
local i = 0
while IsLoopValid() do
    if bGiveUp then break end  -- 20261126 ZJH 连续 5 条没受理（含上面那几条指令）：不再发检测
    -- 20261123 ZJH 按时间跑到点、或按次数跑够了
    if nSeconds > 0 then
        if TickMs() - tRun >= nSeconds * 1000 then break end
    elseif i >= nCount then
        break
    end
    i = i + 1
    local ok, nEcho, nResult, nData, nMs, strWhy = send(10, nShot)
    countNoAccept(ok, strWhy)  -- 20261126 ZJH 数连续没受理；够 5 条时 bGiveUp 置位，本条记完后在循环开头退出
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
    -- 20261123 ZJH 每 500 次写一行进度并落盘（连跑时外面能看到跑到哪了）
    if i % 500 == 0 then
        if file then
            file:write(string.format("# 进度：%d 次，回复不对或超时 %d\n", i, nFail))
            file:flush()
        end
    end
end
nCount = i

-- 20261126 ZJH 提前结束时说明原因（日志和 CSV 都写）
if bGiveUp then
    local strGiveUp = string.format("连续 %d 条指令视觉都没受理（各等了 %d ms），认定观微断开，提前结束", nNoAcceptMax, nTimeout)
    Log(strGiveUp)
    if file then file:write("# " .. strGiveUp .. "\n") end
end

-- 20261126 ZJH 汇总里加上清掉的迟到回复条数
local strSummary = string.format("汇总：检测 %d 次，OK %d，NG %d，负数码 %d，回复不对或超时 %d，迟到回复 %d，平均 %.1f ms，最慢 %d ms，心跳%s",
                                 nCount, nOk, nNg, nBad, nFail, nLate, nSumMs / math.max(1, nCount), nMaxMs,
                                 bBeat and "在涨" or "没涨")
Log(strSummary)
if file then
    file:write("# " .. strSummary .. "\n")
    file:close()
end
