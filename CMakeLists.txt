# 20261024 ZJH 大视野拾取 / 对位模拟工具 (AlignSim) 顶层构建脚本
# 结构: alignsim_core(通信+运动+场景, 无界面) → alignsim_script(Lua) → AlignSim(界面 exe)
cmake_minimum_required(VERSION 3.21)
project(AlignSim VERSION 1.0.0 LANGUAGES CXX)

# 20261024 ZJH C++17 标准, 与参考工程一致
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 20261024 ZJH Qt 元对象编译自动化 (Q_OBJECT 类自动跑 moc)
set(CMAKE_AUTOMOC ON)

# 20261024 ZJH 所有可执行文件和 dll 输出到同一目录, 运行时能找到 ScriptRunner.dll
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 20261024 ZJH MSVC: 源文件按 UTF-8 编译 (中文注释/字符串), 关闭不安全 CRT 警告
if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)

# 20261024 ZJH Lua 5.4 预编译库 (复用参考工程 ScriptRunner.dll, MIT 许可)
add_library(lua_runtime SHARED IMPORTED)
set_target_properties(lua_runtime PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/lua/lib/ScriptRunner.dll"
    IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/lua/lib/ScriptRunner.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/lua/include"
    INTERFACE_COMPILE_DEFINITIONS "LUA_BUILD_AS_DLL")

# 20261024 ZJH 核心库: 寄存器 / 协议 / TCP 服务 / 运动学 / 场景 (不依赖 Widgets, 方便单元测试)
add_library(alignsim_core STATIC
    src/core/RegisterBank.h        src/core/RegisterBank.cpp
    src/comm/IPlcProtocol.h
    src/comm/McBinaryProtocol.h    src/comm/McBinaryProtocol.cpp
    src/comm/McAsciiProtocol.h     src/comm/McAsciiProtocol.cpp
    src/comm/KeyenceLinkProtocol.h src/comm/KeyenceLinkProtocol.cpp
    src/comm/ModbusTcpProtocol.h   src/comm/ModbusTcpProtocol.cpp
    src/comm/ProtocolFactory.h     src/comm/ProtocolFactory.cpp
    src/comm/PlcServer.h           src/comm/PlcServer.cpp
    src/motion/StageKinematics.h   src/motion/StageKinematics.cpp
    src/motion/StageErrorModel.h
    src/motion/Stage.h             src/motion/Stage.cpp
    src/sim/AlignScene.h           src/sim/AlignScene.cpp
    src/sim/PickScene.h            src/sim/PickScene.cpp
    src/sim/CalibRecorder.h        src/sim/CalibRecorder.cpp
    src/core/AppConfig.h           src/core/AppConfig.cpp
)
target_include_directories(alignsim_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(alignsim_core PUBLIC Qt6::Core Qt6::Network)

# 20261024 ZJH 脚本库: Lua 引擎与函数绑定
add_library(alignsim_script STATIC
    src/script/LuaEngine.h src/script/LuaEngine.cpp
)
target_link_libraries(alignsim_script PUBLIC alignsim_core lua_runtime)

# 20261024 ZJH 界面程序
add_executable(AlignSim WIN32
    src/main.cpp
    src/gui/MainWindow.h          src/gui/MainWindow.cpp
    src/gui/RegisterTableWidget.h src/gui/RegisterTableWidget.cpp
    src/gui/StageConfigWidget.h   src/gui/StageConfigWidget.cpp
    src/gui/AlignView.h           src/gui/AlignView.cpp
    src/gui/AlignPage.h           src/gui/AlignPage.cpp
    src/gui/PickView.h            src/gui/PickView.cpp
    src/gui/PickPage.h            src/gui/PickPage.cpp
    src/gui/ScriptPage.h          src/gui/ScriptPage.cpp
)
target_link_libraries(AlignSim PRIVATE alignsim_script Qt6::Widgets)

# 20261024 ZJH 构建后: 拷贝 Lua dll 与默认脚本到输出目录
add_custom_command(TARGET AlignSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/lua/lib/ScriptRunner.dll" "$<TARGET_FILE_DIR:AlignSim>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scripts" "$<TARGET_FILE_DIR:AlignSim>/scripts")

# 20261024 ZJH 单元测试 (Qt Test, 正式回归套件)
enable_testing()
foreach(testName test_protocols test_kinematics test_stage test_scenes test_lua)
    add_executable(${testName} tests/${testName}.cpp)
    target_link_libraries(${testName} PRIVATE alignsim_script Qt6::Test)
    add_test(NAME ${testName} COMMAND ${testName})
endforeach()
# 20261025 ZJH Lua 测试直接加载源码目录的脚本 (握手公共库)
target_compile_definitions(test_lua PRIVATE ALIGNSIM_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/scripts")
add_custom_command(TARGET test_lua POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/lua/lib/ScriptRunner.dll" "$<TARGET_FILE_DIR:test_lua>")
