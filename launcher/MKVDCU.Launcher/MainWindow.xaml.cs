using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;

namespace MKVDCU.Launcher;

public partial class MainWindow : Window
{
    private readonly LauncherSettings _settings;
    private readonly BuildService _builder = new();
    private readonly UpdateService _updater = new();
    private CancellationTokenSource? _buildCancellation;
    private ReleaseInfo? _release;
    private bool _ready;

    public MainWindow()
    {
        InitializeComponent();
        _settings = SettingsStore.Load();
        GameFolderBox.Text = _settings.GameFolder;
        InstallBox.Text = _settings.InstallRoot;
        UserBox.Text = _settings.UserRoot;
        CacheBox.Text = _settings.CacheRoot;
        SdkBox.Text = _settings.SdkRoot;
        if (_settings.UpdateChannel == "preview") PreviewChannelButton.IsChecked = true;
        else StableChannelButton.IsChecked = true;
        _ready = true;
        ShowPanel("play");
        RefreshBuildState();
        Loaded += async (_, _) =>
        {
            if (!string.IsNullOrWhiteSpace(_settings.GameFolder)) await VerifyAsync();
            await CheckForUpdatesAsync(silent: true);
        };
    }

    private static readonly Brush Good = new SolidColorBrush(Color.FromRgb(187, 164, 125));
    private static readonly Brush Bad = new SolidColorBrush(Color.FromRgb(192, 95, 78));

    private void ShowPanel(string panel)
    {
        PlayPanel.Visibility = panel == "play" ? Visibility.Visible : Visibility.Collapsed;
        LibraryPanel.Visibility = panel == "library" ? Visibility.Visible : Visibility.Collapsed;
        BuildPanel.Visibility = panel == "build" ? Visibility.Visible : Visibility.Collapsed;
        SettingsPanel.Visibility = panel == "settings" ? Visibility.Visible : Visibility.Collapsed;
        UpdatesPanel.Visibility = panel == "updates" ? Visibility.Visible : Visibility.Collapsed;
        foreach (var nav in new[] { PlayNav, LibraryNav, BuildNav, SettingsNav, UpdatesNav })
        {
            var active = (string)nav.Tag == panel;
            nav.Background = new SolidColorBrush(active ? Color.FromRgb(54, 41, 40) : Colors.Transparent);
            nav.BorderBrush = active ? Bad : Brushes.Transparent;
            nav.BorderThickness = new Thickness(active ? 3 : 0, 0, 0, 0);
            nav.Foreground = active ? Brushes.White : new SolidColorBrush(Color.FromRgb(184, 181, 175));
        }
    }

