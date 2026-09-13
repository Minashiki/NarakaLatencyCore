using NarakaLatency.Gui;
using System.Text.Json;

var passed = 0;
void Check(bool condition, string name)
{
    if (!condition) throw new Exception($"FAILED: {name}");
    Console.WriteLine($"PASS: {name}");
    passed++;
}

var presets = new List<SavedPreset>();
var inboundOnly = new SavedPreset
{
    Name = "  仅入站  ",
    InboundEnabled = true,
    OutboundEnabled = false,
    InboundDelayMs = 8.5,
    OutboundDelayMs = 3.2
};
Check(PresetCatalog.TryUpsert(presets, inboundOnly, out var stored, out var replaced, out _),
    "save inbound-only preset");
Check(!replaced && stored.Name == "仅入站" && stored.InboundEnabled &&
      !stored.OutboundEnabled && stored.InboundDelayMs == 8.5 &&
      stored.OutboundDelayMs == 3.2, "preserve both delays and enable flags");
Check(PresetCatalog.HasEffectiveDelay(stored), "enabled direction has effective delay");

var overwrite = stored.Copy();
overwrite.Name = "仅入站";
overwrite.InboundEnabled = false;
overwrite.OutboundEnabled = true;
overwrite.OutboundDelayMs = 12.3;
Check(PresetCatalog.TryUpsert(presets, overwrite, out stored, out replaced, out _) &&
      replaced && presets.Count == 1, "same-name save overwrites");
Check(!stored.InboundEnabled && stored.OutboundEnabled && stored.OutboundDelayMs == 12.3,
    "overwritten values are independent");

var disabled = new SavedPreset { Name = "全部关闭" };
Check(PresetCatalog.TryUpsert(presets, disabled, out stored, out _, out _) &&
      !PresetCatalog.HasEffectiveDelay(stored), "all-disabled preset can be stored");
Check(!PresetCatalog.TryUpsert(presets, new SavedPreset { Name = "   " },
      out _, out _, out _), "blank name rejected");
Check(!PresetCatalog.TryUpsert(presets, new SavedPreset
    { Name = "invalid", InboundDelayMs = 100.1 }, out _, out _, out _),
    "delay above 100 ms rejected");
Check(!PresetCatalog.TryUpsert(presets, new SavedPreset
    { Name = "invalid", InboundDelayMs = double.NaN }, out _, out _, out _),
    "non-finite delay rejected");

var settings = new AppSettings { Presets = presets };
var testPath = Path.Combine(Path.GetTempPath(), $"nlc-presets-{Guid.NewGuid():N}.json");
try
{
    Check(SettingsStore.SaveTo(settings, testPath), "atomic settings save");
    Check(!File.ReadAllText(testPath).Contains("\"Summary\""),
        "display summary is not persisted");
    var loaded = SettingsStore.LoadFrom(testPath);
    Check(loaded.Presets.Count == 2 && loaded.Presets[0].OutboundEnabled &&
          loaded.Presets[0].OutboundDelayMs == 12.3 &&
          !loaded.Presets[1].InboundEnabled && !loaded.Presets[1].OutboundEnabled,
          "JSON round-trip preserves direction settings");

    var legacy = JsonSerializer.Deserialize<AppSettings>("{\"InboundDelayMs\":5.5}");
    Check(legacy is not null && legacy.Presets.Count == 0,
        "legacy settings without presets remain loadable");

    var sanitized = PresetCatalog.Sanitize(new[]
    {
        new SavedPreset { Name = "good", InboundDelayMs = 1 },
        new SavedPreset { Name = "bad", OutboundDelayMs = -1 },
        new SavedPreset { Name = "GOOD", InboundDelayMs = 2 }
    });
    Check(sanitized.Count == 1 && sanitized[0].InboundDelayMs == 2,
        "invalid and duplicate loaded presets sanitized");

    var linked = new SavedPreset
    {
        Name = "mismatch", LinkDirections = true,
        InboundDelayMs = 2, OutboundDelayMs = 3
    };
    Check(PresetCatalog.TryNormalize(linked, out var normalized) &&
          !normalized.LinkDirections, "inconsistent linked values become unlinked");

    var full = Enumerable.Range(0, PresetCatalog.MaximumPresets)
        .Select(i => new SavedPreset { Name = $"p{i}" }).ToList();
    Check(!PresetCatalog.TryUpsert(full, new SavedPreset { Name = "overflow" },
          out _, out _, out _), "preset count limit enforced");
}
finally
{
    if (File.Exists(testPath)) File.Delete(testPath);
    if (File.Exists(testPath + ".tmp")) File.Delete(testPath + ".tmp");
}

Console.WriteLine($"{passed} preset tests passed.");
