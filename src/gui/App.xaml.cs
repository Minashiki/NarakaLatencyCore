using System.Threading;
using System.Windows;
using Microsoft.Win32;

namespace NarakaLatency.Gui;

public partial class App : Application
{
    private Mutex? _singleInstance;
    private bool _ownsSingleInstance;
    private MainWindow? _window;
    internal bool IsSmokeTest { get; private set; }

    protected override void OnStartup(StartupEventArgs e)
    {
        IsSmokeTest = e.Args.Contains("--smoke-test", StringComparer.OrdinalIgnoreCase) ||
            e.Args.Contains("--test-no-save", StringComparer.OrdinalIgnoreCase);
        _singleInstance = new Mutex(
            true, @"Global\NarakaLatencyController.Gui.v1", out var createdNew);
        _ownsSingleInstance = createdNew;
        if (!createdNew)
        {
            MessageBox.Show("程序已经在运行。", "Naraka Latency Controller",
                MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown(2);
            return;
        }

        base.OnStartup(e);
        _window = new MainWindow();
        MainWindow = _window;
        SystemEvents.PowerModeChanged += OnPowerModeChanged;
        _window.Show();

        if (e.Args.Contains("--smoke-test", StringComparer.OrdinalIgnoreCase))
        {
            var timer = new System.Windows.Threading.DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(1)
            };
            timer.Tick += (_, _) =>
            {
                timer.Stop();
                _window.Close();
            };
            timer.Start();
        }
    }

    private async void OnPowerModeChanged(object sender, PowerModeChangedEventArgs e)
    {
        if (e.Mode == PowerModes.Suspend && _window?.DataContext is MainViewModel viewModel)
        {
            await viewModel.StopForSystemSuspendAsync();
        }
    }

    internal void ReleaseSingleInstanceForRelaunch()
    {
        if (_ownsSingleInstance)
        {
            _singleInstance?.ReleaseMutex();
            _ownsSingleInstance = false;
        }
        _singleInstance?.Dispose();
        _singleInstance = null;
    }

    internal void ReacquireSingleInstanceAfterCancelledRelaunch()
    {
        if (_singleInstance is not null) return;
        _singleInstance = new Mutex(
            true, @"Global\NarakaLatencyController.Gui.v1", out var createdNew);
        _ownsSingleInstance = createdNew;
    }

    protected override void OnExit(ExitEventArgs e)
    {
        SystemEvents.PowerModeChanged -= OnPowerModeChanged;
        NativeMethods.Shutdown();
        ReleaseSingleInstanceForRelaunch();
        base.OnExit(e);
    }
}
