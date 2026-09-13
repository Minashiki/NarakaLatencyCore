# 真实网络测试

正式过滤器排除 loopback，因此同机 `127.0.0.1` / `::1` 测试不会被延迟。这是安全边界，不应为测试而在产品 CLI 中开放自定义过滤器。

准备第二台局域网机器或虚拟机（桥接网卡），记录其 IP。服务器端允许测试端口通过防火墙：

```powershell
nlc_udp_echo_server.exe --bind 0.0.0.0 --port 47991
nlc_tcp_echo_server.exe --bind 0.0.0.0 --port 47992
```

被测 Windows 主机先测基线：

```powershell
nlc_udp_echo_client.exe --host <server-ip> --port 47991 --count 100
nlc_tcp_echo_client.exe --host <server-ip> --port 47992 --count 100
```

再以管理员身份启动 GUI 或 CLI，例如 Inbound 7 ms、Outbound 3 ms，并重复测试。往返中位数理论上约增加 10 ms。只开 Inbound 或只开 Outbound 时，echo RTT 均约增加相应方向的值，但两者对真实应用交互的体验不同。

同时检查 GUI/CLI 指标：两方向捕获、注入与队列应合理增长；稳定负载下 `droppedPackets`、sendFailures 和 receiverErrors 应为 0。若进入 Bypassing 或 Faulted，立即执行安全停止并查看滚动日志。

浏览器下载、长连接稳定性、网卡切换、睡眠/唤醒和 IPv6 应在可恢复的测试机上单独验证。不要在依赖当前网络的远程管理会话中启用全局延迟。
