using System.ComponentModel;
using System.Windows;

namespace NarakaLatency.Gui;

public partial class MainWindow : Window
{
    private bool _closingAfterStop;

    public MainWindow()
    {
        InitializeComponent();
        var viewModel = new MainViewModel();
        DataContext = viewModel;
        Loaded += (_, _) => viewModel.AttachWindow(this);
        Closing += OnClosing;
    }

    private async void OnClosing(object? sender, CancelEventArgs e)
    {
        if (_closingAfterStop || DataContext is not MainViewModel viewModel)
        {
            return;
        }
        e.Cancel = true;
        IsEnabled = false;
        await viewModel.ShutdownAsync();
        _closingAfterStop = true;
        _ = Dispatcher.BeginInvoke(Close);
    }
}
