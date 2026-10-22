using System.Security.Cryptography;

namespace TheElderShell.Plugin.Tests;

public sealed class PluginScriptIndexTests
{
    [Fact]
    public void IndexUsesTypedDeclarationsAndExactPluginDigest()
    {
        var root = Path.Combine(Path.GetTempPath(), "elder-shell-index-" + Guid.NewGuid().ToString("N"));
        try
        {
            var plugin = ServicePlugin.Generate(root);
            var output = PluginScriptIndex.Generate(plugin, Path.Combine(root, "indexes"));
            var lines = File.ReadAllLines(output);
            Assert.Equal("ElderShellScriptIndex\t1", lines[0]);
            Assert.Equal("plugin\tTheElderShell.esp", lines[1]);
            Assert.Equal("sha256\t" + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(plugin))).ToLowerInvariant(), lines[2]);
            Assert.Contains("script\tElderShellRoot", lines);
            Assert.Contains("script\tElderShellConsole", lines);
            Assert.Contains("script\tElderShellDiagnostics", lines);
            Assert.Equal(6, lines.Length);
        }
        finally
        {
            if (Directory.Exists(root))
            {
                Directory.Delete(root, true);
            }
        }
    }
}
