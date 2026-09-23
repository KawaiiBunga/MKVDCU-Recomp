using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace MKVDCU.Launcher.Core;

internal sealed record BuildUpdate(string Stage, double Fraction, string Detail = "");

// Builds the game from the player's files: verify the XEX, sync the port
// sources, recompile the XEX to C++ (codegen), configure and compile with the
// launcher's toolchain, then install the result. Mirrors scripts/build-pc.ps1.
internal static partial class BuildPipeline
{
    private static readonly UTF8Encoding Utf8NoBom = new(false);

    public static async Task BuildAsync(LauncherSettings settings, PortSource port, ToolchainPaths tools,
        IProgress<BuildUpdate> progress, Action<string> log, CancellationToken token)
    {
        progress.Report(new("Checking game files", 0.01));
        var validated = await GameValidator.ValidateAsync(settings.GameFolder, token);
        if (!validated.Valid) throw new InvalidOperationException(validated.Heading + ": " + validated.Detail);
        ValidateDestinations(settings);

        progress.Report(new("Preparing sources", 0.03));
        var workspace = Paths.Workspace(settings.InstallRoot);
        await Task.Run(() => PortPackage.SyncToWorkspace(port, workspace), token);
        var project = Path.Combine(workspace, PortSource.HostPath);
        var sdk = Path.Combine(workspace, PortSource.SdkPath);
        var rexglue = Path.Combine(sdk, "bin", "rexglue.exe");
        var buildDir = Path.Combine(project, "out", "build", "win-amd64-release");
        var environment = BuildEnvironment(tools, sdk);

        // A manifest pointing codegen at this player's game folder.
        var gameXex = Path.Combine(settings.GameFolder, "default.xex");
        var manifest = Path.Combine(project, "mkvsdcu_manifest.local.toml");
        var manifestText =
            "[project]\nname = \"mkvsdcu\"\nsdk_version = \"0.10.0\"\n" +
            $"game_root = {TomlString(settings.GameFolder)}\n\n[entrypoint]\n" +
            $"file_path = {TomlString(gameXex)}\nout_directory_path = \"generated/default\"\n" +
            "includes = [\"config/mkvsdcu_functions.toml\", \"config/mkvsdcu_codegen.toml\"]\n";
        var manifestChanged = !File.Exists(manifest) || File.ReadAllText(manifest).Trim() != manifestText.Trim();
        if (manifestChanged) File.WriteAllText(manifest, manifestText, Utf8NoBom);

        var stamp = Path.Combine(project, "generated", "default", "codegen.build.stamp");
        var codegenInputs = new[]
        {
            Path.Combine(project, "config", "mkvsdcu_functions.toml"),
            Path.Combine(project, "config", "mkvsdcu_codegen.toml"),
            gameXex, rexglue,
        };
        var codegenNeeded = manifestChanged || !File.Exists(stamp) ||
            !File.Exists(Path.Combine(project, "generated", "default", "mkvsdcu_init.h")) ||
            codegenInputs.Any(input => File.GetLastWriteTimeUtc(input) > File.GetLastWriteTimeUtc(stamp));
        if (codegenNeeded)
        {
            progress.Report(new("Recompiling the game's code", 0.05, "This step takes a few minutes."));
            await Run(rexglue, ["codegen", manifest], project, environment, log, token, "Code generation");
        }

        var glue = Path.Combine(project, "generated", "rexglue.cmake");
        var originalGlue = File.ReadAllBytes(glue);
        try
        {
            File.WriteAllText(glue, PatchGlue(Encoding.UTF8.GetString(originalGlue)), Utf8NoBom);
            progress.Report(new("Configuring", 0.15));
            await Run(tools.CMake,
            [
                "-S", project, "-B", buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_CXX_COMPILER=" + Slash(tools.ClangXX), "-DCMAKE_RC_COMPILER=" + Slash(tools.LlvmRc),
                "-DCMAKE_MAKE_PROGRAM=" + Slash(tools.Ninja), "-DCMAKE_PREFIX_PATH=" + Slash(sdk),
                "-DMKVSDCU_CODEGEN_MANIFEST=" + Slash(manifest), "-DMKVSDCU_CODEGEN_MANAGED_EXTERNALLY=ON",
            ], project, environment, log, token, "Configuring");

            progress.Report(new("Compiling", 0.18));
            var jobs = ParallelJobs();
            await Run(tools.CMake, ["--build", buildDir, "--parallel", jobs.ToString()], project, environment,
                line =>
                {
                    log(line);
                    var match = NinjaProgress().Match(line);
                    if (match.Success && int.TryParse(match.Groups[2].Value, out var total) && total > 0)
                    {
                        var done = int.Parse(match.Groups[1].Value);
                        progress.Report(new("Compiling", 0.18 + 0.78 * done / total, $"{done} of {total}"));
                    }
                }, token, "Compiling");
        }
        finally
        {
            File.WriteAllBytes(glue, originalGlue);
        }

        progress.Report(new("Installing", 0.97));
        InstallOutput(settings, buildDir);
        settings.BuiltXexHash = validated.Hash!;
        settings.BuiltPortVersion = port.Version;
        SettingsStore.Save(settings);
        progress.Report(new("Ready", 1.0));
        log("Installed to " + Paths.GameInstall(settings.InstallRoot));
    }

    private static async Task Run(string exe, IEnumerable<string> arguments, string workingDirectory,
        IDictionary<string, string> environment, Action<string> log, CancellationToken token, string what)
    {
        log("> " + Path.GetFileName(exe) + " " + string.Join(' ', arguments));
        var code = await ProcessRunner.RunAsync(exe, arguments, workingDirectory, log, token, environment);
        if (code != 0) throw new InvalidOperationException($"{what} failed (exit code {code}). The build log has details.");
    }

