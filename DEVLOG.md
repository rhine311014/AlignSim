# 开发记录

## [2026-10-26]

### 11:18 - 吉嘉 G+ 真实 UVW 运动学对齐、Modbus TCP 与全项优化
- **修改文件**: `src/motion/StageKinematics.h/.cpp`, `src/motion/Stage.cpp`, `src/gui/StageConfigWidget.h/.cpp`, `src/comm/ModbusTcpProtocol.h/.cpp`, `src/comm/ProtocolFactory.h/.cpp`, `src/comm/PlcServer.cpp`, `src/sim/AlignScene.h/.cpp`, `src/sim/PickScene.h/.cpp`, `src/gui/AlignView.h/.cpp`, `src/gui/AlignPage.h/.cpp`, `src/gui/PickView.h/.cpp`, `src/gui/PickPage.h/.cpp`, `src/gui/RegisterTableWidget.cpp`, `src/core/AppConfig.cpp`, `src/script/LuaEngine.h/.cpp`, `scripts/标定_FCLB_双爪拾取头.lua`, `scripts/拾取自测_无视觉.lua`, `scripts/大视野拾取.lua`, `tests/test_kinematics.cpp`, `tests/test_protocols.cpp`
- **修改类型**: 新增/优化
- **修改内容**:
  1. **吉嘉 G+ (`XXY250-90-250-SV-X`) 真实 UVW 平台对齐**: 严格按说明书第 5~7 页实现 `(Ux - at, Uy - bt)` 力臂与 `Y1(+90°)/X(180°)/Y2(-90°)` 轴向投影, 正解增加阻尼步长回退保证大角度 100% 收敛; 平台配置页内置 `G+ XXY250 (R=85mm)` 与 `G+ 说明书3-4示例 (25.456)` 预设、精度规格一键填入 (`±2.5µm/7″, 间隙8µm/12″`)、相对位姿步进 (`X'/Y'/θ'`) 与单步马达位移 `ΔU/ΔV/ΔW` 及 `±4mm/±5mm` 限位提示; 单元测试逐项核对说明书第 7 页全部 5 步数值 (误差 `< 1e-5 mm`).
  2. **Modbus TCP 协议完整实现**: 支持功能码 `0x03/0x04` 读多字、`0x06` 写单字、`0x10` 写多字、异常码与半包/粘包切帧.
  3. **光学退化模拟与虚拟存图**: 对位与拾取视图支持光照渐变/散斑噪点 (`0~100%`)、边缘微小缺口瑕疵模拟, 以及一键渲染保存 PNG 拍屏图到 `logs/`.
  4. **双爪独立挂载标定与码盘/抛料**: 支持标定 Mark 切换挂载在 `旋转中心 / 1号爪 / 2号爪` (`SetPickCalibMount`), 新增 `标定_FCLB_双爪拾取头.lua`; 拾取脚本支持 `2×5` 阵列料盘码放与反面 NG 独立抛料位.
  5. **视图交互与寄存器协议注释**: 拾取视图支持左键拖拽产品、双击翻转正反、右键直接移动当前吸嘴到点击处; 对位视图支持滚轮缩放与拖拽平移; 寄存器表新增协议中文说明列、快捷跳转按钮 (`D0/D100/D200/D300`) 与非零行高亮.
- **关联功能**: 真实 UVW 运动学 / Modbus TCP / 视觉仿真与交互

