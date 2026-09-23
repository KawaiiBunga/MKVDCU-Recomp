using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using MKVDCU.Launcher.Core;
using Microsoft.Win32;

namespace MKVDCU.Launcher;

public partial class MainWindow : Window
{
    private enum NextStep { ChooseGame, InstallTools, Build, Play, Running }

    private readonly LauncherSettings _settings = SettingsStore.Load();
    private readonly bool _justUpdated;
    private CancellationTokenSource? _operation;
    private ReleaseManifest? _latest;
    private Core.ValidationResult? _game;
    private MsvcStatus _msvc = new(false, "Checking");
    private ToolchainPaths? _tools;
    private PortSource? _port;
    private Process? _gameProcess;
    private NextStep _next;
    private bool _ready;

    private static readonly Brush Done = new SolidColorBrush(Color.FromRgb(197, 169, 119));
    private static readonly Brush Pending = new SolidColorBrush(Color.FromRgb(166, 164, 159));
    private static readonly Brush Problem = new SolidColorBrush(Color.FromRgb(224, 122, 104));

    public MainWindow() : this(false) { }

    public MainWindow(bool justUpdated)
    {
        _justUpdated = justUpdated;
        InitializeComponent();
        TitleVersion.Text = Releases.IsDevBuild ? "development build" : "v" + Releases.CurrentVersion;
        (_settings.UpdateChannel == "preview" ? PreviewChannel : StableChannel).IsChecked = true;
        AutoUpdateCheck.IsChecked = _settings.CheckForUpdates;
        _ready = true;
        ShowFolders();
        Loaded += async (_, _) =>
        {
            await RefreshAsync();
            if (_justUpdated && _next == NextStep.Build && !string.IsNullOrEmpty(_settings.BuiltXexHash))
            {
                Footer("Updated to v" + Releases.CurrentVersion + ". Building the update.");
                await BuildAsync();
            }
            else if (_settings.CheckForUpdates && !Releases.IsDevBuild)
            {
                await CheckForUpdatesAsync(quiet: true);
            }
        };
    }

    // ---- State -------------------------------------------------------------

    private async Task RefreshAsync()
    {
        _game = await GameValidator.ValidateAsync(_settings.GameFolder);
        _msvc = await Task.Run(Toolchain.DetectMsvc);
        _tools = Toolchain.FindInstalled();
        _port = PortPackage.FindInstalled(Releases.CurrentVersion);
        var portVersion = _port?.Version ?? Releases.CurrentVersion;
        var built = GameLauncher.IsInstalled(_settings);
        var upToDate = built && _settings.BuiltPortVersion == portVersion;

        Mark(StepGameMark, "1", _game.Valid ? true : string.IsNullOrEmpty(_settings.GameFolder) ? null : false);
        StepGameDetail.Text = _game.Valid ? _settings.GameFolder : _game.Heading;
        StepGameDetail.ToolTip = _game.Valid ? _settings.GameFolder : _game.Detail;

        var toolsReady = _msvc.Ready && _tools != null;
        Mark(StepToolsMark, "2", toolsReady ? true : null);
        StepToolsDetail.Text = toolsReady ? "Installed" : MissingToolsText();

        Mark(StepBuildMark, "3", upToDate ? true : null);
        StepBuildDetail.Text = upToDate ? "Ready" : built ? "An update needs a quick rebuild" : "Not built yet";

        _next = _gameProcess != null ? NextStep.Running
            : !_game.Valid ? NextStep.ChooseGame
            : !toolsReady ? NextStep.InstallTools
            : !upToDate ? NextStep.Build
            : NextStep.Play;
        PrimaryButton.Content = _next switch
        {
            NextStep.ChooseGame => "CHOOSE GAME FOLDER",
            NextStep.InstallTools => "INSTALL BUILD TOOLS",
            NextStep.Build => built ? "UPDATE GAME" : "BUILD GAME",
            NextStep.Running => "RUNNING",
            _ => "PLAY",
        };
        PrimaryButton.IsEnabled = _operation == null && _next != NextStep.Running;
        PrimaryHint.Text = _next switch
        {
            NextStep.ChooseGame => string.IsNullOrEmpty(_settings.GameFolder)
                ? "Choose the folder you copied the Xbox 360 game to. Help has a guide."
                : _game.Detail,
            NextStep.InstallTools => _msvc.Ready
                ? "One-time download into the launcher's own folder. Nothing is installed system-wide."
                : "One-time setup. Windows asks for permission once, for Microsoft's installer.",
            NextStep.Build => built ? "Rebuilds only what changed." : "Takes 5 to 20 minutes the first time.",
            NextStep.Running => "The game is running.",
            _ => ModsSummary(),
        };
        if (_operation == null) Footer(_next == NextStep.Play ? "Ready." : "");
    }