    private void Nav_Click(object sender, RoutedEventArgs e) => ShowPanel((string)((Button)sender).Tag);
    private void TitleBar_Drag(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2)
            WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
        else DragMove();
    }
    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Maximize_Click(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    protected override void OnStateChanged(EventArgs e)
    {
        base.OnStateChanged(e);
        var maximized = WindowState == WindowState.Maximized;
        MaximizeButton.Content = maximized ? "" : "";
        MaximizeButton.ToolTip = maximized ? "Restore" : "Maximize";
        WindowFrame.BorderThickness = new Thickness(maximized ? 0 : 1);
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        HwndSource.FromHwnd(new WindowInteropHelper(this).Handle)?.AddHook(WindowProc);
    }

    // A borderless window maximizes over the taskbar unless it is told the
    // monitor's work area.
    private static IntPtr WindowProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        const int WM_GETMINMAXINFO = 0x0024;
        if (msg != WM_GETMINMAXINFO) return IntPtr.Zero;
        var monitor = NativeMethods.MonitorFromWindow(hwnd, NativeMethods.MONITOR_DEFAULTTONEAREST);
        var info = new NativeMethods.MONITORINFO { cbSize = Marshal.SizeOf<NativeMethods.MONITORINFO>() };
        if (monitor == IntPtr.Zero || !NativeMethods.GetMonitorInfo(monitor, ref info)) return IntPtr.Zero;
        var limits = Marshal.PtrToStructure<NativeMethods.MINMAXINFO>(lParam);
        limits.ptMaxPosition.X = info.rcWork.Left - info.rcMonitor.Left;
        limits.ptMaxPosition.Y = info.rcWork.Top - info.rcMonitor.Top;
        limits.ptMaxSize.X = info.rcWork.Right - info.rcWork.Left;
        limits.ptMaxSize.Y = info.rcWork.Bottom - info.rcWork.Top;
        Marshal.StructureToPtr(limits, lParam, true);
        return IntPtr.Zero;
    }
    private void Close_Click(object sender, RoutedEventArgs e) { _buildCancellation?.Cancel(); Close(); }

    private void BrowseGame_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Choose extracted MK vs. DC Universe game folder" };
        if (dialog.ShowDialog(this) == true)
        {
            GameFolderBox.Text = dialog.FolderName;
            _ = VerifyAsync();
        }
    }

    private void BrowseFolder(TextBox destination, string title)
    {
        var dialog = new OpenFolderDialog { Title = title };
        if (Directory.Exists(destination.Text)) dialog.InitialDirectory = destination.Text;
        if (dialog.ShowDialog(this) == true) destination.Text = dialog.FolderName;
    }

    private void BrowseInstall_Click(object sender, RoutedEventArgs e) =>
        BrowseFolder(InstallBox, "Choose game build location");
    private void BrowseUser_Click(object sender, RoutedEventArgs e) =>
        BrowseFolder(UserBox, "Choose save and settings location");
    private void BrowseCache_Click(object sender, RoutedEventArgs e) =>
        BrowseFolder(CacheBox, "Choose shader cache location");
    private void BrowseSdk_Click(object sender, RoutedEventArgs e) =>
        BrowseFolder(SdkBox, "Choose ReXGlue SDK install or checkout");

    private async void Verify_Click(object sender, RoutedEventArgs e) => await VerifyAsync();

    private async Task<bool> VerifyAsync()
    {
        _settings.GameFolder = GameFolderBox.Text.Trim();
        HeroGamePath.Text = string.IsNullOrWhiteSpace(_settings.GameFolder) ? "—" : _settings.GameFolder;
        ValidationHeading.Text = HeroStatus.Text = "SCANNING FILES";
        ValidationDetail.Text = HeroDetail.Text = "Reading game executable and required content folders…";
        FooterStatus.Text = "CHECKING GAME FOLDER";
        try
        {
            var result = await GameValidator.ValidateAsync(_settings.GameFolder);
            ValidationHeading.Text = HeroStatus.Text = result.Heading;
            ValidationDetail.Text = HeroDetail.Text = result.Detail;
            ValidationHeading.Foreground = HeroStatus.Foreground = result.Valid ? Good : Bad;
            FooterStatus.Text = result.Valid ? "GAME FILES VERIFIED" : "GAME VALIDATION NEEDS ATTENTION";
            SettingsStore.Save(_settings);
            RefreshBuildState();
            return result.Valid;
        }
        catch (Exception ex)
        {
            ValidationHeading.Text = HeroStatus.Text = "VALIDATION FAILED";
            ValidationDetail.Text = HeroDetail.Text = ex.Message;
            ValidationHeading.Foreground = HeroStatus.Foreground = Bad;
            FooterStatus.Text = "VALIDATION ERROR";
            return false;
        }
    }

    private void RefreshBuildState()
    {
        var installed = File.Exists(Paths.GameExe(_settings.InstallRoot)) &&
                        _settings.BuiltXexHash.Equals(Paths.ExpectedXexHash, StringComparison.OrdinalIgnoreCase) &&
                        _settings.BuiltVersion == _settings.InstalledVersion;
        HeroBuildState.Text = installed ? "READY TO PLAY" : "BUILD REQUIRED";
        HeroBuildState.Foreground = installed ? Good : Bad;
        PlayButton.Content = installed ? "PLAY GAME   →" : "BUILD TO PLAY   →";
        PlayButton.IsEnabled = true;
    }

    private void SaveSettings_Click(object sender, RoutedEventArgs e) => SaveLocations();
    private bool SaveLocations()
    {
        try
        {
            _settings.InstallRoot = Path.GetFullPath(InstallBox.Text.Trim());
            _settings.UserRoot = Path.GetFullPath(UserBox.Text.Trim());
            _settings.CacheRoot = Path.GetFullPath(CacheBox.Text.Trim());
            _settings.SdkRoot = string.IsNullOrWhiteSpace(SdkBox.Text)
                ? "" : Path.GetFullPath(SdkBox.Text.Trim());
            if (!string.IsNullOrWhiteSpace(GameFolderBox.Text))
            {
                _settings.GameFolder = Path.GetFullPath(GameFolderBox.Text.Trim());
                BuildService.ValidateDestinations(_settings);
            }
            SettingsStore.Save(_settings);
            RefreshBuildState();
            FooterStatus.Text = "LOCATIONS SAVED";
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Location error", MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }
    }

    private async void Build_Click(object sender, RoutedEventArgs e)
    {
        if (_buildCancellation != null || !SaveLocations()) return;
        if (!await VerifyAsync()) { ShowPanel("library"); return; }
        ShowPanel("build");
        BuildLog.Clear();
        BuildStatus.Text = "BUILDING";
        BuildProgress.IsIndeterminate = true;
        BuildButton.IsEnabled = false;
        CancelBuildButton.IsEnabled = true;
        FooterStatus.Text = "NATIVE BUILD IN PROGRESS";
        _buildCancellation = new CancellationTokenSource();
        var progress = new Progress<string>(line =>
        {
            BuildLog.AppendText(line + Environment.NewLine);
            if (BuildLog.Text.Length > 160_000) BuildLog.Text = BuildLog.Text[^120_000..];
            BuildLog.ScrollToEnd();
        });
        try
        {
            await _builder.BuildAsync(_settings, line => ((IProgress<string>)progress).Report(line), _buildCancellation.Token);
            BuildStatus.Text = "BUILD COMPLETE";
            FooterStatus.Text = "READY TO PLAY";
            RefreshBuildState();
        }
        catch (OperationCanceledException)
        {
            BuildStatus.Text = "BUILD CANCELLED";
            FooterStatus.Text = "BUILD CANCELLED";
        }
        catch (Exception ex)
        {
            BuildStatus.Text = "BUILD FAILED";
            FooterStatus.Text = "BUILD FAILED — SEE BUILD LOG";
            BuildLog.AppendText(Environment.NewLine + "ERROR: " + ex.Message);
        }
        finally
        {
            BuildProgress.IsIndeterminate = false;
            BuildButton.IsEnabled = true;
            CancelBuildButton.IsEnabled = false;
            _buildCancellation.Dispose();
            _buildCancellation = null;
        }
    }

    private void CancelBuild_Click(object sender, RoutedEventArgs e) => _buildCancellation?.Cancel();

    private async void Play_Click(object sender, RoutedEventArgs e)
    {
        if (!await VerifyAsync()) { ShowPanel("library"); return; }
        if (!File.Exists(Paths.GameExe(_settings.InstallRoot)) ||
            !_settings.BuiltXexHash.Equals(Paths.ExpectedXexHash, StringComparison.OrdinalIgnoreCase) ||
            _settings.BuiltVersion != _settings.InstalledVersion)
        {
            ShowPanel("build");
            return;
        }
        try
        {
            var process = BuildService.Launch(_settings);
            FooterStatus.Text = $"GAME RUNNING  /  PID {process.Id}";
            HeroBuildState.Text = "GAME RUNNING";
            _ = MonitorGameAsync(process);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Launch failed", MessageBoxButton.OK, MessageBoxImage.Error);
            ShowPanel("build");
        }
    }

    private async Task MonitorGameAsync(Process process)
    {
        try
        {
            await process.WaitForExitAsync();
            FooterStatus.Text = process.ExitCode == 0 ? "GAME CLOSED CLEANLY" : $"GAME EXITED  /  CODE {process.ExitCode} — CHECK LOGS";
        }
        catch (Exception ex) { FooterStatus.Text = "GAME MONITOR ERROR: " + ex.Message; }
        finally { process.Dispose(); RefreshBuildState(); }
    }

    private void Channel_Changed(object sender, RoutedEventArgs e)
    {
        if (!_ready || sender is not RadioButton { IsChecked: true } choice) return;
        _settings.UpdateChannel = (string)choice.Tag;
        SettingsStore.Save(_settings);
        _release = null;
        DownloadButton.IsEnabled = false;
        UpdateStatus.Text = "CHANNEL CHANGED";
        UpdateDetail.Text = "Check releases to see the newest compatible build.";
        ReleaseNotes.Text = "Check for updates to load release notes.";
    }

    private async void CheckUpdates_Click(object sender, RoutedEventArgs e) => await CheckForUpdatesAsync(silent: false);

    private async Task CheckForUpdatesAsync(bool silent)
    {
        if (!silent) ShowPanel("updates");
        UpdateStatus.Text = "CHECKING RELEASES";
        UpdateDetail.Text = "Contacting the GitHub Releases feed…";
        try
        {
            _release = await _updater.CheckAsync(_settings.UpdateChannel, CancellationToken.None);
            if (_release == null)
            {
                UpdateStatus.Text = "NO RELEASE AVAILABLE";
                UpdateDetail.Text = "No compatible Windows release was found for this channel.";
                ReleaseNotes.Text = "No release notes available.";
                DownloadButton.IsEnabled = false;
                return;
            }
            var installed = _release.Tag == _settings.InstalledVersion;
            UpdateStatus.Text = installed ? "CURRENT RELEASE" : "NEW RELEASE  /  " + _release.Tag;
            UpdateDetail.Text = installed ? "This version is already installed." :
                $"Windows package · {_release.Size / 1024.0 / 1024.0:0.0} MiB · {_settings.UpdateChannel} channel";
            ReleaseNotes.Text = _release.Body;
            DownloadButton.IsEnabled = !installed;
        }
        catch (Exception ex)
        {
            UpdateStatus.Text = "OFFLINE / FEED ERROR";
            UpdateDetail.Text = ex.Message;
            ReleaseNotes.Text = "Release notes could not be loaded.";
            DownloadButton.IsEnabled = false;
        }
    }

    private async void Download_Click(object sender, RoutedEventArgs e)
    {
        if (_release == null) return;
        DownloadButton.IsEnabled = false;
        UpdateStatus.Text = "DOWNLOADING UPDATE";
        var progress = new Progress<double>(fraction => UpdateProgress.Value = Math.Clamp(fraction * 100, 0, 100));
        try
        {
            var file = await _updater.DownloadAsync(_release, _settings.InstallRoot, progress, CancellationToken.None);
            UpdateStatus.Text = "INSTALLING UPDATE";
            UpdateDetail.Text = "Verified package. The launcher will restart after installation.";
            UpdateService.StartInstall(file, _settings.InstallRoot, _release.Tag);
            FooterStatus.Text = "SWITCHING TO NEW RELEASE";
            Close();
        }
        catch (Exception ex)
        {
            UpdateStatus.Text = "UPDATE DOWNLOAD FAILED";
            UpdateDetail.Text = ex.Message;
            DownloadButton.IsEnabled = true;
        }
    }
}
