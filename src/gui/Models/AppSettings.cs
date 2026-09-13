namespace NarakaLatency.Gui;

internal sealed class AppSettings
{
    public bool InboundEnabled { get; set; } = true;
    public bool OutboundEnabled { get; set; } = true;
    public double InboundDelayMs { get; set; }
    public double OutboundDelayMs { get; set; }
    public bool LinkDirections { get; set; }
    public double? WindowLeft { get; set; }
    public double? WindowTop { get; set; }
    public bool AdvancedPanelExpanded { get; set; }
    public bool CsvEnabled { get; set; }
    public List<SavedPreset> Presets { get; set; } = [];
}