    private string MissingToolsText()
    {
        var missing = new List<string>();
        if (!_msvc.Ready) missing.Add("Microsoft C++ Build Tools (about 3 GB)");
        if (_tools == null) missing.Add("compiler tools (about 130 MB)");
        return "Needs " + string.Join(" and ", missing);
    }

    private string ModsSummary()
    {
        var count = ModStore.EnabledFolders(_settings).Count;
        return count == 0 ? "F1 opens the settings menu in game." : $"{count} mod{(count == 1 ? "" : "s")} enabled.";
    }

    private static void Mark(TextBlock mark, string number, bool? state)
    {
        mark.Text = state == true ? "✓" : state == false ? "!" : number;
        mark.Foreground = state == true ? Done : state == false ? Problem : Pending;
    }

    private void Footer(string text) => FooterStatus.Text = text;

    // ---- Main action --------------------------------------------------------

    private async void Primary_Click(object sender, RoutedEventArgs e)
    {
        switch (_next)
        {
            case NextStep.ChooseGame: await ChooseGameFolderAsync(); break;
            case NextStep.InstallTools: await InstallToolsAsync(); break;
            case NextStep.Build: await BuildAsync(); break;
            case NextStep.Play: Play(); break;
        }
    }

    private async Task ChooseGameFolderAsync()
    {
        var dialog = new OpenFolderDialog { Title = "Choose the game folder (the one with default.xex)" };
        if (Directory.Exists(_settings.GameFolder)) dialog.InitialDirectory = _settings.GameFolder;
        if (dialog.ShowDialog(this) != true) return;
        var result = await GameValidator.ValidateAsync(dialog.FolderName);
        if (!result.Valid)
        {
            MessageBox.Show(this, result.Detail, result.Heading, MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        var previous = _settings.GameFolder;
        _settings.GameFolder = dialog.FolderName;
        try
        {
            BuildPipeline.ValidateDestinations(_settings);
        }
        catch (InvalidOperationException ex)
        {
            _settings.GameFolder = previous;
            MessageBox.Show(this, ex.Message, "Choose another folder", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        SettingsStore.Save(_settings);
        ShowFolders();
        await RefreshAsync();
    }

    private async Task InstallToolsAsync(bool reinstall = false)
    {
        await RunAsync("Installing build tools", async token =>
        {
            if (!_msvc.Ready)
            {
                SetProgress("Microsoft C++ Build Tools", null, "Microsoft's installer shows its own progress window.");
                await Toolchain.InstallMsvcAsync(detail => SetProgress(detail, null, ""), token);
                _msvc = Toolchain.DetectMsvc();
                if (!_msvc.Ready)
                    throw new InvalidOperationException("Microsoft's installer finished but the C++ tools were not found (" +
                                                        _msvc.Detail + "). Try again, or restart Windows first.");
            }
            if (_tools == null || reinstall)
            {
                _tools = await Setup.InstallToolchainAsync(_settings.UpdateChannel,
                    DownloadProgress("Downloading compiler tools"), detail => SetProgress(detail, null, ""), token);
            }
        });
    }

    private async Task BuildAsync()
    {
        await RunAsync("Building the game", async token =>
        {
            _port ??= await Setup.EnsurePortAsync(_settings.UpdateChannel, DownloadProgress("Downloading port files"), token);
            var progress = new Progress<BuildUpdate>(update =>
                SetProgress(update.Stage, update.Fraction, update.Detail));
            await BuildPipeline.BuildAsync(_settings, _port, _tools!, progress, AppendLog, token);
        });
    }

    private void Play()
    {
        try
        {
            var process = GameLauncher.Launch(_settings, _port?.Version ?? Releases.CurrentVersion,
                ModStore.EnabledFolders(_settings));
            _gameProcess = process;
            Footer("Game running.");
            _ = WatchGameAsync(process);
            _ = RefreshAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "The game did not start", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private async Task WatchGameAsync(Process process)
    {
        try
        {
            await process.WaitForExitAsync();
            Footer(process.ExitCode == 0 ? "Game closed." : $"The game stopped unexpectedly (code {process.ExitCode}). Settings > Open logs.");
        }
        finally
        {
            process.Dispose();
            _gameProcess = null;
            await RefreshAsync();
        }
    }

    // Runs one long task with the progress area, cancel button and error text.
    private async Task RunAsync(string title, Func<CancellationToken, Task> work)
    {
        if (_operation != null) return;
        _operation = new CancellationTokenSource();
        PrimaryButton.IsEnabled = false;
        CancelButton.Visibility = Visibility.Visible;
        ErrorText.Visibility = Visibility.Collapsed;
        ProgressArea.Visibility = Visibility.Visible;
        SetProgress(title, null, "");
        Footer(title + "…");
        try
        {
            await work(_operation.Token);
            Footer("Done.");
        }
        catch (OperationCanceledException)
        {
            Footer("Cancelled.");
        }
        catch (Exception ex)
        {
            ErrorText.Text = ex.Message;
            ErrorText.Visibility = Visibility.Visible;
            AppendLog("ERROR: " + ex);
            Footer("Something went wrong. The build log has details.");
        }
        finally
        {
            _operation.Dispose();
            _operation = null;
            CancelButton.Visibility = Visibility.Collapsed;
            ProgressArea.Visibility = Visibility.Collapsed;
            await RefreshAsync();
        }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) => _operation?.Cancel();

    private void SetProgress(string stage, double? fraction, string detail)
    {
        ProgressStage.Text = stage;
        Progress.IsIndeterminate = fraction == null;
        if (fraction is { } f) Progress.Value = Math.Clamp(f, 0, 1);
        ProgressPercent.Text = fraction is { } p ? $"{p:P0}" : "";
        ProgressDetail.Text = detail;
    }

    private IProgress<DownloadProgress> DownloadProgress(string stage) => new Progress<DownloadProgress>(p =>
    {
        var detail = p.Total > 0
            ? $"{Downloader.Size(p.Received)} of {Downloader.Size(p.Total)}"
            : Downloader.Size(p.Received);
        if (p.BytesPerSecond > 0) detail += $"  ·  {Downloader.Size((long)p.BytesPerSecond)}/s";
        SetProgress(stage, p.Total > 0 ? (double)p.Received / p.Total : null, detail);
    });

    private void AppendLog(string line)
    {
        Dispatcher.BeginInvoke(() =>
        {
            BuildLog.AppendText(line + Environment.NewLine);
            if (BuildLog.Text.Length > 200_000) BuildLog.Text = BuildLog.Text[^150_000..];
            if (LogPanel.Visibility == Visibility.Visible) BuildLog.ScrollToEnd();
        });
    }

    private void LogToggle_Click(object sender, RoutedEventArgs e)
    {
        var show = LogPanel.Visibility != Visibility.Visible;
        LogPanel.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        LogToggle.Content = show ? "Hide build log" : "Show build log";
        if (show) BuildLog.ScrollToEnd();
    }

    // ---- Updates ------------------------------------------------------------

    private async Task CheckForUpdatesAsync(bool quiet)
    {
        UpdateStatus.Text = "Checking…";
        try
        {
            _latest = await Releases.LatestAsync(_settings.UpdateChannel, CancellationToken.None);
            var newer = _latest != null && !Releases.IsDevBuild && Releases.Compare(_latest.Version, Releases.CurrentVersion) > 0;
            UpdateStatus.Text = _latest == null ? "No releases yet."
                : newer ? $"Version {_latest.Version} is available."
                : Releases.IsDevBuild ? $"Latest release: {_latest.Version} (this is a development build)."
                : "You have the latest version.";
            UpdateBanner.Visibility = newer ? Visibility.Visible : Visibility.Collapsed;
            if (newer) UpdateText.Text = $"Version {_latest!.Version} is available.";
        }
        catch (Exception ex) when (ex is System.Net.Http.HttpRequestException or TaskCanceledException or IOException)
        {
            UpdateStatus.Text = quiet ? "" : "Could not reach GitHub. Check your connection.";
        }
    }

    private async void CheckUpdates_Click(object sender, RoutedEventArgs e) => await CheckForUpdatesAsync(quiet: false);

    private async void Update_Click(object sender, RoutedEventArgs e)
    {
        if (_latest == null) return;
        var release = _latest;
        var restart = false;
        PlayNav.IsChecked = true;
        await RunAsync("Updating to " + release.Version, async token =>
        {
            restart = await Releases.ReplaceLauncherAsync(release, DownloadProgress("Downloading the new launcher"), token);
        });
        if (restart)
        {
            Releases.RestartLauncher();
            Application.Current.Shutdown();
        }
    }

    private void ReleaseNotes_Click(object sender, RoutedEventArgs e) =>
        OpenUrl(_latest?.HtmlUrl ?? Paths.RepositoryUrl + "/releases");

    private void Channel_Checked(object sender, RoutedEventArgs e)
    {
        if (!_ready || sender is not RadioButton { IsChecked: true } choice) return;
        _settings.UpdateChannel = (string)choice.Tag;
        SettingsStore.Save(_settings);
        UpdateBanner.Visibility = Visibility.Collapsed;
        UpdateStatus.Text = "";
    }

    private void AutoUpdate_Click(object sender, RoutedEventArgs e)
    {
        _settings.CheckForUpdates = AutoUpdateCheck.IsChecked == true;
        SettingsStore.Save(_settings);
    }

    // ---- Mods ---------------------------------------------------------------

    private void RefreshMods()
    {
        InstalledMods.Children.Clear();
        var installed = ModStore.Installed();
        NoMods.Visibility = installed.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        _settings.EnabledMods.RemoveAll(id => installed.All(m => !m.Manifest.Id.Equals(id, StringComparison.OrdinalIgnoreCase)));
        var ordered = _settings.EnabledMods
            .Select(id => installed.First(m => m.Manifest.Id.Equals(id, StringComparison.OrdinalIgnoreCase)))
            .Concat(installed.Where(m => !_settings.EnabledMods.Contains(m.Manifest.Id, StringComparer.OrdinalIgnoreCase))
                .OrderBy(m => m.Manifest.Name))
            .ToList();
        foreach (var mod in ordered) InstalledMods.Children.Add(ModRow(mod));
    }

    private FrameworkElement ModRow(InstalledMod mod)
    {
        var id = mod.Manifest.Id;
        var enabledIndex = _settings.EnabledMods.FindIndex(m => m.Equals(id, StringComparison.OrdinalIgnoreCase));
        var grid = new Grid { Margin = new Thickness(0, 0, 0, 12) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition());
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var toggle = new CheckBox { IsChecked = enabledIndex >= 0, VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(0, 3, 12, 0), ToolTip = "Enabled" };
        toggle.Click += (_, _) =>
        {
            if (toggle.IsChecked == true) _settings.EnabledMods.Add(id);
            else _settings.EnabledMods.RemoveAll(m => m.Equals(id, StringComparison.OrdinalIgnoreCase));
            SettingsStore.Save(_settings);
            RefreshMods();
        };
        grid.Children.Add(toggle);

        var text = new StackPanel();
        var title = new TextBlock { FontWeight = FontWeights.SemiBold };
        title.Inlines.Add(mod.Manifest.Name);
        if (mod.Manifest.Version.Length > 0) title.Inlines.Add(new System.Windows.Documents.Run("  " + mod.Manifest.Version) { Foreground = Pending, FontWeight = FontWeights.Normal });
        text.Children.Add(title);
        var detail = string.Join(" · ", new[] { mod.Manifest.Author.Length > 0 ? "by " + mod.Manifest.Author : "", mod.Manifest.Description }.Where(s => s.Length > 0));
        if (detail.Length > 0) text.Children.Add(new TextBlock { Text = detail, Foreground = Pending, TextWrapping = TextWrapping.Wrap });
        Grid.SetColumn(text, 1);
        grid.Children.Add(text);

        var buttons = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Top };
        if (enabledIndex >= 0)
        {
            buttons.Children.Add(SmallButton("▲", "Load earlier (wins over mods below)", enabledIndex > 0, () => MoveMod(enabledIndex, -1)));
            buttons.Children.Add(SmallButton("▼", "Load later", enabledIndex < _settings.EnabledMods.Count - 1, () => MoveMod(enabledIndex, 1)));
        }
        buttons.Children.Add(SmallButton("Remove", "Delete this mod", true, () =>
        {
            if (MessageBox.Show(this, $"Remove {mod.Manifest.Name}?", "Remove mod", MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
            ModStore.Remove(mod, _settings);
            SettingsStore.Save(_settings);
            RefreshMods();
        }));
        Grid.SetColumn(buttons, 2);
        grid.Children.Add(buttons);
        return grid;
    }

    private Button SmallButton(string content, string tooltip, bool enabled, Action action)
    {
        var button = new Button
        {
            Content = content, ToolTip = tooltip, IsEnabled = enabled, Margin = new Thickness(6, 0, 0, 0),
            Style = (Style)FindResource("Secondary"), Padding = new Thickness(10, 4, 10, 4),
        };
        button.Click += (_, _) => action();
        return button;
    }

    private void MoveMod(int index, int delta)
    {
        var id = _settings.EnabledMods[index];
        _settings.EnabledMods.RemoveAt(index);
        _settings.EnabledMods.Insert(index + delta, id);
        SettingsStore.Save(_settings);
        RefreshMods();
    }

    private void AddMod_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Title = "Choose a mod (.zip)", Filter = "Mod archive (*.zip)|*.zip" };
        if (dialog.ShowDialog(this) != true) return;
        try
        {
            var mod = ModStore.InstallFromZip(dialog.FileName);
            if (!_settings.EnabledMods.Contains(mod.Manifest.Id, StringComparer.OrdinalIgnoreCase))
                _settings.EnabledMods.Insert(0, mod.Manifest.Id);
            SettingsStore.Save(_settings);
            RefreshMods();
        }
        catch (Exception ex) when (ex is InvalidDataException or IOException or UnauthorizedAccessException)
        {
            MessageBox.Show(this, ex.Message, "Could not add the mod", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private async void BrowseMods_Click(object sender, RoutedEventArgs e)
    {
        ModCatalogCard.Visibility = Visibility.Visible;
        ModCatalog.Children.Clear();
        ModCatalogStatus.Text = "Loading…";
        ModCatalogStatus.Visibility = Visibility.Visible;
        try
        {
            var entries = await ModStore.FetchIndexAsync(CancellationToken.None);
            ModCatalogStatus.Text = entries.Count == 0 ? "No mods are listed yet." : "";
            ModCatalogStatus.Visibility = entries.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            var installed = ModStore.Installed().ToDictionary(m => m.Manifest.Id, StringComparer.OrdinalIgnoreCase);
            foreach (var entry in entries) ModCatalog.Children.Add(CatalogRow(entry, installed.GetValueOrDefault(entry.Id)));
        }
        catch (Exception ex) when (ex is System.Net.Http.HttpRequestException or TaskCanceledException or System.Text.Json.JsonException or KeyNotFoundException)
        {
            ModCatalogStatus.Text = "Could not load the mod list. Check your connection.";
        }
    }

    private FrameworkElement CatalogRow(ModIndexEntry entry, InstalledMod? installed)
    {
        var grid = new Grid { Margin = new Thickness(0, 0, 0, 12) };
        grid.ColumnDefinitions.Add(new ColumnDefinition());
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var text = new StackPanel();
        text.Children.Add(new TextBlock { Text = $"{entry.Name}  {entry.Version}", FontWeight = FontWeights.SemiBold });
        text.Children.Add(new TextBlock
        {
            Text = string.Join(" · ", new[] { entry.Author.Length > 0 ? "by " + entry.Author : "", entry.Description, entry.Size > 0 ? Downloader.Size(entry.Size) : "" }.Where(s => s.Length > 0)),
            Foreground = Pending, TextWrapping = TextWrapping.Wrap,
        });
        grid.Children.Add(text);
        var current = installed != null && installed.Manifest.Version == entry.Version;
        var button = new Button
        {
            Content = current ? "Installed" : installed != null ? "Update" : "Install",
            IsEnabled = !current, Style = (Style)FindResource("Secondary"), VerticalAlignment = VerticalAlignment.Top,
        };
        button.Click += async (_, _) =>
        {
            button.IsEnabled = false;
            button.Content = "Downloading…";
            try
            {
                var progress = new Progress<DownloadProgress>(p => button.Content = p.Total > 0 ? $"{p.Received * 100 / p.Total}%" : "Downloading…");
                var mod = await ModStore.InstallFromIndexAsync(entry, progress, CancellationToken.None);
                if (!_settings.EnabledMods.Contains(mod.Manifest.Id, StringComparer.OrdinalIgnoreCase))
                    _settings.EnabledMods.Insert(0, mod.Manifest.Id);
                SettingsStore.Save(_settings);
                button.Content = "Installed";
                RefreshMods();
            }
            catch (Exception ex)
            {
                button.Content = "Install";
                button.IsEnabled = true;
                MessageBox.Show(this, ex.Message, "Could not install the mod", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        };
        Grid.SetColumn(button, 1);
        grid.Children.Add(button);
        return grid;
    }

    private void OpenModsFolder_Click(object sender, RoutedEventArgs e)
    {
        Directory.CreateDirectory(Paths.Mods);
        OpenFolder(Paths.Mods);
    }

    // ---- Settings -----------------------------------------------------------

    private void ShowFolders()
    {
        GameFolderText.Text = string.IsNullOrEmpty(_settings.GameFolder) ? "Not chosen" : _settings.GameFolder;
        InstallFolderText.Text = _settings.InstallRoot;
        UserFolderText.Text = _settings.UserRoot;
        CacheFolderText.Text = _settings.CacheRoot;
    }

    private async void ChangeFolder_Click(object sender, RoutedEventArgs e)
    {
        var which = (string)((Button)sender).Tag;
        if (which == "game")
        {
            await ChooseGameFolderAsync();
            return;
        }
        var dialog = new OpenFolderDialog { Title = "Choose a folder" };
        if (dialog.ShowDialog(this) != true) return;
        var previous = (_settings.InstallRoot, _settings.UserRoot, _settings.CacheRoot);
        switch (which)
        {
            case "install": _settings.InstallRoot = dialog.FolderName; break;
            case "user": _settings.UserRoot = dialog.FolderName; break;
            case "cache": _settings.CacheRoot = dialog.FolderName; break;
        }
        try
        {
            if (!string.IsNullOrEmpty(_settings.GameFolder)) BuildPipeline.ValidateDestinations(_settings);
        }
        catch (InvalidOperationException ex)
        {
            (_settings.InstallRoot, _settings.UserRoot, _settings.CacheRoot) = previous;
            MessageBox.Show(this, ex.Message, "Choose another folder", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        SettingsStore.Save(_settings);
        ShowFolders();
        await RefreshAsync();
    }

    private async void Rebuild_Click(object sender, RoutedEventArgs e)
    {
        if (_next is NextStep.ChooseGame or NextStep.InstallTools)
        {
            MessageBox.Show(this, "Finish the setup steps on Play first.", "Rebuild", MessageBoxButton.OK);
            return;
        }
        PlayNav.IsChecked = true;
        await BuildAsync();
    }

    private async void ReinstallTools_Click(object sender, RoutedEventArgs e)
    {
        PlayNav.IsChecked = true;
        await InstallToolsAsync(reinstall: true);
    }

    private void OpenLogs_Click(object sender, RoutedEventArgs e)
    {
        var logs = Path.Combine(_settings.UserRoot, "logs");
        Directory.CreateDirectory(logs);
        OpenFolder(logs);
    }

    private void Shortcut_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Shortcuts.Create(Environment.SpecialFolder.DesktopDirectory);
            Shortcuts.Create(Environment.SpecialFolder.Programs);
            Footer("Shortcuts added to the desktop and Start menu.");
        }
        catch (Exception ex) when (ex is COMException or IOException or UnauthorizedAccessException)
        {
            MessageBox.Show(this, ex.Message, "Could not create the shortcut", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    // ---- Help and window -----------------------------------------------------

    private void OpenDoc_Click(object sender, RoutedEventArgs e) =>
        OpenUrl($"{Paths.RepositoryUrl}/blob/main/{(string)((Button)sender).Tag}");
    private void OpenRepo_Click(object sender, RoutedEventArgs e) =>
        OpenUrl($"{Paths.RepositoryUrl}/{(string)((Button)sender).Tag}");

    private static void OpenUrl(string url)
    {
        if (url.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
    }

    private static void OpenFolder(string folder) =>
        Process.Start(new ProcessStartInfo("explorer.exe") { ArgumentList = { folder }, UseShellExecute = false });

    // For --page (screenshots, support instructions).
    public void ShowPage(string page)
    {
        var nav = page switch { "mods" => ModsNav, "settings" => SettingsNav, "help" => HelpNav, _ => PlayNav };
        nav.IsChecked = true;
    }

    private void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (PlayPanel == null) return;
        var panel = (string)((RadioButton)sender).Tag;
        PlayPanel.Visibility = panel == "play" ? Visibility.Visible : Visibility.Collapsed;
        ModsPanel.Visibility = panel == "mods" ? Visibility.Visible : Visibility.Collapsed;
        SettingsPanel.Visibility = panel == "settings" ? Visibility.Visible : Visibility.Collapsed;
        HelpPanel.Visibility = panel == "help" ? Visibility.Visible : Visibility.Collapsed;
        if (panel == "mods") RefreshMods();
        if (panel == "play" && _ready) _ = RefreshAsync();
    }

    private void TitleBar_Drag(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2) ToggleMaximize();
        else DragMove();
    }

    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Maximize_Click(object sender, RoutedEventArgs e) => ToggleMaximize();
    private void ToggleMaximize() =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void Close_Click(object sender, RoutedEventArgs e)
    {
        if (_operation != null &&
            MessageBox.Show(this, "Stop the current task and close?", "MKVDCU-Recomp", MessageBoxButton.YesNo) != MessageBoxResult.Yes)
            return;
        _operation?.Cancel();
        Close();
    }

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
}
