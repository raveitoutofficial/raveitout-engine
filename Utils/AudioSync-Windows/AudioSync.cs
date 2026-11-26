// Rave It Out — Audio Sync (Windows)
// Companion utility: keep a list of audio-device profiles (speakers, headphones,
// Bluetooth...) each with its own global offset, and write the chosen one into
// the game's Save/Preferences.ini before starting Rave It Out.
// Windows port of audio-sync/Sources/AudioSync.swift: same Save/AudioSyncProfiles.json
// and Preferences.ini formats, so profiles move between the Mac and Windows builds.
// Public domain (Unlicense), like the rest of the Rave It Out build.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace RaveItOut.AudioSync
{
    // MARK: - Data

    class AudioProfile
    {
        public string Id = Guid.NewGuid().ToString().ToUpperInvariant();
        public string Name = "";
        public double Offset;          // seconds, same unit as GlobalOffsetSeconds
        public string Device;          // output device this profile is linked to (null = none)
    }

    static class Fmt
    {
        static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
        public static bool Same(double a, double b) => Math.Abs(a - b) < 0.0000005;

        /// "+0.050", "-0.181", "0.000" (like the Mac's formatOffset).
        public static string Offset(double v, int digits = 3)
        {
            if (Math.Abs(v) < 0.0000005) v = 0;
            return (v > 0 ? "+" : "") + v.ToString("F" + digits, Inv);
        }

        /// 3 to 6 decimals, "+" for positive (the Mac's offset text field).
        public static string Field(double v)
        {
            if (Math.Abs(v) < 0.0000005) return "0.000";
            string s = v.ToString("F6", Inv).TrimEnd('0');
            int dot = s.IndexOf('.');
            while (s.Length - dot - 1 < 3) s += "0";
            return (v > 0 ? "+" : "") + s;
        }

        public static bool TryParse(string text, out double v)
        {
            string t = (text ?? "").Trim().Replace('−', '-');
            if (t.EndsWith("s")) t = t.Substring(0, t.Length - 1).Trim();
            if (t.StartsWith("+")) t = t.Substring(1);
            return double.TryParse(t, NumberStyles.Float, Inv, out v)
                || double.TryParse(t, NumberStyles.Float, CultureInfo.CurrentCulture, out v);
        }

        public static long RoundAway(double v) => (long)Math.Round(v, MidpointRounding.AwayFromZero);
    }

    // MARK: - Preferences.ini (byte-preserving)

    static class PrefsINI
    {
        public const string Key = "GlobalOffsetSeconds";
        // Latin-1 maps every byte to one char and back, so untouched bytes round-trip exactly.
        static readonly Encoding Bytes = Encoding.GetEncoding(28591);

        struct Line { public int Start, End; public string Text; }  // End excludes "\r\n" / "\n"

        static List<Line> Lines(string text)
        {
            var list = new List<Line>();
            int pos = 0;
            while (true)
            {
                int nl = text.IndexOf('\n', pos);
                int end = nl < 0 ? text.Length : nl;
                int contentEnd = end > pos && text[end - 1] == '\r' ? end - 1 : end;
                list.Add(new Line { Start = pos, End = contentEnd, Text = text.Substring(pos, contentEnd - pos) });
                if (nl < 0) break;
                pos = nl + 1;
            }
            return list;
        }

        static bool IsSection(string t) => t.StartsWith("[") && t.EndsWith("]");

        public static double? ReadOffset(string path)
        {
            string text;
            try { text = Bytes.GetString(File.ReadAllBytes(path)); } catch { return null; }
            string section = "";
            foreach (var l in Lines(text))
            {
                string t = l.Text.Trim();
                if (IsSection(t)) { section = t; continue; }
                if (section == "[Options]" && t.StartsWith(Key + "="))
                {
                    double v;
                    return double.TryParse(t.Substring(Key.Length + 1).Trim(), NumberStyles.Float,
                                           CultureInfo.InvariantCulture, out v) ? v : (double?)null;
                }
            }
            return null;
        }

        /// Replace (or add) GlobalOffsetSeconds in [Options]. Only that line changes; every
        /// other byte, including CRLF line endings, stays exactly as it was.
        public static void WriteOffset(double value, string path)
        {
            if (Math.Abs(value) < 0.0000005) value = 0;
            string entry = Key + "=" + value.ToString("F6", CultureInfo.InvariantCulture);
            string text = "";
            if (File.Exists(path)) text = Bytes.GetString(File.ReadAllBytes(path));
            string eol = text.Contains("\r\n") || text.Length == 0 ? "\r\n" : "\n";
            string result = null;

            if (text.Length > 0)
            {
                string section = "";
                Line? options = null;
                foreach (var l in Lines(text))
                {
                    string t = l.Text.Trim();
                    if (IsSection(t)) { section = t; if (t == "[Options]") options = l; continue; }
                    if (section == "[Options]" && t.StartsWith(Key + "="))
                    {
                        result = text.Substring(0, l.Start) + entry + text.Substring(l.End);
                        break;
                    }
                }
                if (result == null && options.HasValue)
                {
                    int after = text.IndexOf('\n', options.Value.End);
                    result = after < 0 ? text + eol + entry
                                       : text.Substring(0, after + 1) + entry + eol + text.Substring(after + 1);
                }
                if (result == null) result = "[Options]" + eol + entry + eol + eol + text;
                if (!result.EndsWith("\n")) result += eol;
            }
            else result = "[Options]" + eol + entry + eol;

            AtomicWrite(path, Bytes.GetBytes(result));
        }

        /// Value of key in [section], or null.
        public static string ReadValue(string path, string section, string key)
        {
            string text;
            try { text = Bytes.GetString(File.ReadAllBytes(path)); } catch { return null; }
            string cur = "";
            foreach (var l in Lines(text))
            {
                string t = l.Text.Trim();
                if (IsSection(t)) { cur = t; continue; }
                if (cur == section && t.StartsWith(key + "=")) return t.Substring(key.Length + 1).Trim();
            }
            return null;
        }

        /// Set key=value in every section that `inSection` accepts. With add, a missing key is
        /// added right after the section header. Only the touched lines change.
        public static string SetValue(string text, Func<string, bool> inSection, string key, string value, bool add)
        {
            string eol = text.Contains("\r\n") ? "\r\n" : "\n";
            string entry = key + "=" + value;
            string cur = "";
            Line? header = null;
            bool found = false;
            // Walk backwards through edits by collecting them first.
            var edits = new List<Tuple<int, int, string>>();   // start, end, replacement
            Action closeSection = () =>
            {
                if (header.HasValue && !found && add)
                {
                    int after = text.IndexOf('\n', header.Value.End);
                    int at = after < 0 ? text.Length : after + 1;
                    edits.Add(Tuple.Create(at, at, (after < 0 ? eol : "") + entry + eol));
                }
            };
            foreach (var l in Lines(text))
            {
                string t = l.Text.Trim();
                if (IsSection(t))
                {
                    closeSection();
                    cur = t; found = false;
                    header = inSection(t) ? l : (Line?)null;
                    continue;
                }
                if (header.HasValue && t.StartsWith(key + "="))
                {
                    found = true;
                    edits.Add(Tuple.Create(l.Start, l.End, entry));
                }
            }
            closeSection();
            foreach (var e in edits.OrderByDescending(e => e.Item1))
                text = text.Substring(0, e.Item1) + e.Item3 + text.Substring(e.Item2);
            return text;
        }

        /// Write a display mode: resolution in [Options], theme in [Options] and in every
        /// per-game section whose Theme is one of the Rave It Out themes (they override [Options]).
        public static void WriteDisplay(DisplayMode m, string path)
        {
            string text = Bytes.GetString(File.ReadAllBytes(path));
            Func<string, bool> options = s => s == "[Options]";
            text = SetValue(text, options, "DisplayWidth", m.Width.ToString(CultureInfo.InvariantCulture), true);
            text = SetValue(text, options, "DisplayHeight", m.Height.ToString(CultureInfo.InvariantCulture), true);
            text = SetValue(text, options, "DisplayAspectRatio", m.Aspect, true);
            text = SetValue(text, options, "Theme", m.Theme, true);
            foreach (var game in GameSectionsWithRioTheme(text))
                text = SetValue(text, s => s == game, "Theme", m.Theme, false);
            AtomicWrite(path, Bytes.GetBytes(text));
        }

        static List<string> GameSectionsWithRioTheme(string text)
        {
            var list = new List<string>();
            string cur = "";
            foreach (var l in Lines(text))
            {
                string t = l.Text.Trim();
                if (IsSection(t)) { cur = t; continue; }
                if (cur.StartsWith("[Game-") && t.StartsWith("Theme="))
                {
                    string v = t.Substring(6).Trim();
                    if (DisplayMode.All.Any(m => m.Theme == v)) list.Add(cur);
                }
            }
            return list;
        }

        public static void AtomicWrite(string path, byte[] data)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            string tmp = path + ".audiosync-tmp";
            File.WriteAllBytes(tmp, data);
            if (File.Exists(path)) File.Replace(tmp, path, null);
            else File.Move(tmp, path);
        }
    }

    // MARK: - Display modes (SD theme for 4:3 CRT cabinets, HD theme for 16:9 screens)

    class DisplayMode
    {
        public string Name, Aspect, Theme;
        public int Width, Height;

        public static readonly DisplayMode[] All =
        {
            new DisplayMode { Name = "SD 480p — 4:3 CRT (640×480)", Width = 640, Height = 480, Aspect = "1.333333", Theme = "RaveItOut-SD-mipmaps" },
            new DisplayMode { Name = "HD 720p (1280×720)", Width = 1280, Height = 720, Aspect = "1.777778", Theme = "RaveItOut" },
            new DisplayMode { Name = "Full HD 1080p (1920×1080)", Width = 1920, Height = 1080, Aspect = "1.777778", Theme = "RaveItOut" },
        };

        /// The mode Preferences.ini is set to now, or null when it matches none of them.
        public static DisplayMode Read(string path)
        {
            int w, h;
            if (!int.TryParse(PrefsINI.ReadValue(path, "[Options]", "DisplayWidth"), out w)) return null;
            if (!int.TryParse(PrefsINI.ReadValue(path, "[Options]", "DisplayHeight"), out h)) return null;
            string theme = PrefsINI.ReadValue(path, "[Options]", "Theme");
            string pump = PrefsINI.ReadValue(path, "[Game-pump]", "Theme");
            if (!string.IsNullOrEmpty(pump)) theme = pump;
            return All.FirstOrDefault(m => m.Width == w && m.Height == h && m.Theme == theme);
        }
    }

    // MARK: - AudioSyncProfiles.json (same layout as Swift's JSONEncoder, prettyPrinted + sortedKeys)

    static class ProfileJSON
    {
        public static List<AudioProfile> Load(string path)
        {
            try
            {
                var root = new Parser(File.ReadAllText(path, Encoding.UTF8)).Parse() as Dictionary<string, object>;
                var arr = root?["profiles"] as List<object>;
                if (arr == null) return null;
                var list = new List<AudioProfile>();
                foreach (var o in arr)
                {
                    var d = (Dictionary<string, object>)o;
                    object dev;
                    list.Add(new AudioProfile
                    {
                        Id = (string)d["id"],
                        Name = (string)d["name"],
                        Offset = (double)d["offset"],
                        Device = d.TryGetValue("device", out dev) ? dev as string : null,
                    });
                }
                return list;
            }
            catch { return null; }
        }

        public static string Encode(List<AudioProfile> profiles)
        {
            var sb = new StringBuilder("{\n  \"profiles\" : [\n");
            for (int i = 0; i < profiles.Count; i++)
            {
                var p = profiles[i];
                sb.Append("    {\n");
                if (p.Device != null) sb.Append("      \"device\" : ").Append(Str(p.Device)).Append(",\n");
                sb.Append("      \"id\" : ").Append(Str(p.Id)).Append(",\n");
                sb.Append("      \"name\" : ").Append(Str(p.Name)).Append(",\n");
                sb.Append("      \"offset\" : ").Append(Num(p.Offset)).Append("\n");
                sb.Append(i + 1 < profiles.Count ? "    },\n" : "    }\n");
            }
            sb.Append("  ],\n  \"version\" : 1\n}");
            return sb.ToString();
        }

        public static void Save(string path, List<AudioProfile> profiles) =>
            PrefsINI.AtomicWrite(path, new UTF8Encoding(false).GetBytes(Encode(profiles)));

        static string Num(double v)
        {
            if (v == 0) return "0";
            return v.ToString("R", CultureInfo.InvariantCulture).Replace("E", "e");
        }

        static string Str(string s)
        {
            var sb = new StringBuilder("\"");
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '/': sb.Append("\\/"); break;    // Swift escapes slashes by default
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    default:
                        if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            return sb.Append('"').ToString();
        }

        class Parser
        {
            readonly string s; int i;
            public Parser(string s) { this.s = s; }
            public object Parse() { var v = Value(); Ws(); if (i != s.Length) throw new FormatException(); return v; }
            void Ws() { while (i < s.Length && char.IsWhiteSpace(s[i])) i++; }
            char Peek() { Ws(); if (i >= s.Length) throw new FormatException(); return s[i]; }
            void Expect(char c) { if (Peek() != c) throw new FormatException(); i++; }
            object Value()
            {
                char c = Peek();
                if (c == '{')
                {
                    i++; var d = new Dictionary<string, object>();
                    if (Peek() == '}') { i++; return d; }
                    while (true)
                    {
                        string k = Str(); Expect(':'); d[k] = Value();
                        if (Peek() == ',') { i++; continue; }
                        Expect('}'); return d;
                    }
                }
                if (c == '[')
                {
                    i++; var a = new List<object>();
                    if (Peek() == ']') { i++; return a; }
                    while (true)
                    {
                        a.Add(Value());
                        if (Peek() == ',') { i++; continue; }
                        Expect(']'); return a;
                    }
                }
                if (c == '"') return Str();
                if (s.Substring(i).StartsWith("true")) { i += 4; return true; }
                if (s.Substring(i).StartsWith("false")) { i += 5; return false; }
                if (s.Substring(i).StartsWith("null")) { i += 4; return null; }
                int st = i;
                while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
                return double.Parse(s.Substring(st, i - st), NumberStyles.Float, CultureInfo.InvariantCulture);
            }
            string Str()
            {
                Expect('"'); var sb = new StringBuilder();
                while (true)
                {
                    char c = s[i++];
                    if (c == '"') return sb.ToString();
                    if (c != '\\') { sb.Append(c); continue; }
                    char e = s[i++];
                    switch (e)
                    {
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'u': sb.Append((char)Convert.ToInt32(s.Substring(i, 4), 16)); i += 4; break;
                        default: sb.Append(e); break;
                    }
                }
            }
        }
    }

    // MARK: - Current Windows output device (Core Audio, built into Windows)

    static class OutputDevice
    {
        [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator { }

        [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IMMDeviceEnumerator
        {
            [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
            [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
        }

        [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IMMDevice
        {
            [PreserveSig] int Activate(ref Guid iid, int ctx, IntPtr p, out IntPtr iface);
            [PreserveSig] int OpenPropertyStore(int access, out IPropertyStore store);
        }

        [ComImport, Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IPropertyStore
        {
            [PreserveSig] int GetCount(out int count);
            [PreserveSig] int GetAt(int index, out PropertyKey key);
            [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant value);
        }

        [StructLayout(LayoutKind.Sequential)] struct PropertyKey { public Guid fmtid; public int pid; }
        [StructLayout(LayoutKind.Sequential)]
        struct PropVariant { public ushort vt, r1, r2, r3; public IntPtr p1, p2; }
        [DllImport("ole32.dll")] static extern int PropVariantClear(ref PropVariant pv);

        static readonly Guid DeviceProps = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0");   // PKEY_Device_FriendlyName (14)
        static readonly Guid EndpointProps = new Guid("1da5d803-d492-4edd-8c23-e0c0ffee7f0e"); // PKEY_AudioEndpoint_FormFactor (0)
        static IMMDeviceEnumerator enumerator;

        /// Name plus a Segoe MDL2 glyph for the kind of device.
        public static void Current(out string name, out string glyph)
        {
            name = "No output device"; glyph = "";
            try
            {
                if (enumerator == null) enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
                IMMDevice dev;
                if (enumerator.GetDefaultAudioEndpoint(0 /*eRender*/, 0 /*eConsole*/, out dev) != 0 || dev == null) return;
                IPropertyStore store;
                if (dev.OpenPropertyStore(0, out store) != 0) { name = "Unknown device"; glyph = ""; return; }
                var key = new PropertyKey { fmtid = DeviceProps, pid = 14 };
                PropVariant v;
                name = "Unknown device";
                if (store.GetValue(ref key, out v) == 0)
                {
                    if (v.vt == 31 /*VT_LPWSTR*/) name = Marshal.PtrToStringUni(v.p1);
                    PropVariantClear(ref v);
                }
                key = new PropertyKey { fmtid = EndpointProps, pid = 0 };
                int form = -1;
                if (store.GetValue(ref key, out v) == 0)
                {
                    if (v.vt == 19 /*VT_UI4*/) form = (int)(v.p1.ToInt64() & 0xffffffff);
                    PropVariantClear(ref v);
                }
                switch (form)
                {
                    case 3: case 5: glyph = ""; break;   // headphones, headset
                    case 9: glyph = ""; break;           // HDMI / DisplayPort display
                    default: glyph = ""; break;          // speakers etc.
                }
                Marshal.ReleaseComObject(store);
                Marshal.ReleaseComObject(dev);
            }
            catch { }
        }
    }

    // MARK: - Small settings file (the Mac uses UserDefaults)

    static class Settings
    {
        static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                                                  "Rave It Out Audio Sync");
        static readonly string FilePath = Path.Combine(Dir, "settings.ini");
        static Dictionary<string, string> values;

        public static string AppDataDir => Dir;

        static Dictionary<string, string> Values()
        {
            if (values != null) return values;
            values = new Dictionary<string, string>();
            try
            {
                foreach (var l in File.ReadAllLines(FilePath))
                {
                    int eq = l.IndexOf('=');
                    if (eq > 0) values[l.Substring(0, eq)] = l.Substring(eq + 1);
                }
            }
            catch { }
            return values;
        }

        public static string Get(string k) { string v; return Values().TryGetValue(k, out v) ? v : null; }
        public static bool GetBool(string k, bool def) { var v = Get(k); return v == null ? def : v == "1"; }
        public static void Set(string k, string v)
        {
            Values()[k] = v;
            try
            {
                Directory.CreateDirectory(Dir);
                File.WriteAllLines(FilePath, values.Select(p => p.Key + "=" + p.Value));
            }
            catch { }
        }
        public static void SetBool(string k, bool v) => Set(k, v ? "1" : "0");
    }

    // MARK: - Model

    class AppModel
    {
        public List<AudioProfile> Profiles = new List<AudioProfile>();
        public AudioProfile Selected;
        public string GameFolder;
        public double? GameOffset;
        public bool GameRunning;
        public string DeviceName = "", DeviceGlyph = "";
        public string Status = "";
        public bool AutoApply, CloseAfterLaunch;
        public DisplayMode GameDisplay;   // what Preferences.ini is set to (null = none of ours)
        public DisplayMode Display;       // what Apply will write (null = leave as is)
        public event Action Changed = delegate { };

        public AppModel()
        {
            AutoApply = Settings.GetBool("autoApply", false);
            CloseAfterLaunch = Settings.GetBool("closeAfterLaunch", true);
            GameFolder = LocateGameFolder();
            RefreshGameOffset();
            Display = GameDisplay;
            LoadProfiles();
            RefreshDevice(true);
            RefreshRunning();
        }

        public void Notify() => Changed();

        // Paths
        public string PrefsPath => GameFolder == null ? null : Path.Combine(GameFolder, "Save", "Preferences.ini");
        public string ProfilesPath => GameFolder != null
            ? Path.Combine(GameFolder, "Save", "AudioSyncProfiles.json")
            : Path.Combine(Settings.AppDataDir, "AudioSyncProfiles.json");
        public string GameExe
        {
            get
            {
                if (GameFolder == null) return null;
                string prog = Path.Combine(GameFolder, "Program");
                string exe = Path.Combine(prog, "StepMania.exe");
                if (File.Exists(exe)) return exe;
                try { return Directory.GetFiles(prog, "StepMania*.exe").FirstOrDefault(); } catch { return null; }
            }
        }

        public static bool IsGameFolder(string dir) =>
            File.Exists(Path.Combine(dir, "Save", "Preferences.ini"))
            || File.Exists(Path.Combine(dir, "portable.ini"))
            || File.Exists(Path.Combine(dir, "Program", "StepMania.exe"));

        /// Saved choice first, then the folder this exe sits in (and a few levels up).
        static string LocateGameFolder()
        {
            var saved = Settings.Get("gameFolder");
            if (saved != null && Directory.Exists(saved) && IsGameFolder(saved)) return saved;
            var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
            for (int n = 0; n < 4 && dir != null; n++, dir = dir.Parent)
                if (IsGameFolder(dir.FullName)) return dir.FullName.TrimEnd('\\');
            return null;
        }

        public void SetGameFolder(string dir)
        {
            if (!IsGameFolder(dir)) { Status = "That folder doesn't look like a Rave It Out install."; Notify(); return; }
            Settings.Set("gameFolder", dir);
            GameFolder = dir;
            RefreshGameOffset();
            LoadProfiles();
            Status = "Using " + dir;
            Notify();
        }

        // Profiles
        public void LoadProfiles()
        {
            var list = File.Exists(ProfilesPath) ? ProfileJSON.Load(ProfilesPath) : null;
            Profiles = list != null && list.Count > 0 ? list
                     : new List<AudioProfile> { new AudioProfile { Name = "Speakers", Offset = 0 } };
            SaveProfiles();
            Selected = Profiles.FirstOrDefault(p => GameOffset.HasValue && Fmt.Same(GameOffset.Value, p.Offset))
                       ?? Profiles.FirstOrDefault();
        }

        public void SaveProfiles()
        {
            try { ProfileJSON.Save(ProfilesPath, Profiles); } catch { }
        }

        public void AddProfile(double offset = 0, string name = null)
        {
            var p = new AudioProfile { Name = name ?? UniqueName("New Device"), Offset = offset };
            Profiles.Add(p);
            Selected = p;
            SaveProfiles();
            Notify();
        }

        public void DeleteSelected()
        {
            int i = Profiles.IndexOf(Selected);
            if (i < 0 || Profiles.Count <= 1) return;
            Profiles.RemoveAt(i);
            Selected = Profiles[Math.Max(0, i - 1)];
            SaveProfiles();
            Notify();
        }

        public string UniqueName(string b)
        {
            string name = b; int n = 2;
            while (Profiles.Any(p => p.Name == name)) name = b + " " + n++;
            return name;
        }

        public AudioProfile ActiveProfile =>
            GameOffset.HasValue ? Profiles.FirstOrDefault(p => Fmt.Same(p.Offset, GameOffset.Value)) : null;

        // Game state
        public void RefreshGameOffset()
        {
            GameOffset = PrefsPath == null ? null : PrefsINI.ReadOffset(PrefsPath);
            GameDisplay = PrefsPath == null ? null : DisplayMode.Read(PrefsPath);
        }

        public bool HasTheme(string theme) =>
            GameFolder != null && Directory.Exists(Path.Combine(GameFolder, "Themes", theme));

        /// True when the running state changed.
        public bool RefreshRunning()
        {
            bool running = false;
            try
            {
                foreach (var p in Process.GetProcessesByName("StepMania")) { running = true; p.Dispose(); }
            }
            catch { }
            bool changed = running != GameRunning;
            GameRunning = running;
            return changed;
        }

        public void RefreshDevice(bool initial)
        {
            string name, glyph;
            OutputDevice.Current(out name, out glyph);
            bool changed = name != DeviceName;
            DeviceName = name;
            DeviceGlyph = glyph;
            if (!changed) return;
            var linked = Profiles.FirstOrDefault(p => p.Device == name);
            if (linked == null) { if (!initial) Notify(); return; }
            Selected = linked;
            if (AutoApply && !initial) Apply();
            else if (!initial) Status = name + " connected — selected “" + linked.Name + "”.";
            if (!initial) Notify();
        }

        public bool Apply()
        {
            var p = Selected;
            if (p == null) return false;
            if (PrefsPath == null) { Status = "Game folder not found — choose it below."; Notify(); return false; }
            RefreshRunning();
            if (GameRunning)
            {
                Status = "Quit Rave It Out first — it saves its own settings on exit and would undo this.";
                Notify();
                return false;
            }
            try
            {
                var d = Display;
                if (d != null && !HasTheme(d.Theme))
                {
                    Status = "Theme “" + d.Theme + "” is missing — run Utils\\RaveItOut-SD\\make_sd_theme.py (see its README).";
                    Notify();
                    return false;
                }
                PrefsINI.WriteOffset(p.Offset, PrefsPath);
                if (d != null && d != GameDisplay) PrefsINI.WriteDisplay(d, PrefsPath);
                RefreshGameOffset();
                Status = "Global offset set to " + Fmt.Offset(p.Offset) + " s (" + p.Name + ")"
                       + (d != null ? "; display " + d.Name + "." : ".");
                Notify();
                return true;
            }
            catch (Exception e)
            {
                Status = "Couldn't write Preferences.ini: " + e.Message;
                Notify();
                return false;
            }
        }

        /// True when the app should close.
        public bool ApplyAndLaunch()
        {
            if (!Apply()) return false;
            var exe = GameExe;
            if (exe == null) { Status = "Couldn't find the game in " + (GameFolder ?? "?") + "."; Notify(); return false; }
            try
            {
                // --theme as well as the prefs: the engine applies it before any per-game Theme override.
                string args = Display != null ? "--theme=" + Display.Theme : "";
                Process.Start(new ProcessStartInfo(exe, args) { WorkingDirectory = Path.GetDirectoryName(exe), UseShellExecute = true });
                RefreshRunning();
                Notify();
                return CloseAfterLaunch;
            }
            catch (Exception e) { Status = "Couldn't launch: " + e.Message; Notify(); return false; }
        }
    }

#if !TESTING
    // MARK: - Look (Miami Nights neon)

    static class Rio
    {
        public static readonly Color BG = Color.FromArgb(13, 5, 33);
        public static readonly Color Panel = Color.FromArgb(28, 13, 61);
        public static readonly Color Field = Color.FromArgb(20, 9, 45);
        public static readonly Color Pink = Color.FromArgb(255, 46, 166);
        public static readonly Color Purple = Color.FromArgb(148, 64, 242);
        public static readonly Color Cyan = Color.FromArgb(46, 217, 242);
        public static readonly Color Dim = Color.FromArgb(160, 150, 185);
        public static readonly Color Faint = Color.FromArgb(130, 120, 160);

        public static GraphicsPath Round(RectangleF r, float rad)
        {
            var p = new GraphicsPath();
            float d = rad * 2;
            p.AddArc(r.X, r.Y, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }

        public static Font UI(float size, FontStyle st = FontStyle.Regular) => new Font("Segoe UI", size, st);
        public static Label Caption(string text) => new Label
        {
            Text = text, AutoSize = true, ForeColor = Dim, Font = UI(8f, FontStyle.Bold), BackColor = Color.Transparent,
        };
    }

    class RoundPanel : Panel
    {
        public RoundPanel() { DoubleBuffered = true; BackColor = Rio.Panel; Padding = new Padding(14); }
        protected override void OnPaintBackground(PaintEventArgs e)
        {
            e.Graphics.Clear(Rio.BG);
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using (var path = Rio.Round(new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), 12))
            using (var b = new SolidBrush(Rio.Panel))
            using (var pen = new Pen(Color.FromArgb(115, Rio.Purple)))
            {
                e.Graphics.FillPath(b, path);
                e.Graphics.DrawPath(pen, path);
            }
        }
    }

    class NeonButton : Button
    {
        public bool Primary;
        bool hover, down;
        public NeonButton(string text, bool primary = false)
        {
            Text = text; Primary = primary;
            FlatStyle = FlatStyle.Flat; FlatAppearance.BorderSize = 0;
            Font = Rio.UI(9.75f, FontStyle.Bold); ForeColor = Color.White;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.SupportsTransparentBackColor, true);
        }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { down = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { down = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(Parent?.BackColor ?? Rio.BG);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            int a = Enabled ? (down ? 170 : 255) : 90;
            using (var path = Rio.Round(r, 7))
            {
                Brush fill = Primary
                    ? (Brush)new LinearGradientBrush(r, Color.FromArgb(a, Rio.Pink), Color.FromArgb(a, Rio.Purple), 0f)
                    : new SolidBrush(Color.FromArgb(hover && Enabled ? 38 : 20, 255, 255, 255));
                using (fill) g.FillPath(fill, path);
                using (var pen = new Pen(Primary ? Color.FromArgb(a, Rio.Pink) : Color.FromArgb(Enabled ? 160 : 70, Rio.Purple)))
                    g.DrawPath(pen, path);
            }
            if (Focused && ShowFocusCues)
                using (var pen = new Pen(Rio.Cyan) { DashStyle = DashStyle.Dot })
                    g.DrawRectangle(pen, 3, 3, Width - 7, Height - 7);
            TextRenderer.DrawText(g, Text, Font, ClientRectangle, Enabled ? Color.White : Color.FromArgb(120, 120, 140),
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }

    class HeaderTitle : Control
    {
        public HeaderTitle() { DoubleBuffered = true; BackColor = Rio.BG; }
        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            using (var big = new Font("Segoe UI Black", 22f, FontStyle.Italic))
            using (var small = Rio.UI(8f, FontStyle.Bold))
            {
                var size = g.MeasureString("RAVE IT OUT", big);
                var rect = new RectangleF(0, 0, Math.Max(1, size.Width), Math.Max(1, size.Height));
                using (var glow = new SolidBrush(Color.FromArgb(50, Rio.Pink)))
                    for (int d = 1; d <= 2; d++) g.DrawString("RAVE IT OUT", big, glow, -d * 0.5f + d, d);
                using (var grad = new LinearGradientBrush(rect, Rio.Pink, Rio.Purple, 0f))
                    g.DrawString("RAVE IT OUT", big, grad, 0, 0);
                using (var cyan = new SolidBrush(Rio.Cyan))
                    g.DrawString("A U D I O   S Y N C   ·   M I A M I   N I G H T S", small, cyan, 4, size.Height - 6);
            }
        }
    }

    class ProfileList : ListBox
    {
        public AppModel Model;
        public ProfileList()
        {
            DrawMode = DrawMode.OwnerDrawFixed; ItemHeight = 44; BorderStyle = BorderStyle.None;
            BackColor = Rio.Panel; ForeColor = Color.White; IntegralHeight = false;
            SetStyle(ControlStyles.OptimizedDoubleBuffer, true);
        }
        protected override void OnDrawItem(DrawItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= Items.Count) return;
            var p = (AudioProfile)Items[e.Index];
            var g = e.Graphics;
            using (var bg = new SolidBrush(Rio.Panel)) g.FillRectangle(bg, e.Bounds);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            var r = new RectangleF(e.Bounds.X + 1, e.Bounds.Y + 2, e.Bounds.Width - 3, e.Bounds.Height - 4);
            bool selected = p == Model.Selected;
            bool active = Model.GameOffset.HasValue && Fmt.Same(Model.GameOffset.Value, p.Offset);
            if (selected)
                using (var path = Rio.Round(r, 8))
                using (var fill = new LinearGradientBrush(r, Color.FromArgb(90, Rio.Pink), Color.FromArgb(90, Rio.Purple), 0f))
                using (var pen = new Pen(Rio.Pink))
                {
                    g.FillPath(fill, path);
                    g.DrawPath(pen, path);
                }
            float cy = r.Y + r.Height / 2, cx = r.X + 16;
            if (active)
            {
                using (var b = new SolidBrush(Rio.Cyan)) g.FillEllipse(b, cx - 8, cy - 8, 16, 16);
                using (var pen = new Pen(Rio.BG, 2f))
                    g.DrawLines(pen, new[] { new PointF(cx - 4, cy), new PointF(cx - 1, cy + 3), new PointF(cx + 4, cy - 3) });
            }
            else using (var pen = new Pen(Color.FromArgb(80, 255, 255, 255), 1.5f)) g.DrawEllipse(pen, cx - 7, cy - 7, 14, 14);
            using (var nameFont = Rio.UI(9.75f, FontStyle.Bold))
            using (var subFont = Rio.UI(8.25f))
            {
                var nameRect = new Rectangle((int)r.X + 32, (int)r.Y + 4, (int)r.Width - 36, 20);
                TextRenderer.DrawText(g, p.Name, nameFont, nameRect, Color.White,
                    TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
                string sub = Fmt.Offset(p.Offset) + " s" + (p.Device != null ? "   · linked" : "");
                TextRenderer.DrawText(g, sub, subFont, new Rectangle(nameRect.X, nameRect.Bottom, nameRect.Width, 16),
                    Rio.Dim, TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix);
            }
        }
    }

    // MARK: - Window

    class MainForm : Form
    {
        readonly AppModel model;
        bool updating;

        readonly Label deviceLabel = new Label(), deviceGlyph = new Label(), gameOffsetLabel = new Label();
        readonly ProfileList list = new ProfileList();
        readonly NeonButton addBtn = new NeonButton("+"), delBtn = new NeonButton("−"),
                            saveGameBtn = new NeonButton("Save game value");
        readonly RoundPanel editor = new RoundPanel();
        readonly TextBox nameBox = new TextBox(), offsetBox = new TextBox();
        readonly Label msLabel = new Label(), linkedLabel = new Label(), noSelection = new Label();
        readonly NeonButton resetBtn = new NeonButton("Reset to 0"), useGameBtn = new NeonButton("Use game's value"),
                            linkBtn = new NeonButton("Link"), unlinkBtn = new NeonButton("Unlink");
        readonly CheckBox autoApplyBox = new CheckBox(), closeBox = new CheckBox();
        readonly ComboBox displayBox = new ComboBox();
        readonly Label runningLabel = new Label(), statusLabel = new Label(), folderLabel = new Label();
        readonly NeonButton applyBtn = new NeonButton("Apply"), playBtn = new NeonButton("Apply && Play", true),
                            changeBtn = new NeonButton("Change…");
        readonly Timer timer = new Timer { Interval = 1500 };
        readonly ToolTip tips = new ToolTip();

        public MainForm(AppModel model)
        {
            this.model = model;
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96f, 96f);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "Rave It Out Audio Sync";
            ClientSize = new Size(760, 604);
            MinimumSize = new Size(740, 604);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Rio.BG; ForeColor = Color.White; Font = Rio.UI(9f);
            KeyPreview = true;
            DoubleBuffered = true;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            Build();
            ResumeLayout(false);

            model.Changed += Sync;
            timer.Tick += (s, e) => Poll();
            timer.Start();
            Activated += (s, e) => { model.RefreshGameOffset(); Sync(); };
            KeyDown += OnKey;
            Sync();
        }

        void Build()
        {
            int W = ClientSize.Width, H = ClientSize.Height;
            var AT = AnchorStyles.Top; var AB = AnchorStyles.Bottom; var AL = AnchorStyles.Left; var AR = AnchorStyles.Right;

            // Header
            var title = new HeaderTitle { Bounds = new Rectangle(18, 12, 380, 64) };
            deviceGlyph.Font = new Font("Segoe MDL2 Assets", 12f);
            deviceGlyph.Bounds = new Rectangle(W - 360, 26, 24, 22); deviceGlyph.Anchor = AT | AR; deviceGlyph.ForeColor = Rio.Cyan;
            deviceLabel.Font = Rio.UI(9.75f, FontStyle.Bold); deviceLabel.Bounds = new Rectangle(W - 334, 26, 316, 22);
            deviceLabel.TextAlign = ContentAlignment.MiddleRight; deviceLabel.Anchor = AT | AR; deviceLabel.AutoEllipsis = true;
            gameOffsetLabel.Font = Rio.UI(9f); gameOffsetLabel.ForeColor = Rio.Dim;
            gameOffsetLabel.Bounds = new Rectangle(W - 400, 50, 382, 20); gameOffsetLabel.TextAlign = ContentAlignment.MiddleRight;
            gameOffsetLabel.Anchor = AT | AR; gameOffsetLabel.AutoEllipsis = true;
            Controls.AddRange(new Control[] { title, deviceGlyph, deviceLabel, gameOffsetLabel });

            // Profiles panel
            int top = 86, bottom = H - 164;
            var left = new RoundPanel { Bounds = new Rectangle(18, top, 240, bottom - top), Anchor = AT | AB | AL };
            var cap = Rio.Caption("DEVICE PROFILES"); cap.Location = new Point(14, 12);
            list.Model = model;
            list.Bounds = new Rectangle(10, 34, 220, left.Height - 34 - 50); list.Anchor = AT | AB | AL | AR;
            list.SelectedIndexChanged += (s, e) =>
            {
                if (updating || list.SelectedIndex < 0) return;
                model.Selected = (AudioProfile)list.Items[list.SelectedIndex];
                Sync();
            };
            int by = left.Height - 44;
            addBtn.Bounds = new Rectangle(12, by, 32, 30); addBtn.Anchor = AB | AL;
            delBtn.Bounds = new Rectangle(48, by, 32, 30); delBtn.Anchor = AB | AL;
            saveGameBtn.Bounds = new Rectangle(86, by, 142, 30); saveGameBtn.Anchor = AB | AL; saveGameBtn.Font = Rio.UI(8.25f, FontStyle.Bold);
            addBtn.Click += (s, e) => model.AddProfile();
            delBtn.Click += (s, e) => model.DeleteSelected();
            saveGameBtn.Click += (s, e) => { if (model.GameOffset.HasValue) model.AddProfile(model.GameOffset.Value, model.UniqueName("From Game")); };
            tips.SetToolTip(addBtn, "Add a device profile");
            tips.SetToolTip(delBtn, "Delete the selected profile");
            tips.SetToolTip(saveGameBtn, "Make a profile from the offset currently in the game's settings");
            left.Controls.AddRange(new Control[] { cap, list, addBtn, delBtn, saveGameBtn });
            Controls.Add(left);

            // Editor panel
            editor.Bounds = new Rectangle(270, top, W - 270 - 18, bottom - top); editor.Anchor = AT | AB | AL | AR;
            int ew = editor.Width;
            var nameCap = Rio.Caption("NAME"); nameCap.Location = new Point(16, 14);
            StyleBox(nameBox, 10f); nameBox.Bounds = new Rectangle(16, 34, ew - 32, 26); nameBox.Anchor = AT | AL | AR;
            nameBox.TextChanged += (s, e) =>
            {
                if (updating || model.Selected == null) return;
                model.Selected.Name = nameBox.Text;
                model.SaveProfiles();
                list.Invalidate();
                SyncHeader();
            };

            var offCap = Rio.Caption("GLOBAL OFFSET"); offCap.Location = new Point(16, 74);
            int x = 16, y = 94;
            foreach (var st in new[] { Tuple.Create(-0.010, "−10"), Tuple.Create(-0.001, "−1") })
            { var b = Step(st.Item1, st.Item2); b.Location = new Point(x, y + 2); editor.Controls.Add(b); x += 52; }
            StyleBox(offsetBox, 15f, FontStyle.Bold); offsetBox.TextAlign = HorizontalAlignment.Center;
            offsetBox.Bounds = new Rectangle(x, y, 140, 36);
            offsetBox.Leave += (s, e) => CommitOffset();
            offsetBox.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter && !e.Control) { CommitOffset(); offsetBox.SelectAll(); e.SuppressKeyPress = true; } };
            x += 146;
            var sLabel = new Label { Text = "s", AutoSize = true, ForeColor = Rio.Dim, Location = new Point(x, y + 10), Font = Rio.UI(10f) };
            x += 20;
            editor.Controls.AddRange(new Control[] { offsetBox, sLabel });
            foreach (var st in new[] { Tuple.Create(0.001, "+1"), Tuple.Create(0.010, "+10") })
            { var b = Step(st.Item1, st.Item2); b.Location = new Point(x, y + 2); editor.Controls.Add(b); x += 52; }

            msLabel.Bounds = new Rectangle(16, 140, 100, 22); msLabel.ForeColor = Rio.Dim; msLabel.TextAlign = ContentAlignment.MiddleLeft;
            resetBtn.Bounds = new Rectangle(120, 136, 92, 28); resetBtn.Font = Rio.UI(8.25f, FontStyle.Bold);
            useGameBtn.Bounds = new Rectangle(218, 136, 210, 28); useGameBtn.Font = Rio.UI(8.25f, FontStyle.Bold);
            resetBtn.Click += (s, e) => SetOffset(0);
            useGameBtn.Click += (s, e) => { if (model.GameOffset.HasValue) SetOffset(model.GameOffset.Value); };
            var offHelp = Hint("Same as Options → Global Offset in the game. Bluetooth headphones usually need a negative value (around −0.150 to −0.250). Buttons step in milliseconds.",
                               new Rectangle(16, 170, ew - 32, 34));

            var linkCap = Rio.Caption("LINKED DEVICE"); linkCap.Location = new Point(16, 218);
            linkedLabel.Bounds = new Rectangle(16, 240, ew - 120, 28); linkedLabel.Anchor = AT | AL | AR;
            linkedLabel.TextAlign = ContentAlignment.MiddleLeft; linkedLabel.AutoEllipsis = true; linkedLabel.Font = Rio.UI(9.5f, FontStyle.Bold);
            unlinkBtn.Bounds = new Rectangle(ew - 96, 240, 80, 28); unlinkBtn.Anchor = AT | AR; unlinkBtn.Font = Rio.UI(8.25f, FontStyle.Bold);
            linkBtn.Bounds = new Rectangle(16, 240, ew - 32, 28); linkBtn.Anchor = AT | AL | AR; linkBtn.Font = Rio.UI(8.25f, FontStyle.Bold);
            unlinkBtn.Click += (s, e) => { if (model.Selected != null) { model.Selected.Device = null; model.SaveProfiles(); Sync(); } };
            linkBtn.Click += (s, e) =>
            {
                if (model.Selected == null) return;
                foreach (var p in model.Profiles) if (p.Device == model.DeviceName) p.Device = null;
                model.Selected.Device = model.DeviceName;
                model.SaveProfiles();
                Sync();
            };
            var linkHelp = Hint("A linked profile is picked automatically when that device becomes the Windows sound output.",
                                new Rectangle(16, 274, ew - 32, 32));
            StyleCheck(autoApplyBox, "Also apply it automatically"); autoApplyBox.Location = new Point(16, 308);
            autoApplyBox.CheckedChanged += (s, e) => { if (!updating) { model.AutoApply = autoApplyBox.Checked; Settings.SetBool("autoApply", model.AutoApply); } };

            noSelection.Text = "Select or add a profile."; noSelection.ForeColor = Rio.Dim; noSelection.AutoSize = true;
            noSelection.Location = new Point(16, 16); noSelection.Visible = false;
            editor.Controls.AddRange(new Control[] { nameCap, nameBox, offCap, msLabel, resetBtn, useGameBtn, offHelp,
                                                     linkCap, linkedLabel, unlinkBtn, linkBtn, linkHelp, autoApplyBox, noSelection });
            Controls.Add(editor);

            // Display mode: SD (4:3 CRT theme) or HD / Full HD (widescreen theme)
            var dispCap = new Label { Text = "Game display", AutoSize = true, ForeColor = Rio.Dim, Font = Rio.UI(9f, FontStyle.Bold),
                                      Location = new Point(18, H - 148), Anchor = AB | AL };
            displayBox.DropDownStyle = ComboBoxStyle.DropDownList; displayBox.FlatStyle = FlatStyle.Flat;
            displayBox.BackColor = Rio.Field; displayBox.ForeColor = Color.White; displayBox.Font = Rio.UI(9.75f);
            displayBox.Bounds = new Rectangle(124, H - 152, 270, 26); displayBox.Anchor = AB | AL;
            displayBox.Items.Add("Keep current setting");
            foreach (var m in DisplayMode.All) displayBox.Items.Add(m.Name);
            displayBox.SelectedIndexChanged += (s, e) =>
            {
                if (updating) return;
                int i = displayBox.SelectedIndex;
                model.Display = i <= 0 ? null : DisplayMode.All[i - 1];
                if (model.AutoApply) model.Apply(); else model.Notify();
            };
            tips.SetToolTip(displayBox, "Resolution and theme the game starts with. SD = 640×480 4:3 CRT theme; HD / Full HD = widescreen theme.");
            var dispHint = new Label { Text = "SD = 640×480 CRT theme  ·  HD / Full HD = widescreen theme", AutoSize = false,
                                       ForeColor = Rio.Faint, Font = Rio.UI(8.25f), TextAlign = ContentAlignment.MiddleLeft,
                                       Bounds = new Rectangle(404, H - 150, W - 404 - 18, 22), Anchor = AB | AL | AR, AutoEllipsis = true };
            Controls.AddRange(new Control[] { dispCap, displayBox, dispHint });

            // Footer
            runningLabel.Text = "⚠  Rave It Out is running. Quit it before applying — it saves its settings on exit.";
            runningLabel.ForeColor = Color.Gold; runningLabel.Font = Rio.UI(9f, FontStyle.Bold);
            runningLabel.Bounds = new Rectangle(18, H - 110, W - 36, 20); runningLabel.Anchor = AB | AL | AR;
            statusLabel.ForeColor = Rio.Cyan; statusLabel.Bounds = new Rectangle(18, H - 86, W - 36 - 390, 40);
            statusLabel.Anchor = AB | AL | AR; statusLabel.TextAlign = ContentAlignment.MiddleLeft; statusLabel.AutoEllipsis = true;
            StyleCheck(closeBox, "Close after launch"); closeBox.Location = new Point(W - 18 - 378, H - 76); closeBox.Anchor = AB | AR;
            closeBox.CheckedChanged += (s, e) => { if (!updating) { model.CloseAfterLaunch = closeBox.Checked; Settings.SetBool("closeAfterLaunch", model.CloseAfterLaunch); } };
            applyBtn.Bounds = new Rectangle(W - 18 - 238, H - 84, 90, 36); applyBtn.Anchor = AB | AR;
            playBtn.Bounds = new Rectangle(W - 18 - 140, H - 84, 140, 36); playBtn.Anchor = AB | AR;
            applyBtn.Click += (s, e) => { CommitOffset(); model.Apply(); };
            playBtn.Click += (s, e) => { CommitOffset(); if (model.ApplyAndLaunch()) Close(); };
            tips.SetToolTip(applyBtn, "Write the offset to Save\\Preferences.ini (Ctrl+S)");
            tips.SetToolTip(playBtn, "Apply, then start Rave It Out (Ctrl+Enter)");

            var folderGlyph = new Label { Text = "", Font = new Font("Segoe MDL2 Assets", 9f), ForeColor = Rio.Faint,
                                          Bounds = new Rectangle(18, H - 34, 20, 20), Anchor = AB | AL, TextAlign = ContentAlignment.MiddleLeft };
            folderLabel.ForeColor = Rio.Faint; folderLabel.Font = Rio.UI(8.25f); folderLabel.AutoEllipsis = true;
            folderLabel.Bounds = new Rectangle(40, H - 34, W - 40 - 18 - 96, 20); folderLabel.Anchor = AB | AL | AR;
            folderLabel.TextAlign = ContentAlignment.MiddleLeft;
            changeBtn.Bounds = new Rectangle(W - 18 - 88, H - 38, 88, 28); changeBtn.Anchor = AB | AR; changeBtn.Font = Rio.UI(8.25f, FontStyle.Bold);
            changeBtn.Click += (s, e) => ChooseFolder();
            Controls.AddRange(new Control[] { runningLabel, statusLabel, closeBox, applyBtn, playBtn, folderGlyph, folderLabel, changeBtn });
        }

        static void StyleBox(TextBox b, float size, FontStyle st = FontStyle.Regular)
        {
            b.BackColor = Rio.Field; b.ForeColor = Color.White; b.BorderStyle = BorderStyle.FixedSingle; b.Font = Rio.UI(size, st);
        }
        static void StyleCheck(CheckBox c, string text)
        {
            c.Text = text; c.AutoSize = true; c.ForeColor = Color.White; c.Font = Rio.UI(9f); c.BackColor = Color.Transparent;
        }
        static Label Hint(string text, Rectangle r) => new Label
        {
            Text = text, Bounds = r, ForeColor = Rio.Faint, Font = Rio.UI(8.25f),
            Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
        };

        NeonButton Step(double delta, string label)
        {
            var b = new NeonButton(label) { Size = new Size(48, 32) };
            b.Click += (s, e) => { CommitOffset(); if (model.Selected != null) SetOffset(Math.Round((model.Selected.Offset + delta) * 1e6, MidpointRounding.AwayFromZero) / 1e6); };
            tips.SetToolTip(b, (delta > 0 ? "+" : "−") + Fmt.RoundAway(Math.Abs(delta) * 1000) + " ms");
            return b;
        }

        void SetOffset(double v)
        {
            if (model.Selected == null) return;
            model.Selected.Offset = v;
            model.SaveProfiles();
            Sync();
        }

        void CommitOffset()
        {
            if (model.Selected == null || updating) return;
            double v;
            if (Fmt.TryParse(offsetBox.Text, out v))
            {
                v = Math.Round(v, 6, MidpointRounding.AwayFromZero);
                if (!Fmt.Same(v, model.Selected.Offset) || v != model.Selected.Offset) { SetOffset(v); return; }
            }
            updating = true; offsetBox.Text = Fmt.Field(model.Selected.Offset); updating = false;
        }

        void ChooseFolder()
        {
            using (var d = new FolderBrowserDialog { Description = "Choose the Rave It Out folder (the one with Program and Save)" })
            {
                if (model.GameFolder != null) d.SelectedPath = model.GameFolder;
                if (d.ShowDialog(this) == DialogResult.OK) model.SetGameFolder(d.SelectedPath);
            }
        }

        void OnKey(object sender, KeyEventArgs e)
        {
            if (e.Control && e.KeyCode == Keys.S) { e.SuppressKeyPress = true; if (applyBtn.Enabled) applyBtn.PerformClick(); }
            else if (e.Control && e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; if (playBtn.Enabled) playBtn.PerformClick(); }
        }

        void Poll()
        {
            model.RefreshDevice(false);
            if (model.RefreshRunning())
            {
                model.RefreshGameOffset();   // the game rewrites Preferences.ini when it quits
                Sync();
            }
            else SyncHeader();
        }

        void SyncHeader()
        {
            deviceGlyph.Text = model.DeviceGlyph;
            deviceLabel.Text = model.DeviceName;
            var a = model.ActiveProfile;
            gameOffsetLabel.Text = model.GameOffset.HasValue
                ? "Game offset now: " + Fmt.Offset(model.GameOffset.Value) + " s" + (a != null ? "  (" + a.Name + ")" : "")
                : "Game offset: not set yet";
        }

        void Sync()
        {
            if (IsDisposed) return;
            updating = true;
            SyncHeader();

            // list
            var items = list.Items.Cast<AudioProfile>().ToList();
            if (!items.SequenceEqual(model.Profiles))
            {
                list.BeginUpdate();
                list.Items.Clear();
                foreach (var p in model.Profiles) list.Items.Add(p);
                list.EndUpdate();
            }
            int sel = model.Profiles.IndexOf(model.Selected);
            if (list.SelectedIndex != sel) list.SelectedIndex = sel;
            list.Invalidate();
            delBtn.Enabled = model.Profiles.Count > 1;
            saveGameBtn.Visible = model.GameOffset.HasValue && model.ActiveProfile == null;

            // editor
            var s = model.Selected;
            foreach (Control c in editor.Controls) if (c != noSelection) c.Visible = s != null;
            noSelection.Visible = s == null;
            if (s != null)
            {
                if (nameBox.Text != s.Name) nameBox.Text = s.Name;
                if (!offsetBox.Focused || !Fmt.TryParse(offsetBox.Text, out double cur) || cur != s.Offset) offsetBox.Text = Fmt.Field(s.Offset);
                msLabel.Text = "= " + Fmt.RoundAway(s.Offset * 1000) + " ms";
                bool showGame = model.GameOffset.HasValue && !Fmt.Same(model.GameOffset.Value, s.Offset);
                useGameBtn.Visible = showGame;
                if (showGame) useGameBtn.Text = "Use game's value (" + Fmt.Field(model.GameOffset.Value) + ")";
                bool linked = s.Device != null;
                linkedLabel.Visible = unlinkBtn.Visible = linked;
                linkBtn.Visible = !linked;
                linkedLabel.Text = linked ? "\U0001F517  " + s.Device : "";
                linkBtn.Text = "Link to “" + model.DeviceName + "”";
                autoApplyBox.Checked = model.AutoApply;
            }

            // display
            displayBox.SelectedIndex = model.Display == null ? 0 : Array.IndexOf(DisplayMode.All, model.Display) + 1;
            displayBox.Enabled = !model.GameRunning;

            // footer
            runningLabel.Visible = model.GameRunning;
            statusLabel.Text = model.Status;
            closeBox.Checked = model.CloseAfterLaunch;
            applyBtn.Enabled = !model.GameRunning;
            playBtn.Enabled = !model.GameRunning && model.GameExe != null;
            closeBox.Enabled = !model.GameRunning;
            folderLabel.Text = model.GameFolder ?? "Game folder not found";
            updating = false;
        }
    }

    static class Program
    {

        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm(new AppModel()));
        }
    }
#endif
}
