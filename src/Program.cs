using System.Diagnostics;
using System.Runtime;
using System.Text;

var builder = WebApplication.CreateSlimBuilder(args);

var app = builder.Build();

app.MapGet("/", () => GetInfo());

string? port = Environment.GetEnvironmentVariable("PORT");

if (string.IsNullOrEmpty(port))
{
    app.Run();
}
else
{
    app.Run($"http://0.0.0.0:" + port);
}

static string GetInfo()
{
    var sb = new StringBuilder();
    sb.AppendLine($"CPU Count: {Environment.ProcessorCount}");
    sb.AppendLine($"GCSettings.IsServerGC: {GCSettings.IsServerGC}");
    sb.AppendLine();

    sb.AppendLine(RunProgram("lscpu"));
    sb.AppendLine();
    sb.AppendLine(RunProgram("env"));
    return sb.ToString();
}

static string RunProgram(string program)
{
    try
    {
        var psi = new ProcessStartInfo(program)
        {
            RedirectStandardOutput = true,
        };

        var p = Process.Start(psi)!;
        p.WaitForExit();
        return p.StandardOutput.ReadToEnd();
    }
    catch (Exception ex)
    {
        return $"Failed to start {program}: {ex}";
    }
}