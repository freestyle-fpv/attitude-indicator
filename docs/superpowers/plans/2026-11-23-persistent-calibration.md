# Persistent Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 完成一次校准后持久保存，后续开机直接恢复。

**Architecture:** 方向独立、CRC 保护的 NVS 记录；采样任务负责恢复/采样并投递保存请求，主循环负责写入；网页诊断解释保存状态。

**Tech Stack:** ESP32 Arduino C++11 / Preferences / FreeRTOS / 原生 C++ 回归 / Node.js。

**Spec:** `docs/superpowers/specs/2026-11-23-persistent-calibration-design.md`

## Global Constraints

- 四方向记录隔离，坏记录重新校准，保留 400 样本和现有门槛。
- 保存安装零点；启动时当前倾角不能被归零。
- 主循环写 NVS，仅新校准完成后写一次；写入失败必须可见。
- 本地测试包交付，不自动发布远端；沿用当前工作区，不覆盖先前改动。

## Review Focus

- 倾斜开机恢复真实相对姿态，而不是新零点。
- CRC、长度、版本、方向、非有限值和不合理偏置不能授权恢复。
- PWR 请求或 IMU 自检后不能再次套用开机记录跳过新校准。
- 传感器启动失败与暂时失读不能被持久记录伪装为有效姿态。
- NVS 写入或回读失败必须显示失败，且每个成功窗口仅写一次。

### Task 1: 存储记录与滤波零点恢复

- [x] 添加 `tests/calibration_store_test.cpp`：方向隔离、记录损坏、保存失败、倾斜启动和无重力读数保护；先观察缺失功能导致失败。
- [x] 实现 `firmware/AttitudeIndicator/calibration_store.h` 中记录、校验、NVS 适配函数，以及 `AttitudeFilter::restoreReference(Vec3 zero, Vec3 current)`。
- [x] 将回归加入 `tools/test.sh`，运行新测试及现有姿态测试。

### Task 2: 真实采样任务与网页诊断

- [x] 扩展真实采样任务主机回归，先验证旧代码不能恢复记录和投递保存。
- [x] 初始化时加载当前方向记录，首次可信样本恢复零点与偏置；手动校准禁用旧记录恢复；完成后投递一次保存，主循环回读验证。
- [x] 输出 `IMUHW cal_store`，网页中文区分恢复、保存中、已保存与失败，并添加行为回归。

### Task 3: 验证与本地安装包

- [x] 更新安装说明，解释重装保留数据、方向切换和手动重新校准。
- [x] 运行完整 `sh tools/test.sh` 与 `sh tools/build.sh`；生成带新版本号的离线安装包，核验内嵌固件。
- [x] 自查启动/手动/故障/坏记录路径，并说明实体断电测试范围。

执行记录：采用现有工作区执行。完整主机测试、ESP32 构建、安装包核验已通过。独立审查的旧窗口保存状态竞态使用原子窗口 token 修复并加回归；实际断电验证尚未进行。结果见 `docs/persistent-calibration-validation.md`。没有提交或发布远端。
