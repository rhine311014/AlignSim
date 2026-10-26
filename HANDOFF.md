# HANDOFF - 会话遗留工作

> 保存时间: 2026-10-25 16:56:00
> 触发原因: 里程碑 (误差注入 + 重复性/精度验证完成)

## 0. 最新进展 (16:56)
- [x] 平台误差注入 (Stage 指令/实际双位置, `src/motion/StageErrorModel.h`), test_stage 9 例
- [x] `scripts/lib/verify.lua` + `验证_重复性/验证_精度`: 视觉握手或理想视觉, CSV 统计报告; ctest 5/5
- [ ] 待做: 全视野网格验证 (需视觉"只检测"指令); 到位/忙信号、行程限位、故障注入; 记录回放

## 1. 当前任务主题
AlignSim 配合视觉软件 + 相机拍屏完成收发数据与标定 (FCLB/FAAC/FAAL, 对位平台 + 大视野拾取头)

## 2. 已完成的关键步骤
- [x] 固定 px/mm 拍屏显示 (对位相机视图 / 拾取视图), 配置持久化
- [x] 拾取头标定 Mark (偏移可配, 默认 X=20mm), 标定时隐藏产品
- [x] 握手公共库 `scripts/lib/handshake.lua` + 标定/对位脚本; FCLB 被误当对位的问题已按指令码拆分
- [x] 标定记录 CSV (logs 目录) + 标定真值面板
- [x] ctest 4/4 (Lua 11 例含完整 FCLB 模拟视觉); TCP 端到端: XYD / UVW / 拾取头 FCLB 11 点全过

## 3. 未完成的工作 (重要!)
- [ ] 真实视觉软件 + 相机现场联调 (按 `联调说明.md`)
- [ ] 拾取指令码 (scripts/大视野拾取.lua 的 20/21) 与拾取头标定场景号 p1=2 按视觉软件实际修改
- [ ] 大视野拾取.lua 仍用自带简易握手, 可改为 handshake 库风格 (视觉协议确认后)

## 4. 关键文件路径
- `scripts/lib/handshake.lua`: 握手公共库 (地址表 DEFAULT_REGS)
- `scripts/标定_FCLB_*.lua`, `scripts/对位_FAA*.lua`: 标定/对位入口
- `src/sim/CalibRecorder.*`: CSV 记录
- `src/script/LuaEngine.cpp`: Lua 函数 + require 搜索器
- `联调说明.md`: 现场操作步骤与排查清单

## 5. 下次会话应该做什么 (关键!)
1. 回归: `cmake --build build --config Debug && cd build && ctest -C Debug`
2. 根据现场联调反馈 (超时/回复码/坐标方向) 调整 handshake.lua 参数或寄存器表
