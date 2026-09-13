# 架构

## 范围和固定过滤器

核心在 `WINDIVERT_LAYER_NETWORK` 使用固定过滤器：

```text
!loopback and !impostor and !fragment and (tcp or udp)
```

它是全局 TCP/UDP 延迟器，不做 PID 映射、进程识别、正文解析或封包变换。Inbound/Outbound 只由 `WINDIVERT_ADDRESS.Outbound` 判定；完整地址结构与原始字节一起保存并用于原方向重新注入。

## 为什么不用 timeGetTime

`timeGetTime` 是毫秒级传统时钟，受系统 timer period 和轮询粒度影响。轮询它会引入量化误差、额外唤醒与 CPU 消耗。核心用单调的 `QueryPerformanceCounter` 记录捕获时间，并在捕获时把微秒延迟转换为 QPC ticks：

```text
dueTime = captureTime + configuredDelay
```

较长等待由高精度 Waitable Timer 完成，末尾最多 200 微秒短暂自旋。Windows 不是硬实时系统，DPC/ISR、节能和线程竞争仍可能造成迟发，所以核心记录实际调度误差而不承诺绝对精度。

## 为什么接收和发送线程分离

`WinDivertEngine::ReceiverMain` 持续调用 `WinDivertRecvEx`，捕获后立即 QPC 时间戳、复制数据与完整地址、读取该方向当前设置并入队。`DelayScheduler::SenderMain` 单独等待最早到期项，并用 `WinDivertSendEx` 批量重新注入。

若接收线程对每个封包执行 `Sleep(delay)`，接收吞吐会被延迟串行限制，较早封包还会阻塞后续封包进入用户态。内核捕获队列会持续增长并可能超出时间、长度或字节上限。因此接收线程不等待到期时间，不为每包建线程，也不直接按包 Sleep。

## 稳定最小堆和内存池

每项保存完整 `WINDIVERT_ADDRESS`、原始封包字节、方向、capture/due QPC 和全局递增 sequence。最小堆按 `(dueTime, sequence)` 排序，因此相同到期时间保持捕获顺序。

运行时修改只改变原子设置；已经入队的 dueTime 不会重算，从而避免突然重排。封包存储由固定容量池预分配，堆只保存池索引；发送批次缓冲区预留并复用。

## 两种队列

WinDivert 内核队列位于驱动与 `WinDivertRecvEx` 之间，是捕获突发的有限缓冲，不负责精确定时。用户态延迟队列从成功接收并取得 QPC 时间后开始，最小堆才定义发送时间。

容量接近阈值会增加压力指标。池耗尽或连续发送失败会进入 `Bypassing`：新封包立即尝试原样放行，已排队封包仍按原到期时间处理。核心绝不会采用“队列满后批量提前发送旧封包”的策略。

## 状态机与停止

状态为 `Stopped → Starting → Running`，安全条件可使 `Running → Bypassing`，停止经过 `Stopping → Stopped`，启动或后台故障进入 `Faulted`。非法转换会被拒绝并记录。

公开 UI 和 C ABI 的正常退出只有 `StopAndFlush`：先关闭 WinDivert 接收侧，让接收线程排空已经进入驱动队列的内容并退出；随后发送线程立即原样注入用户队列剩余项，最后关闭句柄。内部 `StopAndDrop` 仅供故障策略与单元测试。

强制结束、电源中断或内核崩溃无法执行用户态 flush；进程退出后 WinDivert 句柄会被系统关闭，拦截停止，但已移入用户内存的封包无法恢复。GUI 在窗口关闭和系统休眠前调用安全停止，唤醒后不会自动重启。停止若超过超时会报告仍在进行，后台清理不会被取消。

## 指标、CSV 与日志

全局和分方向指标均为有界数据结构，分位数只保留最近 65,536 个样本。核心记录封包/字节、scheduled/bypass/drop、队列深度、池使用、实际延迟、调度误差、发送/接收错误和生命周期计数。

调试 CSV 默认关闭，通过有界异步队列写入；慢磁盘不会阻塞接收或发送线程，队列满时只丢调试记录并增加 `csvRecordsDropped`。列为：

```text
sequence,direction,captureTimeQpc,dueTimeQpc,actualSendTimeQpc,configuredDelayUs,actualDelayUs,schedulingErrorUs,sendResult
```

滚动日志每个文件上限 5 MB、保留 5 个，仅记录状态、设置和错误，不记录封包正文。
