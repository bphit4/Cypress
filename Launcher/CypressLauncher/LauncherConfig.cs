#nullable enable
using System;
using System.IO;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

internal static class LauncherConfig
{
    public static readonly string MasterUrl = "https://api-cypress.v0e.dev";
    public static readonly string DefaultRelayAddress = "relay.v0e.dev:25200";

    static LauncherConfig()
    {
        try
        {
            string path = Path.Combine(AppContext.BaseDirectory, "launcher.config.json");
            if (!File.Exists(path)) return;

            var j = JObject.Parse(File.ReadAllText(path));
            if (j["masterUrl"] is JToken mu && mu.Type == JTokenType.String)
                MasterUrl = mu.Value<string>()!;
            if (j["defaultRelayAddress"] is JToken ra && ra.Type == JTokenType.String)
                DefaultRelayAddress = ra.Value<string>()!;
        }
        catch { }
    }
}
