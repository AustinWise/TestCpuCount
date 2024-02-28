using System.Text;

var builder = WebApplication.CreateSlimBuilder(args);

var app = builder.Build();

var sb = new StringBuilder();

app.MapGet("/", () => "Hi from .NET");

app.Run();
