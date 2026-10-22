using System.Security.Cryptography;
using System.Text;
using Mutagen.Bethesda.Skyrim;

namespace TheElderShell.Plugin;

public static class PluginScriptIndex
{
    public static string Generate(string pluginPath, string outputDirectory)
    {
        using var mod = SkyrimMod.CreateFromBinaryOverlay(pluginPath, SkyrimRelease.SkyrimSE);
        var names = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var record in mod.EnumerateMajorRecords())
        {
            // An override can repeat another plugin's VMAD unchanged. Only
            // declarations on this plugin's own records supply attribution.
            if (record.FormKey.ModKey != mod.ModKey)
            {
                continue;
            }
            if (record is IHaveVirtualMachineAdapterGetter scripted && scripted.VirtualMachineAdapter is { } adapter)
            {
                foreach (var script in adapter.Scripts)
                {
                    names.Add(script.Name);
                }
            }
            if (record is IQuestGetter quest && quest.VirtualMachineAdapter is { } questAdapter)
            {
                foreach (var alias in questAdapter.Aliases)
                {
                    foreach (var script in alias.Scripts)
                    {
                        names.Add(script.Name);
                    }
                }
            }
        }
        if (names.Count > 20000 || names.Any(name => name.Length is 0 or > 256 || name.Any(char.IsControl)))
        {
            throw new InvalidDataException("Unsupported script declaration names or index size.");
        }
        Directory.CreateDirectory(outputDirectory);
        using var input = File.OpenRead(pluginPath);
        var digest = Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant();
        var lines = new[] { "ElderShellScriptIndex\t1", "plugin\t" + mod.ModKey.FileName, "sha256\t" + digest }
            .Concat(names.Select(name => "script\t" + name));
        var output = Path.Combine(outputDirectory, mod.ModKey.FileName + ".tsv");
        File.WriteAllLines(output, lines, new UTF8Encoding(false));
        return output;
    }
}
