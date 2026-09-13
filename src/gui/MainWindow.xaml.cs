using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace NarakaLatency.Gui;

public partial class MainWindow : Window
{
    private const int HotkeyId = 1;
    private const int WmHotkey = 0x0312;
    private const uint ModControl = 0x0002;
    private const uint ModShift = 0x0004;
    private const uint ModNoRepeat = 0x4000;
    private const uint VirtualKeyM = 0x4D;

    private bool _closingAfterStop;
    private bool _closingInProgress;
    private bool _hotkeyRegistered;
    private HwndSource? _windowSource;

    public MainWindow()
    {
        InitializeComponent();
        var viewModel = new MainViewModel();
        DataContext = viewModel;
        Loaded += (_, _) => viewModel.AttachWindow(this);
        SourceInitialized += OnSourceInitialized;
        Closing += OnClosing;
        Closed += (_, _) => UnregisterGlobalHotkey();
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
        var handle = new WindowInteropHelper(this).Handle;
        _windowSource = HwndSource.FromHwnd(handle);
        _windowSource?.AddHook(WindowMessageHook);
        _hotkeyRegistered = RegisterHotKey(
            handle, HotkeyId, ModControl | ModShift | ModNoRepeat, VirtualKeyM);
        if (DataContext is MainViewModel viewModel)
        {
            viewModel.SetHotkeyRegistrationResult(
                _hotkeyRegistered, _hotkeyRegistered ? 0 : Marshal.GetLastWin32Error());
        }
    }

    private IntPtr WindowMessageHook(
        IntPtr handle, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == WmHotkey && wParam.ToInt64() == HotkeyId)
        {
            handled = true;
            if (!_closingInProgress && DataContext is MainViewModel viewModel)
            {
                viewModel.ToggleFromHotkey();
            }
        }
        return IntPtr.Zero;
    }

    private void UnregisterGlobalHotkey()
    {
        if (_hotkeyRegistered)
        {
            _ = UnregisterHotKey(new WindowInteropHelper(this).Handle, HotkeyId);
            _hotkeyRegistered = false;
        }
        _windowSource?.RemoveHook(WindowMessageHook);
        _windowSource = null;
    }

    private async void OnClosing(object? sender, CancelEventArgs e)
    {
        if (_closingAfterStop || DataContext is not MainViewModel viewModel)
        {
            return;
        }
        e.Cancel = true;
        if (_closingInProgress) return;
        _closingInProgress = true;
        UnregisterGlobalHotkey();
        IsEnabled = false;
        await viewModel.ShutdownAsync();
        _closingAfterStop = true;
        _ = Dispatcher.BeginInvoke(Close);
    }

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RegisterHotKey(IntPtr handle, int id, uint modifiers, uint key);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnregisterHotKey(IntPtr handle, int id);
}