### 10:38 - 双爪先后拾取模式、标定 Mark 居中与目标/对象独立显示
- **修改文件**: `src/sim/PickScene.h/.cpp`, `src/gui/PickView.cpp`, `src/gui/PickPage.h/.cpp`, `src/gui/AlignPage.h/.cpp`, `src/gui/AlignView.cpp`, `src/core/AppConfig.cpp`, `src/script/LuaEngine.h/.cpp`, `scripts/大视野拾取.lua`, `scripts/拾取自测_无视觉.lua`, `scripts/标定_FCLB_对位平台.lua`, `scripts/标定_FCLB_拾取头.lua`, `tests/test_scenes.cpp`
- **修改类型**: 新增/优化
- **修改内容**:
  1. **双爪先后拾取 (Mode A)**: 同一拾取头支持搭载 1号爪(红) 与 2号爪(蓝), 可配各自相对旋转中心的偏移 (`dNozzle1OffX/Y`, `dNozzle2OffX/Y`); 1号爪先吸产品 1, 随后拾取头平移+旋转将 2号爪对准产品 2 吸取 (期间产品 1 随 1号爪刚性跟随平移与旋转), 最后两件一起移到放料位放下; 新增 Lua `GetNozzleCount` / `GetNozzleOffset` / `GetNozzlePose` / `MoveNozzleTo` 及 `PickAt(nozzle)` / `PlaceAt(nozzle)` / `IsHolding(nozzle)`.
  2. **标定 Mark 视野正中央**: 新增 `PickScene::moveCalibMarkToFieldCenter()` 与 Lua `CenterPickCalibMark()`, 勾选「显示标定Mark」或点击「Mark移到视野中央」时自动补偿偏移使标定 Mark 精确落在 `(W/2, H/2)`.
  3. **正面小丝印字符 / 反面无字符 + 目标/对象单独显示**: 反面产品不绘制字符 (模拟背面无丝印), 正面字符改为小字高 (`3.5mm` 可调); 对位页新增「显示目标(黑)」与「显示对象(蓝)」独立开关及 Lua `ShowAlignTarget` / `ShowAlignObject`.
- **关联功能**: 双爪拾取 / 手眼标定 / 产品正反测试

### 09:40 - Mark 大小调节与拾取产品字符/正反面 + 性能与超时优化
- **修改文件**: `src/sim/AlignScene.h`, `src/sim/PickScene.h/.cpp`, `src/gui/AlignPage.h/.cpp`, `src/gui/AlignView.cpp`, `src/gui/PickPage.h/.cpp`, `src/gui/PickView.cpp`, `src/gui/MainWindow.h/.cpp`, `src/gui/RegisterTableWidget.h/.cpp`, `src/comm/PlcServer.h/.cpp`, `src/sim/CalibRecorder.cpp`, `src/script/LuaEngine.h/.cpp`, `src/core/AppConfig.cpp`, `scripts/lib/handshake.lua`, `scripts/大视野拾取.lua`, `scripts/拾取自测_无视觉.lua`, `tests/test_scenes.cpp`
- **修改类型**: 新增/优化
- **修改内容**: 对位页将 Mark 外观/大小/线宽提至顶部即时调节, Mark 表支持单对独立尺寸; 拾取页标定 Mark 新增线宽调节; 拾取产品支持可配表面丝印字符 (默认 `TOP`) 与正反面状态 (反面时字符与特征点镜像翻转, 拾取反面判 NG 并统计, Lua 新增 `SetProductFlipped` 及 `GetProductPose/PickAt` 正反返回值); 新增 `TickMs` 单调计时修复 `sleep(10)` 粒度导致超时翻倍; 通信日志 50ms 批量纯文本写入 + 关闭报文时跳过格式化; 隐藏页签延迟刷新
- **关联功能**: Mark 尺寸调整 / 拾取产品正反测试 / 性能与稳定性

## [2026-10-25]

### 16:55 - 重复性/精度验证脚本与报告
- **修改文件**: `scripts/lib/verify.lua`, `scripts/验证_重复性.lua`, `scripts/验证_精度.lua`, `src/script/LuaEngine.h/.cpp`, `src/sim/CalibRecorder.h/.cpp`, `tests/test_lua.cpp`, `联调说明.md`
- **修改类型**: 新增
- **修改内容**: Lua `GetActualPose/MovePoseBy/GetRotCenter/GetAlignCentroids/CsvBegin/CsvRow/CsvEnd`; 脚本移动按误差模型速度插补 + 整定; 验证库支持视觉握手与理想视觉 (actual/nominal 旋转中心) 两种对位, 输出逐轮明细与 平均/σ/3σ/最大/合格率 统计 (CSV + 日志 + D310~D318)
- **关联功能**: 对位精度验证

### 16:50 - 误差注入界面与持久化
- **修改文件**: `src/gui/StageConfigWidget.h/.cpp`, `src/gui/MainWindow.cpp`, `src/gui/AlignPage.cpp`, `src/core/AppConfig.h/.cpp`
- **修改类型**: 新增
- **修改内容**: 平台配置页误差注入分组 (勾选即开启); 状态区显示指令/实际位姿及差值; 标定真值显示实际旋转中心与偏差; 平台配置页加滚动区; 误差模型写入配置
- **关联功能**: 误差注入

