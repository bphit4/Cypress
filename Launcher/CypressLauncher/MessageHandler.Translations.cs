#nullable enable
using System;
using System.IO;
using System.Linq;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

public partial class MessageHandler
{
    private static string GetTranslationsDir() =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cypress", "Translations");

    private void OnGetTranslations(string lang)
    {
        if (string.IsNullOrWhiteSpace(lang) || !System.Text.RegularExpressions.Regex.IsMatch(lang, @"^[a-zA-Z0-9_\-]{1,32}$"))
            lang = "en-US";

        string appdataDir = GetTranslationsDir();
        string appdataFile = Path.Combine(appdataDir, lang + ".json");
        string bundledFile = Path.Combine(AppContext.BaseDirectory, "assets", "translations", lang + ".json");

        JObject? strings = null;

        if (File.Exists(bundledFile))
        {
            try { strings = JObject.Parse(File.ReadAllText(bundledFile)); } catch { }

            // overwrite AppData so users always get the latest bundled translations
            try
            {
                Directory.CreateDirectory(appdataDir);
                File.Copy(bundledFile, appdataFile, overwrite: true);
            }
            catch { }
        }
        else if (File.Exists(appdataFile))
        {
            // user-provided translation with no bundled equivalent
            try { strings = JObject.Parse(File.ReadAllText(appdataFile)); } catch { }
        }

        if (strings == null)
        {
            if (lang != "en-US") { OnGetTranslations("en-US"); return; }
            Send(new JObject { ["type"] = "translations", ["lang"] = "en-US", ["strings"] = new JObject() });
            return;
        }

        // merge en-US as fallback for any keys missing from this language
        if (lang != "en-US")
        {
            string enFile = Path.Combine(AppContext.BaseDirectory, "assets", "translations", "en-US.json");
            if (File.Exists(enFile))
            {
                try
                {
                    var en = JObject.Parse(File.ReadAllText(enFile));
                    foreach (var prop in en.Properties())
                    {
                        if (strings[prop.Name] == null)
                            strings[prop.Name] = prop.Value;
                    }
                }
                catch { }
            }
        }

        Send(new JObject { ["type"] = "translations", ["lang"] = lang, ["strings"] = strings });
    }

    private void OnGetTranslationsList()
    {
        var langs = new System.Collections.Generic.Dictionary<string, JObject>();

        void ReadDir(string dir)
        {
            if (!Directory.Exists(dir)) return;
            foreach (string f in Directory.GetFiles(dir, "*.json"))
            {
                string lang = Path.GetFileNameWithoutExtension(f);
                if (langs.ContainsKey(lang)) continue;
                var entry = new JObject { ["lang"] = lang };
                try
                {
                    var j = JObject.Parse(File.ReadAllText(f));
                    var meta = j["_meta"] as JObject;
                    entry["name"] = meta?["name"]?.Value<string>() ?? lang;
                    string? author = meta?["author"]?.Value<string>();
                    if (!string.IsNullOrEmpty(author)) entry["author"] = author;
                }
                catch { entry["name"] = lang; }
                langs[lang] = entry;
            }
        }

        ReadDir(Path.Combine(AppContext.BaseDirectory, "assets", "translations"));
        ReadDir(GetTranslationsDir());

        Send(new JObject
        {
            ["type"] = "translationsList",
            ["langs"] = new JArray(langs.Values.OrderBy(e => (string?)e["lang"]))
        });
    }
}
