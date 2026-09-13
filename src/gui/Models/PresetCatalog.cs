namespace NarakaLatency.Gui;

internal static class PresetCatalog
{
    internal const int MaximumPresets = 50;
    internal const int MaximumNameLength = 40;

    internal static bool HasEffectiveDelay(SavedPreset preset) =>
        (preset.InboundEnabled && preset.InboundDelayMs > 0) ||
        (preset.OutboundEnabled && preset.OutboundDelayMs > 0);

    internal static bool TryNormalize(SavedPreset? candidate, out SavedPreset normalized)
    {
        normalized = new SavedPreset();
        if (candidate is null) return false;
        var name = candidate.Name?.Trim();
        if (string.IsNullOrWhiteSpace(name) || name.Length > MaximumNameLength ||
            name.Any(char.IsControl) || !ValidDelay(candidate.InboundDelayMs) ||
            !ValidDelay(candidate.OutboundDelayMs))
        {
            return false;
        }
        normalized = candidate.Copy();
        normalized.Name = name;
        if (normalized.LinkDirections &&
            normalized.InboundDelayMs != normalized.OutboundDelayMs)
        {
            normalized.LinkDirections = false;
        }
        return true;
    }

    internal static List<SavedPreset> Sanitize(IEnumerable<SavedPreset>? candidates)
    {
        var result = new List<SavedPreset>();
        if (candidates is null) return result;
        foreach (var candidate in candidates)
        {
            if (!TryNormalize(candidate, out var preset)) continue;
            var existingIndex = result.FindIndex(item =>
                string.Equals(item.Name, preset.Name, StringComparison.OrdinalIgnoreCase));
            if (existingIndex >= 0) result[existingIndex] = preset;
            else if (result.Count < MaximumPresets) result.Add(preset);
        }
        return result;
    }

    internal static bool TryUpsert(
        IList<SavedPreset> presets,
        SavedPreset candidate,
        out SavedPreset stored,
        out bool replaced,
        out string error)
    {
        stored = new SavedPreset();
        replaced = false;
        error = string.Empty;
        if (!TryNormalize(candidate, out stored))
        {
            error = $"预设名称须为 1–{MaximumNameLength} 个字符，延迟须为 0–100 ms。";
            return false;
        }
        var existingIndex = -1;
        for (var index = 0; index < presets.Count; index++)
        {
            if (string.Equals(presets[index].Name, stored.Name,
                StringComparison.OrdinalIgnoreCase))
            {
                existingIndex = index;
                break;
            }
        }
        if (existingIndex >= 0)
        {
            presets[existingIndex] = stored;
            replaced = true;
            return true;
        }
        if (presets.Count >= MaximumPresets)
        {
            error = $"最多保存 {MaximumPresets} 个预设，请先删除不再使用的预设。";
            return false;
        }
        presets.Add(stored);
        return true;
    }

    private static bool ValidDelay(double delayMs) =>
        double.IsFinite(delayMs) && delayMs is >= 0 and <= 100;
}
