using System.Text.Json;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

namespace TheElderShell.Plugin;

public static class ServicePlugin
{
    public const uint QuestId = 0x800;
    public const string EditorId = "ElderShellServices";
    public static readonly string[] Scripts = ["ElderShellRoot", "ElderShellConsole", "ElderShellDiagnostics"];

    public static string FileName { get; } = ReadFileName();

    public static SkyrimMod Create()
    {
        var mod = new SkyrimMod(ModKey.FromFileName(FileName), SkyrimRelease.SkyrimSE);
        mod.ModHeader.Flags = SkyrimModHeader.HeaderFlag.Small;
        var adapter = new QuestAdapter();
        foreach (var script in Scripts)
        {
            adapter.Scripts.Add(new ScriptEntry { Name = script });
        }

        mod.Quests.Add(new Quest(new FormKey(mod.ModKey, QuestId), SkyrimRelease.SkyrimSE)
        {
            EditorID = EditorId,
            QuestFormVersion = 65,
            Flags = Quest.Flag.StartGameEnabled | Quest.Flag.RunOnce,
            VirtualMachineAdapter = adapter
        });
        return mod;
    }

    public static string Generate(string directory)
    {
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, FileName);
        Create().BeginWrite.ToPath(path)
            .WithLoadOrderFromHeaderMasters()
            .WithNoDataFolder()
            .NoMastersListContentCheck()
            .Write();
        Console.WriteLine($"Generated {path}");
        return path;
    }

    private static string ReadFileName()
    {
        using var resource = typeof(ServicePlugin).Assembly.GetManifestResourceStream("ModIdentity")
            ?? throw new InvalidOperationException("Missing mod identity.");
        using var identity = JsonDocument.Parse(resource);
        return identity.RootElement.GetProperty("binaryName").GetString() + ".esp";
    }
}
