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

    try
    {
        var psi = new ProcessStartInfo("lscpu")
        {
            RedirectStandardOutput = true,
        };

        var p = Process.Start(psi)!;
        p.WaitForExit();
        var output = p.StandardOutput.ReadToEnd();
        sb.AppendLine(output);
    }
    catch (Exception ex)
    {
        sb.AppendLine("Failed to start lscpu:");
        sb.AppendLine(ex.ToString());
    }
    return sb.ToString();
}
