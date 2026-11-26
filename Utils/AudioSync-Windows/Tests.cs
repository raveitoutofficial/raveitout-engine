// Test the Preferences.ini writer and the profiles JSON on copies (build with build.ps1 -Test).
// Usage: AudioSyncTests.exe <Preferences.ini> <work-dir> [AudioSyncProfiles.json]
using System;
using System.IO;
using System.Linq;
using RaveItOut.AudioSync;

static class Tests
{
    static int failures;
    static void Check(bool ok, string what) { Console.WriteLine((ok ? "ok   " : "FAIL ") + what); if (!ok) failures++; }

    static int Main(string[] args)
    {
        string src = args[0], work = args[1];
        Directory.CreateDirectory(work);
        string tmp = Path.Combine(work, "Preferences.ini");
        File.Copy(src, tmp, true);
        byte[] before = File.ReadAllBytes(src);

        Console.WriteLine("read: " + PrefsINI.ReadOffset(tmp));
        PrefsINI.WriteOffset(-0.181129, tmp);
        Check(PrefsINI.ReadOffset(tmp) == -0.181129, "after -0.181129");
        PrefsINI.WriteOffset(PrefsINI.ReadOffset(src) ?? 0, tmp);
        Check(File.ReadAllBytes(tmp).SequenceEqual(before), "writing the original value back gives identical bytes");
        PrefsINI.WriteOffset(0, tmp);
        Check(PrefsINI.ReadOffset(tmp) == 0, "after 0");
        PrefsINI.WriteOffset(-0.181129, tmp);   // leave it changed for the diff

        // missing file and missing key
        string fresh = Path.Combine(work, "fresh", "Preferences.ini");
        if (File.Exists(fresh)) File.Delete(fresh);
        PrefsINI.WriteOffset(0.05, fresh);
        Check(File.ReadAllText(fresh) == "[Options]\r\nGlobalOffsetSeconds=0.050000\r\n", "fresh file");
        string nokey = Path.Combine(work, "nokey.ini");
        File.WriteAllText(nokey, "[Game]\r\nA=1\r\n[Options]\r\nB=2\r\n");
        PrefsINI.WriteOffset(-0.1, nokey);
        Check(File.ReadAllText(nokey) == "[Game]\r\nA=1\r\n[Options]\r\nGlobalOffsetSeconds=-0.100000\r\nB=2\r\n", "key added under [Options]");

        // display modes: resolution + theme, per-game Theme only where it is a Rave It Out theme
        string disp = Path.Combine(work, "display.ini");
        File.WriteAllText(disp, "[Options]\r\nDisplayAspectRatio=1.777778\r\nDisplayHeight=720\r\nDisplayWidth=1280\r\nTheme=RaveItOut\r\n\r\n"
                              + "[Game-dance]\r\nTheme=default\r\n\r\n[Game-pump]\r\nTheme=RaveItOut\r\n");
        Check(DisplayMode.Read(disp) == DisplayMode.All[1], "reads HD 720p");
        PrefsINI.WriteDisplay(DisplayMode.All[0], disp);
        Check(File.ReadAllText(disp) == "[Options]\r\nDisplayAspectRatio=1.333333\r\nDisplayHeight=480\r\nDisplayWidth=640\r\nTheme=RaveItOut-SD-mipmaps\r\n\r\n"
                                       + "[Game-dance]\r\nTheme=default\r\n\r\n[Game-pump]\r\nTheme=RaveItOut-SD-mipmaps\r\n", "SD written, dance theme untouched");
        Check(DisplayMode.Read(disp) == DisplayMode.All[0], "reads SD back");
        PrefsINI.WriteDisplay(DisplayMode.All[2], disp);
        Check(DisplayMode.Read(disp) == DisplayMode.All[2], "Full HD round-trips");
        string dispCopy = Path.Combine(work, "display-real.ini");
        File.Copy(src, dispCopy, true);
        // setting each display key to the value it already has must give identical bytes
        string same = File.ReadAllText(src, System.Text.Encoding.GetEncoding(28591));
        foreach (var k in new[] { "DisplayWidth", "DisplayHeight", "DisplayAspectRatio", "Theme" })
        {
            string v = PrefsINI.ReadValue(src, "[Options]", k);
            if (v != null) same = PrefsINI.SetValue(same, s => s == "[Options]", k, v, false);
        }
        Check(System.Text.Encoding.GetEncoding(28591).GetBytes(same).SequenceEqual(before), "rewriting display keys with their values gives identical bytes");
        var was = DisplayMode.Read(dispCopy);
        PrefsINI.WriteDisplay(DisplayMode.All[0], dispCopy);
        Check(DisplayMode.Read(dispCopy) == DisplayMode.All[0], "SD on a copy of the real prefs");
        Check(File.ReadAllBytes(dispCopy).Length > 0 && File.ReadAllText(dispCopy).Split('\n').Length == File.ReadAllText(src).Split('\n').Length,
              "real prefs: no lines added or lost (was " + (was?.Name ?? "custom") + ")");

        // profiles JSON: a Mac-written file must re-encode byte-for-byte
        if (args.Length > 2)
        {
            string json = File.ReadAllText(args[2]);
            var profiles = ProfileJSON.Load(args[2]);
            Check(profiles != null && profiles.Count > 0, "profiles load (" + (profiles?.Count ?? 0) + ")");
            Check(ProfileJSON.Encode(profiles) == json, "profiles re-encode identically");
            profiles.Add(new AudioProfile { Name = "USB / \"DAC\"", Offset = 0.012, Device = "Speakers (USB Audio)" });
            string outPath = Path.Combine(work, "AudioSyncProfiles.json");
            ProfileJSON.Save(outPath, profiles);
            var again = ProfileJSON.Load(outPath);
            Check(again.Count == profiles.Count && again.Last().Name == "USB / \"DAC\"" && again.Last().Offset == 0.012
                  && again.Last().Device == "Speakers (USB Audio)", "added profile round-trips");
        }
        Console.WriteLine(failures == 0 ? "all passed" : failures + " failed");
        return failures;
    }
}
