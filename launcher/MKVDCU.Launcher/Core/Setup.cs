using System.IO;
using System.Security.Cryptography;

namespace MKVDCU.Launcher.Core;

// The setup steps shared by the window and the headless --build mode.
internal static class Setup
{
    public static async Task<ReleaseManifest> OwnReleaseAsync(string channel, CancellationToken token)
    {
        var release = Releases.IsDevBuild
            ? await Releases.LatestAsync(channel, token)
            : await Releases.ForVersionAsync(Releases.CurrentVersion, token);
        return release ?? throw new InvalidOperationException(
            "Could not find this version's release on GitHub. Check your internet connection and try again.");
    }

    public static async Task<ToolchainPaths> InstallToolchainAsync(string channel, IProgress<DownloadProgress> progress,
        Action<string> status, CancellationToken token)
    {
        ReleaseAsset asset;
        string id;
        // A developer checkout can use the archive scripts/make-toolchain.ps1 made.
        var local = Paths.FindRepository() is { } repo && Directory.Exists(Path.Combine(repo, "dist"))
            ? Directory.EnumerateFiles(Path.Combine(repo, "dist"), "mkvdcu-toolchain-*.zip").FirstOrDefault()
            : null;
        if (local != null && Environment.GetEnvironmentVariable("MKVDCU_RELEASE_DIR") == null)
        {
            id = Path.GetFileNameWithoutExtension(local)["mkvdcu-toolchain-".Length..];
            string hash;
            await using (var stream = File.OpenRead(local))
                hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, token));
            asset = new ReleaseAsset { File = Path.GetFileName(local), Sha256 = hash, Size = new FileInfo(local).Length, LocalPath = local };
        }
        else
        {
            var release = await OwnReleaseAsync(channel, token);
            asset = release.Toolchain.ToAsset(release);
            id = release.Toolchain.Id;
        }
        return await Toolchain.InstallAsync(asset, id, progress, status, token);
    }

    public static async Task<PortSource> EnsurePortAsync(string channel, IProgress<DownloadProgress> progress,
        CancellationToken token)
    {
        if (PortPackage.FindInstalled(Releases.CurrentVersion) is { } installed) return installed;
        var release = await OwnReleaseAsync(channel, token);
        return await PortPackage.InstallAsync(release, progress, token);
    }

    // MKVDCU-Recomp.exe --update: replaces this launcher with the newest
    // release on the saved channel. Exit code 0 updated, 3 already current.
    public static async Task<int> HeadlessUpdateAsync()
    {
        Directory.CreateDirectory(Paths.AppData);
        var logPath = Path.Combine(Paths.AppData, "update-headless.log");
        await using var log = new StreamWriter(logPath, append: false) { AutoFlush = true };
        try
        {
            var settings = SettingsStore.Load();
            var latest = await Releases.LatestAsync(settings.UpdateChannel, CancellationToken.None);
            if (latest == null || Releases.Compare(latest.Version, Releases.CurrentVersion) <= 0)
            {
                await log.WriteLineAsync($"Current: {Releases.CurrentVersion}, latest: {latest?.Version ?? "none"}");
                return 3;
            }
            var replaced = await Releases.ReplaceLauncherAsync(latest, new Progress<DownloadProgress>(), CancellationToken.None);
            await log.WriteLineAsync($"{Releases.CurrentVersion} -> {latest.Version}: {(replaced ? "replaced" : "same file")}");
            return 0;
        }
        catch (Exception ex)
        {
            await log.WriteLineAsync("FAILED " + ex);
            return 1;
        }
    }

    // MKVDCU-Recomp.exe --build "<game folder>": installs the compiler tools
    // if needed and builds, writing progress to build-headless.log in the
    // data folder. For testing and scripted setups; exit code 0 on success.
    public static async Task<int> HeadlessBuildAsync(string gameFolder)
    {
        Directory.CreateDirectory(Paths.AppData);
        var logPath = Path.Combine(Paths.AppData, "build-headless.log");
        await using var log = new StreamWriter(logPath, append: false) { AutoFlush = true };
        void Log(string line) { lock (log) log.WriteLine($"{DateTime.Now:HH:mm:ss} {line}"); }
        try
        {
            var settings = SettingsStore.Load();
            settings.GameFolder = Path.GetFullPath(gameFolder);
            SettingsStore.Save(settings);
            var msvc = Toolchain.DetectMsvc();
            Log("Microsoft C++ tools: " + msvc.Detail);
            if (!msvc.Ready) return 2;
            var downloads = new Progress<DownloadProgress>(p => Log($"download {p.Received}/{p.Total}"));
            var tools = Toolchain.FindInstalled() ?? await Setup.InstallToolchainAsync(settings.UpdateChannel, downloads, Log, CancellationToken.None);
            Log("Toolchain: " + tools.Root);
            var port = await EnsurePortAsync(settings.UpdateChannel, downloads, CancellationToken.None);
            Log($"Port {port.Version}: {port.Root}");
            var lastStage = "";
            var progress = new Progress<BuildUpdate>(u =>
            {
                if (u.Stage != lastStage) Log($"== {u.Stage}");
                lastStage = u.Stage;
            });
            await BuildPipeline.BuildAsync(settings, port, tools, progress, Log, CancellationToken.None);
            Log("OK " + Paths.GameExe(settings.InstallRoot));
            return 0;
        }
        catch (Exception ex)
        {
            Log("FAILED " + ex);
            return 1;
        }
    }
}
