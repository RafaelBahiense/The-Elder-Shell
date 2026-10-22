using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

namespace TheElderShell.Plugin.Tests;

public sealed class ServicePluginTests
{
    [Fact]
    public void GeneratedPluginPreservesTheServiceQuestContract()
    {
        var directory = Path.Combine(Path.GetTempPath(), "elder-shell-plugin-" + Guid.NewGuid().ToString("N"));
        try
        {
            var path = ServicePlugin.Generate(directory);
            using var mod = SkyrimMod.CreateFromBinaryOverlay(path, SkyrimRelease.SkyrimSE);
            Assert.Equal(ModKey.FromFileName("TheElderShell.esp"), mod.ModKey);
            Assert.True(mod.ModHeader.Flags.HasFlag(SkyrimModHeader.HeaderFlag.Small));
            Assert.Empty(mod.ModHeader.MasterReferences);
            var quest = Assert.IsType<IQuestGetter>(Assert.Single(mod.EnumerateMajorRecords()), exactMatch: false);
            Assert.Equal(new FormKey(mod.ModKey, 0x800), quest.FormKey);
            Assert.Equal("ElderShellServices", quest.EditorID);
            Assert.Equal((byte)65, quest.QuestFormVersion);
            Assert.Equal(Quest.Flag.StartGameEnabled | Quest.Flag.RunOnce, quest.Flags);
            Assert.Empty(quest.Aliases);
            Assert.Empty(quest.Stages);
            var adapter = Assert.IsType<IQuestAdapterGetter>(quest.VirtualMachineAdapter, exactMatch: false);
            Assert.Equal(["ElderShellRoot", "ElderShellConsole", "ElderShellSelection", "ElderShellForms", "ElderShellActors", "ElderShellAI", "ElderShellQuests", "ElderShellScripts"],
                adapter.Scripts.Select(script => script.Name));
            Assert.All(adapter.Scripts, script => Assert.Empty(script.Properties));
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void GenerationIsByteIdenticalAcrossOutputDirectories()
    {
        var directory = Path.Combine(Path.GetTempPath(), "elder-shell-plugin-" + Guid.NewGuid().ToString("N"));
        try
        {
            var first = ServicePlugin.Generate(Path.Combine(directory, "first"));
            var second = ServicePlugin.Generate(Path.Combine(directory, "second"));
            Assert.Equal(File.ReadAllBytes(first), File.ReadAllBytes(second));
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }
}
