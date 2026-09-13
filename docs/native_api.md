# C ABI

`NarakaLatency.Native.dll` 导出固定布局、`__cdecl`、UTF-16 错误文本接口：

```text
nl_initialize
nl_start
nl_update_settings
nl_stop_and_flush
nl_get_state
nl_get_metrics
nl_get_last_error
nl_reset_metrics
nl_set_csv_path
nl_shutdown
```

调用方先将结构清零并把 `structSize` 设置为当前结构大小。预留字段必须保持为 0。快照由调用方提供内存，DLL 不返回跨边界所有权。每个导出函数都捕获 C++ 异常并返回 `NL_Result`。

进程间使用 `Global\NarakaLatencyController.Engine.v1` 互斥体，只有一个控制器能进入 Running。`nl_initialize` 本身不加载驱动；`nl_start` 才做管理员权限、相邻 `WinDivert.dll` / `WinDivert64.sys` 和设置检查。

正常退出顺序为 `nl_stop_and_flush(timeoutMs)` 后 `nl_shutdown()`。超时表示清理仍在后台继续，不应强制结束进程。