### 16:47 - 平台误差模型 (指令/实际双位置)
- **修改文件**: `src/motion/StageErrorModel.h`, `src/motion/Stage.h/.cpp`, `src/sim/PickScene.cpp`, `src/gui/AlignView.cpp`, `CMakeLists.txt`, `tests/test_stage.cpp`
- **修改类型**: 新增/重构
- **修改内容**: Stage 维护指令轴值/位姿 (回写视觉) 与实际轴值/位姿 (显示与真值); 误差项: 重复定位高斯误差、反向间隙迟滞、比例 ppm、XYD 垂直度、实际旋转中心偏差、UVW 安装点偏差, 带种子可复现; 位姿移动按标称运动学逆解
- **关联功能**: 误差注入

### 15:52 - 联调说明文档
- **修改文件**: `联调说明.md`
- **修改类型**: 新增
- **修改内容**: 角色分工、握手地址表、拍屏准备、对位平台/拾取头标定与对位操作步骤、排查清单
- **关联功能**: 视觉软件 + 相机联调

### 15:49 - 拾取头标定起点移到视野中心
- **修改文件**: `src/script/LuaEngine.h/.cpp`, `scripts/标定_FCLB_拾取头.lua`
- **修改类型**: 修改
- **修改内容**: 新增 Lua `GetFieldSize()`; 拾取头标定开始时移到视野中心 (原点在视野角上, 标定 Mark 被裁掉一半)
- **关联功能**: 大视野拾取头标定

### 15:45 - 握手公共库与标定/对位脚本
- **修改文件**: `scripts/lib/handshake.lua`, `scripts/标定_FCLB_对位平台.lua`, `scripts/标定_FCLB_拾取头.lua`, `scripts/对位_FAAC.lua`, `scripts/对位_FAAL.lua`; 删除 `scripts/对位_XYD.lua`, `scripts/对位_UVW.lua`
- **修改类型**: 新增/删除
- **修改内容**: 参考工程 FCLB/FAAC/FAAL 共用流程封装为 `hs.run{}` (地址可覆盖、超时/异常码处理、可选逐点记录); 原脚本把 FCLB(标定) 误当对位, 按指令码重新拆分
- **关联功能**: 标定 / 对位

### 15:40 - 标定记录与 Lua 新函数
- **修改文件**: `src/sim/CalibRecorder.h/.cpp`, `src/script/LuaEngine.h/.cpp`, `src/gui/MainWindow.cpp`, `CMakeLists.txt`, `tests/test_lua.cpp`
- **修改类型**: 新增
- **修改内容**: CSV 逐点记录 (轴值/位姿/Mark 世界坐标与屏幕像素, UTF-8 BOM); Lua `CalibBegin/CalibRecord/CalibEnd/ShowPickCalibMark/ShowProducts`; 自定义 require 搜索器 (QFile 读取, 支持中文路径); 测试: C++ 模拟视觉跑完整 FCLB、超时、异常码
- **关联功能**: 标定记录

### 15:30 - 拍屏固定比例、拾取头标定 Mark、标定真值
- **修改文件**: `src/motion/StageKinematics.*`, `src/sim/AlignScene.*`, `src/sim/PickScene.*`, `src/gui/AlignView.*`, `src/gui/AlignPage.*`, `src/gui/PickView.*`, `src/gui/PickPage.*`, `src/core/AppConfig.*`, `src/gui/MainWindow.cpp`, `tests/test_scenes.cpp`
- **修改类型**: 新增/重构
- **修改内容**: 显示配置 (固定 px/mm、Y 轴方向) 下沉到场景, 视图从场景读取; 相机/拾取视图固定比例渲染; 拾取头标定 Mark (可配图形/尺寸/偏移); 对位页与拾取页新增标定真值; 配置持久化
- **关联功能**: 相机拍屏标定

## [2026-10-24]

### 14:16 - 补完 UVW 正解牛顿迭代
- **修改文件**: `src/motion/StageKinematics.cpp`, `tests/test_kinematics.cpp`
- **修改类型**: 修改
- **修改内容**: `UvwKinematics::axesToPose` 由一次线性解改为牛顿迭代 (最多 20 次, 轴值残差 < 1e-9mm 判收敛, 不收敛/奇异返回 bOk=false); 新增 20° 往返用例; 全部 4 个测试套件通过
- **关联功能**: UVW 平台位姿显示与对位残差

