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
