param(
    [string]$Executable = "dist\NarakaLatencyController.exe"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$executablePath = (Resolve-Path (Join-Path $projectRoot $Executable)).Path

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class HotkeyTestNative
{
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool UnregisterHotKey(IntPtr window, int id);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ShowWindow(IntPtr window, int command);
}
'@

$testId = 4321
$modifiers = [uint32](0x0002 -bor 0x0004 -bor 0x4000)
$key = [uint32]0x4D

if (-not [HotkeyTestNative]::RegisterHotKey([IntPtr]::Zero, $testId, $modifiers, $key)) {
    throw "Ctrl+Shift+M is already owned by another application; cannot test registration."
}
if (-not [HotkeyTestNative]::UnregisterHotKey([IntPtr]::Zero, $testId)) {
    throw "Could not release the preflight hotkey."
}

$process = Start-Process -FilePath $executablePath `
    -ArgumentList "--test-no-save" -WindowStyle Hidden -PassThru
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw "GUI exited before hotkey registration." }
        $window = $process.MainWindowHandle
        if ($window -ne [IntPtr]::Zero) { break }
    }
    if ($window -eq [IntPtr]::Zero) { throw "GUI window did not appear." }

    [void][HotkeyTestNative]::ShowWindow($window, 6) # SW_MINIMIZE

    if ([HotkeyTestNative]::RegisterHotKey([IntPtr]::Zero, $testId, $modifiers, $key)) {
        [void][HotkeyTestNative]::UnregisterHotKey([IntPtr]::Zero, $testId)
        throw "GUI did not own Ctrl+Shift+M while running."
    }

    if (-not [HotkeyTestNative]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) {
        throw "Could not request graceful GUI close."
    }
    if (-not $process.WaitForExit(15000)) { throw "GUI did not close safely." }

    if (-not [HotkeyTestNative]::RegisterHotKey([IntPtr]::Zero, $testId, $modifiers, $key)) {
        throw "GUI did not release Ctrl+Shift+M after exit."
    }
    [void][HotkeyTestNative]::UnregisterHotKey([IntPtr]::Zero, $testId)
    Write-Host "Global Ctrl+Shift+M registration and cleanup passed."
}
finally {
    if (-not $process.HasExited) {
        $process.Kill()
        [void]$process.WaitForExit(5000)
    }
    $process.Dispose()
}
