# NarakaLatencyCore

NarakaLatencyCore 是面向 Windows 10/11 x64 的全局 TCP/UDP 固定延迟器原型。原生核心使用 C++20、WinDivert 2.2、QPC、高精度 waitable timer、稳定最小堆和预分配封包池；控制界面使用 .NET 8 WPF。

本阶段只处理通用网络流量。它不绑定进程或游戏，不读取其他进程内存，不识别协议，不修改封包正文，也不实现丢包、复制、乱序、驱动隐藏或反作弊规避。

## 行为与安全边界

固定过滤器为：

```text
!loopback and !impostor and !fragment and (tcp or udp)
```

因此程序作用于本机所有普通非 loopback、非 impostor、非分片 TCP/UDP 流量，包括 IPv4 和 IPv6。ICMP、ARP、loopback 和分片流量不处理。方向只依据 `WINDIVERT_ADDRESS.Outbound`，不会通过 IP 或端口猜测。

Inbound 和 Outbound 均有独立启用开关与 0–100 ms 延迟。关闭或设置为 0 的方向立即原样旁路；运行时修改只影响之后捕获的封包。正常停止始终采用 `StopAndFlush`。

> 全局网络拦截需要管理员权限。启动延迟后，浏览器、更新、语音和远程连接也可能感受到附加延迟。先从 1–5 ms、短时间开始测试。

## 构建

依赖：Visual Studio 2022 C++ 工具链、CMake 3.24+、.NET 8 SDK、WinDivert 2.2.2。默认由 CMake 获取 WinDivert 和 GoogleTest；离线构建可传入本地 SDK。

```powershell
cmake -S . -B build -A x64 -DNLC_WINDIVERT_ROOT=C:\path\to\WinDivert-2.2.2-A
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
dotnet build src\gui\NarakaLatency.Gui.csproj -c Release
```

## 使用

推荐从发布目录以管理员身份运行 `NarakaLatencyController.exe`。默认配置为上下行均启用、延迟 0、上下行联动关闭、CSV 关闭；仅打开界面不会创建 WinDivert 拦截句柄。

控制台默认也只校验参数，不启动拦截：

```powershell
nlc_delay_console.exe --inbound-delay-us 7000 --outbound-delay-us 3000
nlc_delay_console.exe --apply-delay --inbound-delay-us 7000 --outbound-delay-us 3000 --duration 10 --show-stats
nlc_delay_console.exe --apply-delay --preset rtt-20 --duration 10
```

必须显式传入 `--apply-delay` 才会打开全局过滤器。控制台不接受自定义过滤器。

## 测试工具

仓库提供 TCP/UDP echo 客户端和服务器。默认 localhost 只验证工具与基线 RTT；由于正式过滤器明确排除 loopback，它不会产生附加延迟。验证真实 Inbound 延迟必须在第二台机器或虚拟机上运行服务器，详见 [真实网络测试](docs/real_network_test.md)。

更多资料：

- [架构与退出恢复](docs/architecture.md)
- [GUI 使用说明](docs/gui.md)
- [测试矩阵](docs/testing.md)
- [C ABI](docs/native_api.md)
- [已知限制](docs/known_limitations.md)

WinDivert 以官方未修改的动态库和驱动形式分发，其许可证随发布包提供。
