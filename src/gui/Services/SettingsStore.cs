using System.IO;
using System.Text.Json;

namespace NarakaLatency.Gui;

internal static class SettingsStore
{
    private static readonly string DirectoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NarakaLatencyController");
    private static readonly string FilePath = Path.Combine(DirectoryPath, "settings.json");

    internal static AppSettings Load() => LoadFrom(FilePath);

    internal static AppSettings LoadFrom(string filePath)
    {
        try
        {
            if (!File.Exists(filePath)) return new AppSettings();
            var settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(filePath));
            if (settings is null || !double.IsFinite(settings.InboundDelayMs) ||
                !double.IsFinite(settings.OutboundDelayMs) ||
                settings.InboundDelayMs is < 0 or > 100 ||
                settings.OutboundDelayMs is < 0 or > 100)
            {
                return new AppSettings();
            }
            settings.Presets = PresetCatalog.Sanitize(settings.Presets);
            return settings;
        }
        catch
        {
            return new AppSettings();
        }
    }

    internal static bool Save(AppSettings settings) => SaveTo(settings, FilePath);

    internal static bool SaveTo(AppSettings settings, string filePath)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(filePath)!);
            var temporaryPath = filePath + ".tmp";
            File.WriteAllText(temporaryPath, JsonSerializer.Serialize(
                settings, new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temporaryPath, filePath, true);
            return true;
        }
        catch
        {
            // Configuration persistence must never block engine use.
            return false;
        }
    }

    internal static string LogDirectory => Path.Combine(DirectoryPath, "logs");
    internal static string CsvDirectory => Path.Combine(DirectoryPath, "csv");
}
