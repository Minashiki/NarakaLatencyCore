using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Security.Principal;
using System.Text;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;

namespace NarakaLatency.Gui;

internal sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly AppSettings _settings;
    private readonly DispatcherTimer _metricsTimer;
    private readonly SemaphoreSlim _settingsUpdateGate = new(1, 1);
    private bool _nativeAvailable;
    private bool _syncingLinkedValues;
    private bool _polling;
    private string _lastDirection = "inbound";
    private MainWindow? _window;
    private NativeEngineState _state = NativeEngineState.Stopped;
    private string _errorText = string.Empty;
    private ulong _queueDepth;
    private ulong _sendFailures;
    private double _p95Error;
    private double _p99Error;
    private string _advancedMetricsText = "尚未启动";

    internal MainViewModel()
    {
        _settings = SettingsStore.Load();
        IsAdministrator = CheckAdministrator();
        StartCommand = new AsyncRelayCommand(StartAsync, CanStart);
        StopCommand = new AsyncRelayCommand(StopAsync, CanStop);
        ResetMetricsCommand = new RelayCommand(_ => ResetMetrics());
        RelaunchElevatedCommand = new RelayCommand(_ => RelaunchElevated(), _ => !IsAdministrator);
        ApplyPresetCommand = new RelayCommand(ApplyPreset);
        OpenLogDirectoryCommand = new RelayCommand(_ => OpenLogDirectory());

        try
        {
            _nativeAvailable = NativeMethods.nl_initialize() == NativeResult.Ok;
            if (!_nativeAvailable) ErrorText = "原生核心初始化失败。";
        }
        catch (Exception error) when (
            error is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException)
        {
            ErrorText = "缺少或无法加载 NarakaLatency.Native.dll / WinDivert.dll。";
        }

        _metricsTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _metricsTimer.Tick += async (_, _) => await RefreshMetricsAsync();
        _metricsTimer.Start();
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    internal bool IsAdministrator { get; }
    public Visibility AdministratorWarningVisibility =>
        IsAdministrator ? Visibility.Collapsed : Visibility.Visible;

    public bool InboundEnabled
    {
        get => _settings.InboundEnabled;
        set
        {
            if (_settings.InboundEnabled == value) return;
            _settings.InboundEnabled = value;
            Changed();
        }
    }

    public bool OutboundEnabled
    {
        get => _settings.OutboundEnabled;
        set
        {
            if (_settings.OutboundEnabled == value) return;
            _settings.OutboundEnabled = value;
            Changed();
        }
    }

    public double InboundDelayMs
    {
        get => _settings.InboundDelayMs;
        set => SetDelay(value, true);
    }

    public double OutboundDelayMs
    {
        get => _settings.OutboundDelayMs;
        set => SetDelay(value, false);
    }

    public bool LinkDirections
    {
        get => _settings.LinkDirections;
        set
        {
            if (_settings.LinkDirections == value) return;
            _settings.LinkDirections = value;
            OnPropertyChanged();
            if (value)
            {
                _syncingLinkedValues = true;
                if (_lastDirection == "inbound")
                {
                    _settings.OutboundDelayMs = _settings.InboundDelayMs;
                    OnPropertyChanged(nameof(OutboundDelayMs));
                }
                else
                {
                    _settings.InboundDelayMs = _settings.OutboundDelayMs;
                    OnPropertyChanged(nameof(InboundDelayMs));
                }
                _syncingLinkedValues = false;
                Changed(null);
            }
            else SaveSettings();
        }
    }

    public bool CsvEnabled
    {
        get => _settings.CsvEnabled;
        set
        {
            if (_settings.CsvEnabled == value) return;
            _settings.CsvEnabled = value;
            OnPropertyChanged();
            SaveSettings();
        }
    }

    public bool AdvancedPanelExpanded
    {
        get => _settings.AdvancedPanelExpanded;
        set
        {
            if (_settings.AdvancedPanelExpanded == value) return;
            _settings.AdvancedPanelExpanded = value;
            OnPropertyChanged();
            SaveSettings();
        }
    }

    public string StateDisplay => _state switch
    {
        NativeEngineState.Stopped => "引擎状态：未启动",
        NativeEngineState.Starting => "引擎状态：启动中",
        NativeEngineState.Running => "引擎状态：运行中",
        NativeEngineState.Bypassing => "引擎状态：安全旁路",
        NativeEngineState.Stopping => "引擎状态：停止中",
        NativeEngineState.Faulted => "引擎状态：故障",
        _ => "引擎状态：未知"
    };

    public string ErrorText { get => _errorText; private set { _errorText = value; OnPropertyChanged(); } }
    public ulong QueueDepth { get => _queueDepth; private set { _queueDepth = value; OnPropertyChanged(); } }
    public ulong SendFailures { get => _sendFailures; private set { _sendFailures = value; OnPropertyChanged(); } }
    public string P95ErrorDisplay => $"{_p95Error / 1000.0:F3} ms";
    public string P99ErrorDisplay => $"{_p99Error / 1000.0:F3} ms";
    public string AdvancedMetricsText { get => _advancedMetricsText; private set { _advancedMetricsText = value; OnPropertyChanged(); } }

    public ICommand StartCommand { get; }
    public ICommand StopCommand { get; }
    public ICommand ResetMetricsCommand { get; }
    public ICommand RelaunchElevatedCommand { get; }
    public ICommand ApplyPresetCommand { get; }
    public ICommand OpenLogDirectoryCommand { get; }

    internal void AttachWindow(MainWindow window)
    {
        _window = window;
        if (_settings.WindowLeft is double left && _settings.WindowTop is double top)
        {
            window.Left = left;
            window.Top = top;
        }
    }

    internal async Task ShutdownAsync()
    {
        _metricsTimer.Stop();
        if (_window is not null)
        {
            _settings.WindowLeft = _window.Left;
            _settings.WindowTop = _window.Top;
        }
        SaveSettings();
        if (_state is NativeEngineState.Running or NativeEngineState.Bypassing or NativeEngineState.Faulted)
        {
            await StopAsync();
        }
        NativeMethods.Shutdown();
    }

    internal Task StopForSystemSuspendAsync()
    {
        ErrorText = "系统即将睡眠，正在安全停止；唤醒后不会自动重新启用。";
        return StopAsync();
    }

    private bool CanStart() => _nativeAvailable && IsAdministrator &&
        _state == NativeEngineState.Stopped &&
        ((InboundEnabled && InboundDelayMs > 0) || (OutboundEnabled && OutboundDelayMs > 0));

    private bool CanStop() => _state is NativeEngineState.Running or
        NativeEngineState.Bypassing or NativeEngineState.Faulted;

    private async Task StartAsync()
    {
        if (!IsAdministrator)
        {
            ErrorText = "该程序需要管理员权限才能加载网络过滤驱动。";
            return;
        }
        ErrorText = string.Empty;
        _state = NativeEngineState.Starting;
        NotifyState();
        if (CsvEnabled)
        {
            Directory.CreateDirectory(SettingsStore.CsvDirectory);
            var path = Path.Combine(SettingsStore.CsvDirectory,
                $"scheduling-{DateTime.Now:yyyyMMdd-HHmmss}.csv");
            var csvResult = NativeMethods.nl_set_csv_path(path);
            if (csvResult != NativeResult.Ok)
            {
                ErrorText = $"CSV 初始化失败：{csvResult}";
                _state = NativeEngineState.Stopped;
                NotifyState();
                return;
            }
        }
        else NativeMethods.nl_set_csv_path(string.Empty);

        var settings = CreateNativeSettings();
        var result = await Task.Run(() => NativeMethods.nl_start(ref settings));
        if (result != NativeResult.Ok)
        {
            ErrorText = $"启动失败（{result}）：{NativeMethods.GetLastError()}";
            _state = NativeEngineState.Faulted;
        }
        else _state = NativeEngineState.Running;
        NotifyState();
        await RefreshMetricsAsync();
    }

    private async Task StopAsync()
    {
        if (_state == NativeEngineState.Stopped) return;
        _state = NativeEngineState.Stopping;
        NotifyState();
        var result = await Task.Run(() => NativeMethods.nl_stop_and_flush(10_000));
        if (result == NativeResult.Ok)
        {
            _state = NativeEngineState.Stopped;
        }
        else if (result == NativeResult.Timeout)
        {
            ErrorText = "安全停止仍在进行，未确认全部封包已放行。";
        }
        else
        {
            ErrorText = $"停止失败（{result}）：{NativeMethods.GetLastError()}";
            _state = NativeEngineState.Faulted;
        }
        NotifyState();
        await RefreshMetricsAsync();
    }

    private void ResetMetrics()
    {
        if (!_nativeAvailable) return;
        var result = NativeMethods.nl_reset_metrics();
        if (result != NativeResult.Ok) ErrorText = $"重置统计失败：{result}";
        _ = RefreshMetricsAsync();
    }

    private void ApplyPreset(object? parameter)
    {
        if (!double.TryParse(parameter?.ToString(), out var rttMs)) return;
        _syncingLinkedValues = true;
        _settings.InboundEnabled = true;
        _settings.OutboundEnabled = true;
        _settings.InboundDelayMs = rttMs / 2.0;
        _settings.OutboundDelayMs = rttMs / 2.0;
        _syncingLinkedValues = false;
        OnPropertyChanged(nameof(InboundEnabled));
        OnPropertyChanged(nameof(OutboundEnabled));
        OnPropertyChanged(nameof(InboundDelayMs));
        OnPropertyChanged(nameof(OutboundDelayMs));
        Changed(null);
    }

    private void SetDelay(double value, bool inbound)
    {
        if (double.IsNaN(value) || value < 0 || value > 100)
        {
            ErrorText = "延迟必须在 0.0 至 100.0 ms 之间。";
            return;
        }
        value = Math.Round(value, 1, MidpointRounding.AwayFromZero);
        if (inbound)
        {
            if (_settings.InboundDelayMs == value) return;
            _settings.InboundDelayMs = value;
            _lastDirection = "inbound";
            OnPropertyChanged(nameof(InboundDelayMs));
            if (LinkDirections && !_syncingLinkedValues)
            {
                _syncingLinkedValues = true;
                _settings.OutboundDelayMs = value;
                OnPropertyChanged(nameof(OutboundDelayMs));
                _syncingLinkedValues = false;
            }
        }
        else
        {
            if (_settings.OutboundDelayMs == value) return;
            _settings.OutboundDelayMs = value;
            _lastDirection = "outbound";
            OnPropertyChanged(nameof(OutboundDelayMs));
            if (LinkDirections && !_syncingLinkedValues)
            {
                _syncingLinkedValues = true;
                _settings.InboundDelayMs = value;
                OnPropertyChanged(nameof(InboundDelayMs));
                _syncingLinkedValues = false;
            }
        }
        Changed(null);
    }

    private void Changed([CallerMemberName] string? propertyName = null)
    {
        if (propertyName is not null) OnPropertyChanged(propertyName);
        SaveSettings();
        CommandManager.InvalidateRequerySuggested();
        _ = UpdateNativeSettingsAsync();
    }

    private async Task UpdateNativeSettingsAsync()
    {
        if (_state is not (NativeEngineState.Running or NativeEngineState.Bypassing)) return;
        await _settingsUpdateGate.WaitAsync();
        try
        {
            var settings = CreateNativeSettings();
            var result = await Task.Run(() => NativeMethods.nl_update_settings(ref settings));
            if (result != NativeResult.Ok)
                ErrorText = $"运行时设置更新失败（{result}）：{NativeMethods.GetLastError()}";
        }
        catch (Exception error)
        {
            ErrorText = $"运行时设置更新失败：{error.Message}";
        }
        finally { _settingsUpdateGate.Release(); }
    }

    private NativeDelaySettings CreateNativeSettings() => NativeDelaySettings.Create(
        InboundEnabled, OutboundEnabled,
        checked((long)Math.Round(InboundDelayMs * 1000.0)),
        checked((long)Math.Round(OutboundDelayMs * 1000.0)));

    private async Task RefreshMetricsAsync()
    {
        if (!_nativeAvailable || _polling) return;
        _polling = true;
        try
        {
            var metrics = NativeMetricsSnapshot.Create();
            var result = await Task.Run(() => NativeMethods.nl_get_metrics(ref metrics));
            if (result != NativeResult.Ok) return;
            _state = metrics.EngineState;
            QueueDepth = metrics.Inbound.QueueDepth + metrics.Outbound.QueueDepth;
            SendFailures = metrics.Inbound.SendFailures + metrics.Outbound.SendFailures;
            _p95Error = Math.Max(metrics.Inbound.P95SchedulingErrorUs, metrics.Outbound.P95SchedulingErrorUs);
            _p99Error = Math.Max(metrics.Inbound.P99SchedulingErrorUs, metrics.Outbound.P99SchedulingErrorUs);
            OnPropertyChanged(nameof(P95ErrorDisplay));
            OnPropertyChanged(nameof(P99ErrorDisplay));
            AdvancedMetricsText = BuildAdvancedText(metrics);
            if (metrics.EngineState is NativeEngineState.Bypassing or NativeEngineState.Faulted &&
                !string.IsNullOrWhiteSpace(metrics.LastErrorText))
                ErrorText = metrics.LastErrorText;
            NotifyState();
        }
        catch (Exception error)
        {
            ErrorText = $"读取指标失败：{error.Message}";
        }
        finally { _polling = false; }
    }

    private static string BuildAdvancedText(NativeMetricsSnapshot metrics)
    {
        var text = new StringBuilder();
        text.AppendLine($"过滤器: {metrics.ActiveFilter}");
        text.AppendLine($"运行时间: {metrics.EngineUptimeMs} ms");
        text.AppendLine($"封包池: {metrics.CurrentPoolUsage} / 最大 {metrics.MaximumPoolUsage}");
        text.AppendLine($"Inbound: 捕获 {metrics.Inbound.CapturedPackets}, 调度 {metrics.Inbound.ScheduledPackets}, " +
                        $"旁路 {metrics.Inbound.BypassPackets}, P99 误差 {metrics.Inbound.P99SchedulingErrorUs:F1} us");
        text.AppendLine($"Outbound: 捕获 {metrics.Outbound.CapturedPackets}, 调度 {metrics.Outbound.ScheduledPackets}, " +
                        $"旁路 {metrics.Outbound.BypassPackets}, P99 误差 {metrics.Outbound.P99SchedulingErrorUs:F1} us");
        text.AppendLine($"CSV 丢弃记录: {metrics.CsvRecordsDropped}");
        text.AppendLine($"最后错误: {metrics.LastErrorText}");
        return text.ToString();
    }

    private void NotifyState()
    {
        OnPropertyChanged(nameof(StateDisplay));
        CommandManager.InvalidateRequerySuggested();
    }

    private void RelaunchElevated()
    {
        try
        {
            SaveSettings();
            if (Application.Current is App app)
                app.ReleaseSingleInstanceForRelaunch();
            Process.Start(new ProcessStartInfo
            {
                FileName = Environment.ProcessPath!,
                UseShellExecute = true,
                Verb = "runas"
            });
            Application.Current.Shutdown();
        }
        catch (Win32Exception error)
        {
            if (Application.Current is App app)
                app.ReacquireSingleInstanceAfterCancelledRelaunch();
            ErrorText = error.NativeErrorCode == 1223 ? "用户取消了管理员授权。" : error.Message;
        }
    }

    private static void OpenLogDirectory()
    {
        Directory.CreateDirectory(SettingsStore.LogDirectory);
        Process.Start(new ProcessStartInfo("explorer.exe", SettingsStore.LogDirectory)
        {
            UseShellExecute = true
        });
    }

    private static bool CheckAdministrator()
    {
        using var identity = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
    }

    private void SaveSettings() => SettingsStore.Save(_settings);

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
