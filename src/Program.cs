using System.Diagnostics;
using System.Reflection;
using System.Runtime;
using System.Text;


class Program
{
    static readonly string cgrouperPath = Path.Combine(Path.GetDirectoryName(typeof(Program).Assembly.Location), "cgrouper");

    static void Main(string[] args)
    {
        var builder = WebApplication.CreateSlimBuilder(args);


        var app = builder.Build();

        try
        {
            Process.Start("chmod", $"+x {cgrouperPath}");
        }
        catch (Exception ex)
        {
            app.Logger.LogError(ex.ToString());
        }

        app.MapGet("/", GetInfo);
        app.MapGet("/readfile", (string path) => File.ReadAllText(path));
        app.MapGet("/listdir", (string path) =>
        {
            var di = new DirectoryInfo(path);
            var sb = new StringBuilder();
            foreach (var fi in di.GetFileSystemInfos().OrderBy(f => f.Name))
            {
                sb.Append(fi.Name);
                if (fi is DirectoryInfo)
                {
                    sb.Append('/');
                }
                sb.AppendLine();
            }
            return sb.ToString();
        });

        string? port = Environment.GetEnvironmentVariable("PORT");

        if (string.IsNullOrEmpty(port))
        {
            app.Run();
        }
        else
        {
            app.Run($"http://0.0.0.0:" + port);
        }
    }

    static string GetInfo()
    {
        var sb = new StringBuilder();
        sb.AppendLine($"Environment.ProcessorCount: {Environment.ProcessorCount}");
        sb.AppendLine($"GCSettings.IsServerGC: {GCSettings.IsServerGC}");

        sb.AppendLine(RunProgram("lscpu"));
        sb.AppendLine();
        sb.AppendLine(RunProgram("mount"));
        sb.AppendLine();
        sb.AppendLine(RunProgram("stat", "-f /sys/fs/cgroup"));
        sb.AppendLine();
        sb.AppendLine(RunProgram(cgrouperPath));
        sb.AppendLine();
        return sb.ToString();
    }

    static string RunProgram(string program, string? args = null)
    {
        try
        {
            var psi = new ProcessStartInfo(program)
            {
                RedirectStandardOutput = true,
            };
            if (args != null)
            {
                psi.Arguments = args;
            }

            var p = Process.Start(psi)!;
            p.WaitForExit();
            return p.StandardOutput.ReadToEnd();
        }
        catch (Exception ex)
        {
            return $"Failed to start {program}: {ex}";
        }
    }

    async static Task<string> getInstanceInfo(string instanceAttribute)
    {
        var client = new HttpClient();
        client.DefaultRequestHeaders.Add("Metadata-Flavor", "Google");
        try
        {
            return await client.GetStringAsync("http://metadata.google.internal/computeMetadata/v1/instance/" + instanceAttribute);
        }
        catch (Exception ex)
        {
            return ex.ToString();
        }
    }

}