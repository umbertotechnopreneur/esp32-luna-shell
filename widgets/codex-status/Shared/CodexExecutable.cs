namespace OpenAIUsage;

public static class CodexExecutable
{
    public static string Resolve(string configured)
    {
        configured = Environment.ExpandEnvironmentVariables(configured.Trim());
        if (configured is not ("codex" or "codex.exe")) return configured;
        var candidates = new List<string>();
        foreach (var path in new[] { Environment.GetEnvironmentVariable("PATH"),
            OperatingSystem.IsWindows() ? Environment.GetEnvironmentVariable("PATH", EnvironmentVariableTarget.User) : null,
            OperatingSystem.IsWindows() ? Environment.GetEnvironmentVariable("PATH", EnvironmentVariableTarget.Machine) : null })
            foreach (var directory in (path ?? "").Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
                candidates.Add(Path.Combine(Environment.ExpandEnvironmentVariables(directory.Trim().Trim('"')), OperatingSystem.IsWindows() ? "codex.exe" : "codex"));
        if (OperatingSystem.IsWindows())
        {
            var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            candidates.Add(Path.Combine(local, "Microsoft", "WinGet", "Links", "codex.exe"));
            candidates.Add(Path.Combine(local, "OpenAI", "Codex", "bin", "codex.exe"));
        }
        else
        {
            var user = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            candidates.Add(Path.Combine(user, ".local", "bin", "codex"));
            candidates.Add(Path.Combine(user, ".npm-global", "bin", "codex"));
            candidates.Add("/usr/local/bin/codex");
            candidates.Add("/usr/bin/codex");
        }
        return candidates.FirstOrDefault(File.Exists) ?? configured;
    }
}
