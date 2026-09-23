using System.Windows;
using MKVDCU.Launcher.Core;

namespace MKVDCU.Launcher;

public partial class App : Application
{
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        // A self-update leaves the previous launcher next to this one as .old.
        Releases.CleanUpAfterUpdate();

        if (e.Args.Contains("--update"))
        {
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            Shutdown(await Setup.HeadlessUpdateAsync());
            return;
        }
        var build = Array.IndexOf(e.Args, "--build");
        if (build >= 0 && build + 1 < e.Args.Length)
        {
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            Shutdown(await Setup.HeadlessBuildAsync(e.Args[build + 1]));
            return;
        }

        var window = new MainWindow(e.Args.Contains("--updated"));
        window.Show();
        var page = Array.IndexOf(e.Args, "--page");
        if (page >= 0 && page + 1 < e.Args.Length) window.ShowPage(e.Args[page + 1]);
    }
}
