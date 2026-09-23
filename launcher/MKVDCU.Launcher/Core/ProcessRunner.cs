using System.Diagnostics;
using System.IO;
using System.Text;

namespace MKVDCU.Launcher.Core;

internal static class ProcessRunner
{
    // Runs a console tool without a window, streaming its output lines.
    // Cancelling kills the whole process tree.
    public static async Task<int> RunAsync(string exe, IEnumerable<string> arguments, string workingDirectory,
        Action<string> onLine, CancellationToken token, IDictionary<string, string>? environment = null)
    {
        var psi = new ProcessStartInfo(exe)
        {
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        foreach (var argument in arguments) psi.ArgumentList.Add(argument);
        if (environment != null)
            foreach (var (key, value) in environment) psi.Environment[key] = value;

        using var process = new Process { StartInfo = psi };
        process.OutputDataReceived += (_, e) => { if (e.Data != null) onLine(e.Data); };
        process.ErrorDataReceived += (_, e) => { if (e.Data != null) onLine(e.Data); };
        if (!process.Start()) throw new InvalidOperationException("Could not start " + Path.GetFileName(exe));
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        try
        {
            await process.WaitForExitAsync(token);
        }
        catch (OperationCanceledException)
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); } catch (InvalidOperationException) { }
            throw;
        }
        return process.ExitCode;
    }

    public static string? Capture(string exe, params string[] arguments)
    {
        try
        {
            var psi = new ProcessStartInfo(exe)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
            };
            foreach (var argument in arguments) psi.ArgumentList.Add(argument);
            using var process = Process.Start(psi);
            if (process == null) return null;
            var output = process.StandardOutput.ReadToEnd();
            process.WaitForExit(15_000);
            return process.ExitCode == 0 ? output.Trim() : null;
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or IOException)
        {
            return null;
        }
    }
}
