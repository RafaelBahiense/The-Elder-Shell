using TheElderShell.Plugin;

if (args.Length == 3 && args[0] == "inspect-scripts")
{
    Console.WriteLine(PluginScriptIndex.Generate(args[1], args[2]));
    return 0;
}

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: TheElderShell.Plugin <output directory> | inspect-scripts <plugin file> <index directory>");
    return 1;
}

ServicePlugin.Generate(args[0]);
return 0;
