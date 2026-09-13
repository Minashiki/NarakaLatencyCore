using System.Text.Json.Serialization;

namespace NarakaLatency.Gui;

internal sealed class SavedPreset
{
    public string Name { get; set; } = string.Empty;
    public bool InboundEnabled { get; set; }
    public bool OutboundEnabled { get; set; }
    public double InboundDelayMs { get; set; }
    public double OutboundDelayMs { get; set; }
    public bool LinkDirections { get; set; }

    [JsonIgnore]
    public string Summary =>
        $"{Name}  |  入 {(InboundEnabled ? $"{InboundDelayMs:F1} ms" : "关闭")}  " +
        $"出 {(OutboundEnabled ? $"{OutboundDelayMs:F1} ms" : "关闭")}";

    public SavedPreset Copy() => new()
    {
        Name = Name,
        InboundEnabled = InboundEnabled,
        OutboundEnabled = OutboundEnabled,
        InboundDelayMs = InboundDelayMs,
        OutboundDelayMs = OutboundDelayMs,
        LinkDirections = LinkDirections
    };
}
