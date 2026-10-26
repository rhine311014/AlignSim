// 20261025 ZJH 平台误差模型 —— 让模拟平台像真实机械一样"走不准"
//
// 两套位置:
//   指令位置 (commanded): PLC 认为平台在哪, 回写给视觉的就是它 (与无误差时完全一致)
//   实际位置 (actual)   : 指令位置叠加下列误差后平台真正停的位置, 决定 Mark 显示/对位残差/拾取判定 (真值)
//
// 轴空间误差 (每轴): 实际轴值 = 指令 × (1 + 比例ppm) + 反向间隙迟滞 + 重复定位随机误差
// 几何误差         : 实际旋转中心 = 标称 + 偏差; UVW 实际安装点 = 标称 + 偏差; XYD 的 Y 轴相对理想方向偏转 (垂直度)
#pragma once

#include <array>

struct StageErrorModel {
    bool bEnabled = false;                    // 总开关; 关闭时实际位置 = 指令位置 (按标称几何)

    double dRepeatUm = 0.0;                   // 重复定位误差 σ (µm), 直线轴; 每次移动按高斯分布抽一次
    double dRepeatDeg = 0.0;                  // 重复定位误差 σ (°), 角度轴 (XYD 的 D)
    double dBacklashUm = 0.0;                 // 反向间隙 (µm), 直线轴: 负向移动后实际停在指令 + 间隙处
    double dBacklashDeg = 0.0;                // 反向间隙 (°), 角度轴
    std::array<double, 3> arrScalePpm = {};   // 每轴比例误差 (ppm): 实际行程 = 指令 × (1 + ppm×1e-6)

    double dSkewDeg = 0.0;                    // 垂直度误差 (°, 仅 XYD): Y 轴方向偏转, 正值向 −X 倾
    double dRotCenterErrX = 0.0;              // 实际旋转中心相对标称偏差 X (mm)
    double dRotCenterErrY = 0.0;              // 实际旋转中心相对标称偏差 Y (mm)
    std::array<double, 6> arrUvwMountErrUm = {};  // UVW 安装点偏差 (µm): U.x U.y V.x V.y W.x W.y

    double dSpeedMmPerS = 0.0;                // 直线轴运动速度 (mm/s), 0 = 瞬间到位 (仅脚本发起的移动)
    double dSpeedDegPerS = 0.0;               // 角度轴运动速度 (°/s), 0 = 瞬间到位
    int nSettleMs = 0;                        // 到位后整定时间 (ms), 仅脚本发起的移动

    int nSeed = 1;                            // 随机种子: 相同种子 + 相同移动序列 → 相同误差, 便于复现
};
