using System.Diagnostics;
using System.IO.Compression;
using System.Text.Json;

// Runs from a temporary copy so Windows can replace the active launcher folder.
// Usage: MKVDCU.Updater <launcher-pid> <verified-zip> <install-root> <release-tag>
if (args.Length != 4 || !int.TryParse(args[0], out var pid))
    return 2;

var archive = Path.GetFullPath(args[1]);
var installRoot = Path.GetFullPath(args[2]);
var tag = args[3];
var launcherBase = Path.Combine(installRoot, "launcher");
var current = Path.Combine(launcherBase, "current");
var backup = Path.Combine(launcherBase, "previous");
var staging = Path.Combine(launcherBase, "staging-" + Guid.NewGuid().ToString("N"));
if (tag.Length is < 1 or > 80 || tag.Any(c => !(char.IsLetterOrDigit(c) || c is '.' or '-' or '_')))
    return 3;

try
{
    try
    {
        using var sourceProcess = Process.GetProcessById(pid);
        if (!sourceProcess.WaitForExit(60_000)) throw new TimeoutException("Launcher did not exit for update.");
    }
    catch (ArgumentException) { } // Already exited.

    Directory.CreateDirectory(launcherBase);
    if (!IsChild(launcherBase, staging) || !IsChild(launcherBase, current) || !IsChild(launcherBase, backup))
        throw new InvalidDataException("Unsafe update destination.");
    if (Directory.Exists(current) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
        throw new InvalidDataException("Current launcher is a reparse point.");
    if (Directory.Exists(backup) && (File.GetAttributes(backup) & FileAttributes.ReparsePoint) != 0)
        throw new InvalidDataException("Backup launcher is a reparse point.");

    ExtractVerifiedArchive(archive, staging);
    if (!File.Exists(Path.Combine(staging, "MKVDCU-Recomp.exe")))
        throw new InvalidDataException("Release package lacks MKVDCU-Recomp.exe.");
    var manifestPath = Path.Combine(staging, "release-manifest.json");
    if (!File.Exists(manifestPath)) throw new InvalidDataException("Release manifest is missing.");
    using (var manifest = JsonDocument.Parse(File.ReadAllText(manifestPath)))
    {
        var root = manifest.RootElement;
        if (root.GetProperty("schema").GetInt32() != 1 ||
            root.GetProperty("version").GetString() != tag ||
            root.GetProperty("platform").GetString() != "win-x64" ||
            !string.Equals(root.GetProperty("supportedXexSha256").GetString(),
                "2955F2E2BE61EC1948CD2FD3538AD45BEB04772F5EBD5E0FB1BFDE484748E5A7",
                StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Release manifest is incompatible with this port.");
    }
    File.WriteAllText(Path.Combine(staging, "installed-version.txt"), tag);

    if (Directory.Exists(backup)) Directory.Delete(backup, true);
    if (Directory.Exists(current)) Directory.Move(current, backup);
    try
    {
        Directory.Move(staging, current);
        var launched = Process.Start(new ProcessStartInfo(Path.Combine(current, "MKVDCU-Recomp.exe"))
        {
            WorkingDirectory = current,
            UseShellExecute = false
        }) ?? throw new InvalidOperationException("Updated launcher did not start.");
        if (launched.WaitForExit(8_000) && launched.ExitCode != 0)
            throw new InvalidOperationException("Updated launcher exited with an error.");
        return 0;
    }
    catch
    {
        if (Directory.Exists(current)) Directory.Delete(current, true);
        if (Directory.Exists(backup))
        {
            Directory.Move(backup, current);
            Process.Start(new ProcessStartInfo(Path.Combine(current, "MKVDCU-Recomp.exe"))
            {
                WorkingDirectory = current, UseShellExecute = false
            });
        }
        throw;
    }
}
catch (Exception ex)
{
    try
    {
        Directory.CreateDirectory(launcherBase);
        File.WriteAllText(Path.Combine(launcherBase, "update-error.txt"), ex.ToString());
    }
    catch { }
    Console.Error.WriteLine(ex);
    return 1;
}
finally
{
    if (Directory.Exists(staging)) Directory.Delete(staging, true);
}

static bool IsChild(string parent, string child)
{
    var p = Path.GetFullPath(parent).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
    return Path.GetFullPath(child).StartsWith(p, StringComparison.OrdinalIgnoreCase);
}

static void ExtractVerifiedArchive(string archive, string staging)
{
    Directory.CreateDirectory(staging);
    using var zip = ZipFile.OpenRead(archive);
    long totalUncompressed = 0;
    foreach (var entry in zip.Entries)
    {
        var name = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(name) || name.Split(Path.DirectorySeparatorChar).Any(p => p == ".."))
            throw new InvalidDataException("Release archive contains an unsafe path.");
        if (((entry.ExternalAttributes >> 16) & 0xF000) == 0xA000)
            throw new InvalidDataException("Release archive contains a symlink.");
        totalUncompressed += entry.Length;
        if (totalUncompressed > 2L * 1024 * 1024 * 1024)
            throw new InvalidDataException("Release archive is too large.");
        var destination = Path.GetFullPath(Path.Combine(staging, name));
        if (!IsChild(staging, destination)) throw new InvalidDataException("Release archive escapes staging directory.");
        if (name.EndsWith(Path.DirectorySeparatorChar))
        {
            Directory.CreateDirectory(destination);
            continue;
        }
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        entry.ExtractToFile(destination, overwrite: false);
    }
}
