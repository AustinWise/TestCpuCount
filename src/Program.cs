using System.Diagnostics;
using System.Runtime;
using System.Text;

var builder = WebApplication.CreateSlimBuilder(args);

var app = builder.Build();

app.MapGet("/", GetInfo);

string? port = Environment.GetEnvironmentVariable("PORT");

if (string.IsNullOrEmpty(port))
{
    app.Run();
}
else
{
    app.Run($"http://0.0.0.0:" + port);
}

static async Task<string> GetInfo()
{
    var sb = new StringBuilder();
    sb.AppendLine($"CPU Count: {Environment.ProcessorCount}");
    sb.AppendLine($"GCSettings.IsServerGC: {GCSettings.IsServerGC}");
    string cpuPlatform = await getInstanceInfo("cpu-platform");
    string machineType = await getInstanceInfo("machine-type");
    string directory = await getInstanceInfo("");
    sb.AppendLine($"instance data directory: {directory}");
    sb.AppendLine($"cpu-platform: {cpuPlatform}");
    sb.AppendLine($"machine-type: {machineType}");
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