### 13:00 - 修复退出时 Qt 断言
- **修改文件**: `src/gui/MainWindow.cpp`
- **修改类型**: 修改
- **修改内容**: `~MainWindow` 先停止 PlcServer 并断开其信号; 原先子对象析构时 `PlcServer::stop()` 发信号到已析构的 MainWindow, 触发 "class destructor may have already run" 断言, 进程挂在 CRT 对话框
- **关联功能**: 程序退出

### 12:58 - 新增命令行自动运行与截图
- **修改文件**: `src/main.cpp`, `src/gui/MainWindow.h/.cpp`
- **修改类型**: 新增
- **修改内容**: `--run <脚本名>` 启动后自动运行脚本; `--grab <目录> [延时ms]` 截取所有页签后退出
- **关联功能**: 产线自动化 / 界面自检

### 12:50 - 默认 Lua 脚本
- **修改文件**: `scripts/对位_XYD.lua`, `scripts/对位_UVW.lua`, `scripts/大视野拾取.lua`, `scripts/对位自测_无视觉.lua`, `scripts/拾取自测_无视觉.lua`
- **修改类型**: 新增
- **修改内容**: 对位沿用参考工程 FCLB 握手 (D100/D102/D150/D152/D154); 拾取指令码为占位值; 两个自测脚本无需视觉软件即可跑闭环
- **关联功能**: 脚本驱动流程

### 12:45 - 界面 (P2/P3)
- **修改文件**: `src/gui/*`
- **修改类型**: 新增
- **修改内容**: 主窗口 (通信栏 + 5 个页签 + 日志)、寄存器表、对位页 (单/多相机多 Mark、可弹出相机窗口全屏拍屏)、拾取页 (产品外形/数量/视野可配、拾取误差与统计)、脚本页、平台配置页 (XYD/UVW 各自独立配置)
- **关联功能**: 界面

### 12:35 - Lua 引擎与配置持久化
- **修改文件**: `src/script/LuaEngine.h/.cpp`, `src/core/AppConfig.h/.cpp`, `tests/test_lua.cpp`
- **修改类型**: 新增
- **修改内容**: 每脚本独立 lua_State + 线程池; 指令计数钩子可打断死循环; 兼容参考工程函数名并新增位姿/对位/拾取函数; JSON 配置 (QSaveFile 原子写)
- **关联功能**: 脚本 / 配置

### 12:25 - 对位与拾取场景
- **修改文件**: `src/sim/AlignScene.h/.cpp`, `src/sim/PickScene.h/.cpp`, `tests/test_scenes.cpp`
- **修改类型**: 新增
- **修改内容**: 对位: 相机视窗 + Mark 对 + 来料偏差, 多 Mark 用 Procrustes 拟合整体残差; 拾取: 随机摆放、命中判定、拾取误差、产品随拾取头运动、统计
- **关联功能**: 对位模拟 / 大视野拾取

### 12:15 - 平台运动学 (XYD/UVW)
- **修改文件**: `src/motion/StageKinematics.h/.cpp`, `src/motion/Stage.h/.cpp`, `tests/test_kinematics.cpp`
- **修改类型**: 新增
- **修改内容**: XYD 恒等映射; UVW 逆解闭式、正解牛顿法 (迭代部分留 TODO 待补); 旋转中心可配; Stage 线程安全
- **关联功能**: 轴型 UVW / XYD

### 12:05 - 通信层 (P1)
- **修改文件**: `src/core/RegisterBank.*`, `src/comm/*`, `tests/test_protocols.cpp`
- **修改类型**: 新增
- **修改内容**: MC 3E 二进制 / MC 3E ASCII / 基恩士上位链路 / KV-MC 兼容; 每连接接收缓冲处理半包粘包; 非法报文按协议回错误码
- **关联功能**: PLC 通信

### 12:00 - 工程骨架
- **修改文件**: `CMakeLists.txt`, `.gitignore`, `third_party/lua/*`
- **修改类型**: 新增
- **修改内容**: Qt6 + CMake 工程, 复用参考工程预编译 Lua 5.4 (ScriptRunner.dll, MIT)
- **关联功能**: 构建
