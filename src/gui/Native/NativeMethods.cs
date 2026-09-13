using System.Runtime.InteropServices;
using System.Text;

namespace NarakaLatency.Gui;

internal enum NativeResult : int
{
    Ok = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    AccessDenied = 3,
    DependencyMissing = 4,
    StartFailed = 5,
    Timeout = 6,
    BufferTooSmall = 7,
    VersionMismatch = 8,
    AlreadyRunning = 9,
    InternalError = 10
}

internal enum NativeEngineState : int
{
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Bypassing = 3,
    Stopping = 4,
    Faulted = 5
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDelaySettings
{
    public uint StructSize;
    public byte InboundEnabled;
    public byte OutboundEnabled;
    public byte Reserved0;
    public byte Reserved1;
    public long InboundDelayUs;
    public long OutboundDelayUs;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
    public ulong[] Reserved2;

    public static NativeDelaySettings Create(bool inbound, bool outbound, long inboundUs, long outboundUs)
    {
        return new NativeDelaySettings
        {
            StructSize = (uint)Marshal.SizeOf<NativeDelaySettings>(),
            InboundEnabled = inbound ? (byte)1 : (byte)0,
            OutboundEnabled = outbound ? (byte)1 : (byte)0,
            InboundDelayUs = inboundUs,
            OutboundDelayUs = outboundUs,
            Reserved2 = new ulong[4]
        };
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeDirectionMetrics
{
    public uint StructSize;
    public byte Enabled;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)] public byte[] Reserved0;
    public long ConfiguredDelayUs;
    public ulong CapturedPackets;
    public ulong CapturedBytes;
    public ulong ScheduledPackets;
    public ulong ScheduledBytes;
    public ulong InjectedPackets;
    public ulong InjectedBytes;
    public ulong BypassPackets;
    public ulong BypassBytes;
    public ulong DroppedPackets;
    public ulong QueueDepth;
    public ulong MaximumQueueDepth;
    public double AverageActualDelayUs;
    public double P50ActualDelayUs;
    public double P95ActualDelayUs;
    public double P99ActualDelayUs;
    public double MaximumActualDelayUs;
    public double AverageSchedulingErrorUs;
    public double P50SchedulingErrorUs;
    public double P95SchedulingErrorUs;
    public double P99SchedulingErrorUs;
    public double MaximumSchedulingErrorUs;
    public ulong SendFailures;
    public ulong PoolExhaustions;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public ulong[] Reserved1;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
internal struct NativeMetricsSnapshot
{
    public uint StructSize;
    public NativeEngineState EngineState;
    public ulong EngineStartTimeUnixMs;
    public ulong EngineUptimeMs;
    public ulong FlushPendingPackets;
    public double FlushDurationUs;
    public NativeDirectionMetrics Inbound;
    public NativeDirectionMetrics Outbound;
    public ulong CurrentPoolUsage;
    public ulong MaximumPoolUsage;
    public ulong ReceiverErrors;
    public ulong SenderErrors;
    public ulong ConsecutiveSendFailures;
    public ulong CsvRecordsDropped;
    public ulong TotalFlushCount;
    public ulong TotalStartCount;
    public ulong TotalStopCount;
    public int LastErrorCode;
    public uint Reserved0;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string ActiveFilter;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string LastErrorText;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)] public ulong[] Reserved1;

    public static NativeMetricsSnapshot Create() => new()
    {
        StructSize = (uint)Marshal.SizeOf<NativeMetricsSnapshot>(),
        ActiveFilter = string.Empty,
        LastErrorText = string.Empty,
        Reserved1 = new ulong[8]
    };
}

internal static class NativeMethods
{
    private const string DllName = "NarakaLatency.Native.dll";

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeResult nl_initialize();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeResult nl_start(ref NativeDelaySettings settings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeResult nl_update_settings(ref NativeDelaySettings settings);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeResult nl_stop_and_flush(uint timeoutMs);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeResult nl_get_metrics(ref NativeMetricsSnapshot metrics);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern NativeResult nl_reset_metrics();

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern NativeResult nl_get_last_error(StringBuilder buffer, uint bufferLength);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern NativeResult nl_set_csv_path(string path);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    private static extern void nl_shutdown();

    internal static void Shutdown()
    {
        try { nl_shutdown(); } catch (DllNotFoundException) { }
        catch (EntryPointNotFoundException) { }
    }

    internal static string GetLastError()
    {
        var buffer = new StringBuilder(2048);
        return nl_get_last_error(buffer, (uint)buffer.Capacity) == NativeResult.Ok
            ? buffer.ToString() : "无法读取原生错误信息";
    }
}
