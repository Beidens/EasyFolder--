using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace DeskFolder
{
    public class FolderData
    {
        public string Id = "";
        public string Name = "";
        /// <summary>桌面左上角物理像素坐标</summary>
        public int X;
        public int Y;
        public int Hue = 210;
        public List<string> Items = new List<string>();
    }

    /// <summary>文件夹数据存取（%APPDATA%\DeskFolder\folders.dat）</summary>
    public static class DataStore
    {
        public static string Dir = "";
        public static string DataFile = "";
        public static string QueueDir = "";
        public static string IconFile = "";
        public static List<FolderData> Folders = new List<FolderData>();

        static readonly int[] HuePalette = new int[] { 212, 268, 328, 24, 152, 190, 6, 240 };

        public static void Init()
        {
            Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "DeskFolder");
            if (!Directory.Exists(Dir)) Directory.CreateDirectory(Dir);
            DataFile = Path.Combine(Dir, "folders.dat");
            QueueDir = Path.Combine(Dir, "queue");
            IconFile = Path.Combine(Dir, "app.ico");
        }

        public static void Load()
        {
            Folders.Clear();
            if (!File.Exists(DataFile)) return;
            try
            {
                Dictionary<string, FolderData> map = new Dictionary<string, FolderData>();
                foreach (string raw in File.ReadAllLines(DataFile, Encoding.UTF8))
                {
                    string line = raw.Trim();
                    if (line.Length == 0) continue;
                    string[] p = line.Split('|');
                    if (p[0] == "F" && p.Length >= 6)
                    {
                        FolderData f = new FolderData();
                        f.Id = p[1];
                        f.Name = Decode(p[2]);
                        f.X = ParseInt(p[3]);
                        f.Y = ParseInt(p[4]);
                        f.Hue = ParseInt(p[5]);
                        Folders.Add(f);
                        map[f.Id] = f;
                    }
                    else if (p[0] == "I" && p.Length >= 3)
                    {
                        FolderData f;
                        if (map.TryGetValue(p[1], out f))
                        {
                            string path = Decode(p[2]);
                            if (path.Length > 0 && !f.Items.Contains(path)) f.Items.Add(path);
                        }
                    }
                }
            }
            catch { }
        }

        public static void Save()
        {
            try
            {
                StringBuilder sb = new StringBuilder();
                sb.AppendLine("V1");
                foreach (FolderData f in Folders)
                {
                    sb.AppendLine("F|" + f.Id + "|" + Encode(f.Name) + "|" + f.X + "|" + f.Y + "|" + f.Hue);
                    foreach (string it in f.Items)
                        sb.AppendLine("I|" + f.Id + "|" + Encode(it));
                }
                string tmp = DataFile + ".tmp";
                File.WriteAllText(tmp, sb.ToString(), Encoding.UTF8);
                if (File.Exists(DataFile)) File.Delete(DataFile);
                File.Move(tmp, DataFile);
            }
            catch { }
        }

        public static FolderData Create(string name)
        {
            FolderData f = new FolderData();
            f.Id = Guid.NewGuid().ToString("N");
            f.Name = name;
            f.Hue = HuePalette[Folders.Count % HuePalette.Length];

            System.Drawing.Rectangle wa = System.Windows.Forms.Screen.PrimaryScreen.WorkingArea;
            int idx = Folders.Count;
            f.X = wa.Right - 150 - (idx % 3) * 110;
            f.Y = wa.Top + 30 + ((idx / 3) % 4) * 120;

            Folders.Add(f);
            Save();
            return f;
        }

        public static void Delete(FolderData f)
        {
            Folders.Remove(f);
            Save();
        }

        public static bool AddItem(FolderData f, string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            path = path.Trim();
            if (!File.Exists(path) && !Directory.Exists(path)) return false;
            foreach (string it in f.Items)
                if (string.Equals(it, path, StringComparison.OrdinalIgnoreCase)) return false;
            f.Items.Add(path);
            Save();
            return true;
        }

        public static void RemoveItem(FolderData f, string path)
        {
            f.Items.Remove(path);
            Save();
        }

        public static bool Rename(FolderData f, string name)
        {
            if (string.IsNullOrEmpty(name)) return false;
            f.Name = name.Trim();
            Save();
            return true;
        }

        static int ParseInt(string s)
        {
            int v = 0;
            int.TryParse(s, out v);
            return v;
        }

        static string Encode(string s)
        {
            if (s == null) return "";
            return Convert.ToBase64String(Encoding.UTF8.GetBytes(s));
        }

        static string Decode(string s)
        {
            try { return Encoding.UTF8.GetString(Convert.FromBase64String(s)); }
            catch { return ""; }
        }
    }
}
