using System.IO;
using System.Text.Json;

namespace NarakaLatency.Gui;

internal static class SettingsStore
{
    private static readonly string DirectoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "NarakaLatencyController");
    private static readonly string FilePath = Path.Combine(DirectoryPath, "settings.json");

    internal static AppSettings Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return new AppSettings();
            var settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath));
            if (settings is null || settings.InboundDelayMs is < 0 or > 100 ||
                settings.OutboundDelayMs is < 0 or > 100)
            {
                return new AppSettings();
            }
            return settings;
        }
        catch
        {
            return new AppSettings();
        }
    }

    internal static void Save(AppSettings settings)
    {
        try
        {
            Directory.CreateDirectory(DirectoryPath);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(
                settings, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Configuration persistence must never block engine use.
        }
    }

    internal static string LogDirectory => Path.Combine(DirectoryPath, "logs");
    internal static string CsvDirectory => Path.Combine(DirectoryPath, "csv");
}
