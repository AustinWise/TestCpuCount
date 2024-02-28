using System.Diagnostics;
using System.Text;

var builder = WebApplication.CreateSlimBuilder(args);

var app = builder.Build();

app.MapGet("/", () => GetInfo());

app.Run();

static string GetInfo()
{
    var sb = new StringBuilder();
    sb.AppendLine($"CPU Count: {Environment.ProcessorCount}");
    sb.AppendLine();

    try
    {
        var psi = new ProcessStartInfo("lscpu")
        {
            RedirectStandardOutput = true,
        };

        var p = Process.Start(psi);
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
