# 测试

## 自动化测试

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

当前 CTest 发现 86 项测试，覆盖 0、1、5.5、20 ms、独立上下行、稳定顺序、并发入队、运行时单方向修改、关闭单方向立即旁路、StopAndFlush、内部 StopAndDrop、容量压力、异常路径、状态机、C ABI 布局、固定过滤器 IPv4/IPv6 行为，以及 100,000 包不提前发送压力测试。另有控制器假后端 100 次 start/stop 测试。

`nlc_lifecycle_probe.exe --cycles 100` 使用真实 WinDivert 后端连续打开/关闭 100 次并比较进程句柄数，必须以管理员身份运行。它会短暂安装全局 TCP/UDP 过滤器，请在本机控制台执行，避免通过远程桌面会话冒险。

GUI 冒烟测试可执行 `NarakaLatencyController.exe --smoke-test`。它初始化 C ABI、创建窗口并在一秒后安全关闭，不启动 WinDivert。

`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\test_hotkey.ps1` 会确认 `Ctrl+Shift+M` 起初可注册、GUI 最小化时由它占用、GUI 安全退出后释放；该测试不会开启全局延迟。

`dotnet run --project tests\preset_model_tests\NarakaLatency.PresetTests.csproj -c Release` 运行 16 项自定义预设模型测试，在系统临时目录使用独立设置文件，不修改用户的真实 `settings.json`。

## 延迟解释

`actualDelayUs = actualSendTime - captureTime`；`schedulingErrorUs = actualSendTime - dueTime`。普通调度路径不会故意提前发送。StopAndFlush 会忽略剩余 dueTime 以尽快恢复网络，因此 flush 记录不纳入调度误差分位数。

localhost echo 只能验证客户端/服务器和基线 RTT，因为产品过滤器排除了 loopback。真实双向延迟验证见 [real_network_test.md](real_network_test.md)。
