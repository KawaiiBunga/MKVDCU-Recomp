using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.Win32;

namespace MKVDCU.Launcher.Core;

// The compiler tools of one installed toolchain archive.
internal sealed class ToolchainPaths(string root)
{
    public string Root { get; } = root;
    public string Id => Path.GetFileName(Root);
    public string Clang => Path.Combine(Root, "llvm", "bin", "clang.exe");
    public string ClangXX => Path.Combine(Root, "llvm", "bin", "clang++.exe");
    public string LlvmRc => Path.Combine(Root, "llvm", "bin", "llvm-rc.exe");
    public string CMake => Path.Combine(Root, "cmake", "bin", "cmake.exe");
    public string Ninja => Path.Combine(Root, "ninja", "ninja.exe");
    public IEnumerable<string> BinDirectories =>
        [Path.Combine(Root, "llvm", "bin"), Path.Combine(Root, "cmake", "bin"), Path.Combine(Root, "ninja")];
}

internal sealed record MsvcStatus(bool Ready, string Detail);

internal static class Toolchain
{
    private const string Marker = ".complete";
    public const string MsvcBootstrapperUrl = "https://aka.ms/vs/17/release/vs_BuildTools.exe";
    // MSVC compiler libraries and the Windows SDK: what clang needs to target
    // Windows. Microsoft does not allow these to be redistributed.
    private static readonly string[] MsvcComponents =
    [
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "Microsoft.VisualStudio.Component.Windows11SDK.26100",
    ];

    public static ToolchainPaths? FindInstalled(string? preferredId = null)
    {
        if (!Directory.Exists(Paths.Tools)) return null;
        var complete = Directory.EnumerateDirectories(Paths.Tools)
            .Where(dir => File.Exists(Path.Combine(dir, Marker)))
            .Select(dir => new ToolchainPaths(dir))
            .Where(t => File.Exists(t.ClangXX) && File.Exists(t.CMake) && File.Exists(t.Ninja))
            .ToList();
        return complete.FirstOrDefault(t => t.Id == preferredId) ??
               complete.OrderByDescending(t => Directory.GetLastWriteTimeUtc(t.Root)).FirstOrDefault();
    }