    // Only the launcher's tools and Windows itself on PATH, so tools the
    // player installed elsewhere (another make, linker or compiler) cannot
    // leak into the build.
    private static Dictionary<string, string> BuildEnvironment(ToolchainPaths tools, string sdk)
    {
        var windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        var path = string.Join(Path.PathSeparator, tools.BinDirectories.Append(Path.Combine(sdk, "bin"))
            .Append(Path.Combine(windows, "System32")).Append(windows));
        return new() { ["PATH"] = path };
    }

    private static string PatchGlue(string text)
    {
        // ReXGlue rewrites this file during codegen. The launcher has already
        // checked every codegen input, so CMake only touches the stamp instead
        // of scanning the XEX again (same as build-pc.ps1).
        text = text.Replace("${CMAKE_CURRENT_SOURCE_DIR}/mkvsdcu_manifest.toml", "${MKVSDCU_CODEGEN_MANIFEST}");
        const string command = "    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${MKVSDCU_CODEGEN_MANIFEST}";
        if (text.Contains(command))
        {
            const string managed =
                "set(_mkvsdcu_codegen_command $<TARGET_FILE:rex::rexglue>)\n" +
                "set(_mkvsdcu_codegen_arguments codegen \"${MKVSDCU_CODEGEN_MANIFEST}\")\n" +
                "if(MKVSDCU_CODEGEN_MANAGED_EXTERNALLY)\n" +
                "    set(_mkvsdcu_codegen_command \"${CMAKE_COMMAND}\")\n" +
                "    set(_mkvsdcu_codegen_arguments -E touch \"${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp\")\n" +
                "endif()\n";
            text = text.Replace("add_custom_command(", managed + "add_custom_command(");
            text = text.Replace(command, "    COMMAND ${_mkvsdcu_codegen_command} ${_mkvsdcu_codegen_arguments}");
        }
        else if (!text.Contains("${_mkvsdcu_codegen_command}"))
        {
            throw new InvalidOperationException("ReXGlue generated an unexpected CMake codegen command.");
        }
        return text;
    }

    private static void InstallOutput(LauncherSettings settings, string buildDir)
    {
        var required = new[] { "mkvsdcu.exe", "rexruntime.dll", "rexgpu-xenos.dll" };
        foreach (var name in required)
            if (!File.Exists(Path.Combine(buildDir, name))) throw new FileNotFoundException("Build output is missing " + name);
        // The SDK decides which runtime libraries sit next to the game (for
        // example FidelityFX), so ship every one it copied.
        var names = required.Concat(Directory.EnumerateFiles(buildDir, "*.dll").Select(Path.GetFileName).OfType<string>())
            .Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var gameBase = Path.Combine(settings.InstallRoot, "game");
        Directory.CreateDirectory(gameBase);
        var staging = Path.Combine(gameBase, "staging-" + Guid.NewGuid().ToString("N"));
        var current = Paths.GameInstall(settings.InstallRoot);
        var previous = Path.Combine(gameBase, "previous");
        Directory.CreateDirectory(staging);
        try
        {
            foreach (var name in names) File.Copy(Path.Combine(buildDir, name), Path.Combine(staging, name));
            if (Directory.Exists(previous)) Directory.Delete(previous, true);
            if (Directory.Exists(current)) Directory.Move(current, previous);
            try { Directory.Move(staging, current); }
            catch
            {
                if (Directory.Exists(previous) && !Directory.Exists(current)) Directory.Move(previous, current);
                throw;
            }
        }
        finally
        {
            if (Directory.Exists(staging)) Directory.Delete(staging, true);
        }
    }

    public static void ValidateDestinations(LauncherSettings settings)
    {
        var source = Path.GetFullPath(settings.GameFolder);
        var paths = new[] { settings.InstallRoot, settings.UserRoot, settings.CacheRoot }.Select(Path.GetFullPath).ToArray();
        if (paths.Any(p => string.Equals(p, source, StringComparison.OrdinalIgnoreCase) ||
                           Paths.IsWithin(p, source) || Paths.IsWithin(source, p)))
            throw new InvalidOperationException("Install, save and cache folders must be outside the game folder.");
        for (var i = 0; i < paths.Length; i++)
            for (var j = i + 1; j < paths.Length; j++)
                if (string.Equals(paths[i], paths[j], StringComparison.OrdinalIgnoreCase) ||
                    Paths.IsWithin(paths[i], paths[j]) || Paths.IsWithin(paths[j], paths[i]))
                    throw new InvalidOperationException("Install, save and cache folders must be separate.");
    }

    // Recompiled game code needs roughly 1.5 GB per compiler at -O3.
    private static int ParallelJobs()
    {
        var memory = new MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf<MEMORYSTATUSEX>() };
        var byMemory = GlobalMemoryStatusEx(ref memory) ? (int)(memory.ullTotalPhys / (1536UL << 20)) : 4;
        return Math.Clamp(Math.Min(Environment.ProcessorCount, byMemory), 1, 64);
    }

    private static string Slash(string path) => path.Replace('\\', '/');
    private static string TomlString(string value) => JsonSerializer.Serialize(value);

    [GeneratedRegex(@"^\[(\d+)/(\d+)\]")]
    private static partial Regex NinjaProgress();

    [StructLayout(LayoutKind.Sequential)]
    private struct MEMORYSTATUSEX
    {
        public uint dwLength, dwMemoryLoad;
        public ulong ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual,
            ullAvailExtendedVirtual;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX buffer);
}
