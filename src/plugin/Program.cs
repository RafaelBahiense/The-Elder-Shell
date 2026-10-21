using TheElderShell.Plugin;

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: TheElderShell.Plugin <output directory>");
    return 1;
}

ServicePlugin.Generate(args[0]);
return 0;