    public static async Task<ToolchainPaths> InstallAsync(ReleaseAsset asset, string id,
        IProgress<DownloadProgress> progress, Action<string> status, CancellationToken token)
    {
        status("Downloading compiler tools");
        var archive = await asset.FetchAsync(progress, token);
        status("Unpacking compiler tools");
        Directory.CreateDirectory(Paths.Tools);
        var target = Path.Combine(Paths.Tools, id);
        var staging = Path.Combine(Paths.Tools, ".staging-" + Guid.NewGuid().ToString("N"));
        try
        {
            await Task.Run(() =>
            {
                ZipFile.ExtractToDirectory(archive, staging);
                // Tools that pick their mode from their own file name ship once.
                var bin = Path.Combine(staging, "llvm", "bin");
                Duplicate(Path.Combine(bin, "clang.exe"), Path.Combine(bin, "clang++.exe"));
                Duplicate(Path.Combine(bin, "llvm-ar.exe"), Path.Combine(bin, "llvm-ranlib.exe"));
                Duplicate(Path.Combine(bin, "llvm-ar.exe"), Path.Combine(bin, "llvm-lib.exe"));
                File.WriteAllText(Path.Combine(staging, Marker), id);
                if (Directory.Exists(target)) Directory.Delete(target, true);
                Directory.Move(staging, target);
            }, token);
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
        // Older toolchains are no longer used by anything.
        foreach (var dir in Directory.EnumerateDirectories(Paths.Tools))
        {
            if (!string.Equals(dir, target, StringComparison.OrdinalIgnoreCase))
                try { Directory.Delete(dir, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
        File.Delete(archive);
        return new ToolchainPaths(target);
    }

    private static void Duplicate(string source, string destination)
    {
        if (!File.Exists(source) || File.Exists(destination)) return;
        if (!CreateHardLink(destination, source, IntPtr.Zero)) File.Copy(source, destination);
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateHardLink(string fileName, string existingFileName, IntPtr securityAttributes);

    public static MsvcStatus DetectMsvc()
    {
        var vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            "Microsoft Visual Studio", "Installer", "vswhere.exe");
        string? install = null;
        if (File.Exists(vswhere))
        {
            install = ProcessRunner.Capture(vswhere, "-products", "*", "-requires", MsvcComponents[0],
                "-latest", "-property", "installationPath", "-utf8", "-nologo")?.Split('\n')[0].Trim();
        }
        var hasCompilerLibs = !string.IsNullOrEmpty(install) &&
            Directory.Exists(Path.Combine(install, "VC", "Tools", "MSVC")) &&
            Directory.EnumerateFiles(Path.Combine(install, "VC", "Tools", "MSVC"), "msvcrt.lib", SearchOption.AllDirectories).Any();
        var hasSdk = WindowsSdkLibraries() != null;
        return (hasCompilerLibs, hasSdk) switch
        {
            (true, true) => new(true, "Installed"),
            (false, true) => new(false, "C++ libraries missing"),
            (true, false) => new(false, "Windows SDK missing"),
            _ => new(false, "Not installed"),
        };
    }

    private static string? WindowsSdkLibraries()
    {
        using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows Kits\Installed Roots");
        if (key?.GetValue("KitsRoot10") is not string root) return null;
        var lib = Path.Combine(root, "Lib");
        if (!Directory.Exists(lib)) return null;
        return Directory.EnumerateDirectories(lib, "10.*")
            .FirstOrDefault(v => File.Exists(Path.Combine(v, "um", "x64", "kernel32.lib")) &&
                                 File.Exists(Path.Combine(v, "ucrt", "x64", "ucrt.lib")));
    }

    // Downloads Microsoft's Build Tools installer, checks Microsoft signed it,
    // and runs it for just the C++ compiler libraries and Windows SDK. Windows
    // asks the player for permission; Microsoft's own progress window shows.
    public static async Task InstallMsvcAsync(Action<string> status, CancellationToken token)
    {
        status("Downloading Microsoft's installer");
        Directory.CreateDirectory(Paths.Downloads);
        var installer = Path.Combine(Paths.Downloads, "vs_BuildTools.exe");
        await using (var source = await Http.Client.GetStreamAsync(MsvcBootstrapperUrl, token))
        await using (var file = File.Create(installer))
            await source.CopyToAsync(file, token);
        if (!Authenticode.IsSignedBy(installer, "Microsoft Corporation"))
        {
            File.Delete(installer);
            throw new InvalidDataException("The Build Tools installer was not signed by Microsoft; it was not run.");
        }
        status("Waiting for Microsoft's installer (allow it when Windows asks)");
        var arguments = new List<string> { "--passive", "--wait", "--norestart", "--nocache" };
        foreach (var component in MsvcComponents) { arguments.Add("--add"); arguments.Add(component); }
        var psi = new ProcessStartInfo(installer) { UseShellExecute = true };
        foreach (var argument in arguments) psi.ArgumentList.Add(argument);
        Process? process;
        try
        {
            process = Process.Start(psi);
        }
        catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            throw new OperationCanceledException("Windows permission was declined.");
        }
        if (process == null) throw new InvalidOperationException("Microsoft's installer did not start.");
        using (process)
        {
            await process.WaitForExitAsync(token);
            // 3010: installed, restart recommended but not needed for building.
            if (process.ExitCode is not (0 or 3010))
                throw new InvalidOperationException(
                    $"Microsoft's installer stopped with code {process.ExitCode}. Run it again, or install " +
                    "\"Desktop development with C++\" from Visual Studio Build Tools yourself.");
        }
        try { File.Delete(installer); } catch (IOException) { }
    }

    public static string ToolchainIdOf(string root)
    {
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "toolchain.json")));
            return doc.RootElement.GetProperty("id").GetString() ?? Path.GetFileName(root);
        }
        catch (Exception) { return Path.GetFileName(root); }
    }
}
