using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows;
using WinForms = System.Windows.Forms;

namespace DeskFolder
{
    public static class Program
    {
        static Mutex _mutex;
        static EventWaitHandle _signal;
        static WinForms.NotifyIcon _tray;
        static readonly Dictionary<string, FolderWindow> OpenWindows = new Dictionary<string, FolderWindow>();
        static bool _running = true;

        [STAThread]
        public static void Main(string[] args)
        {
            bool create = false;
            string name = null;
            for (int i = 0; i < args.Length; i++)
            {
                if (string.Equals(args[i], "--create", StringComparison.OrdinalIgnoreCase))
                {
                    create = true;
                    if (i + 1 < args.Length && !args[i + 1].StartsWith("-"))
                    {
                        name = args[i + 1];
                        i++;
                    }
                }
            }

            DataStore.Init();
            DpiUtil.Init();

            AppDomain.CurrentDomain.UnhandledException += delegate(object s, UnhandledExceptionEventArgs e)
            {
                try
                {
                    File.AppendAllText(Path.Combine(DataStore.Dir, "error.log"),
                        DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + e.ExceptionObject + "\r\n");
                }
                catch { }
            };

            bool first;
            _mutex = new Mutex(true, "DeskFolder_App_Mutex", out first);
            _signal = new EventWaitHandle(false, EventResetMode.AutoReset, "DeskFolder_App_Signal");

            if (!first)
            {
                // 已有实例在运行：把命令投递过去
                if (create) Enqueue(name);
                try { _signal.Set(); } catch { }
                return;
            }

            Application app = new Application();
            app.ShutdownMode = ShutdownMode.OnExplicitShutdown;
            app.Startup += delegate
            {
                WriteIconFile();
                InstallRegistry();
                BuildTray();
                DataStore.Load();
                foreach (FolderData f in DataStore.Folders) ShowFolder(f);
                ProcessQueue();
                StartSignalLoop();
                if (create) CreateFolder(name);
            };
            app.Run();
        }

        // ---------------- 文件夹 ----------------

        static void CreateFolder(string name)
        {
            string final = name;
            if (string.IsNullOrEmpty(final) || final.Trim().Length == 0)
            {
                string n = NameDialog.Show("新建图标文件夹", "图标文件夹");
                if (n == null) return;
                final = n;
            }
            FolderData f = DataStore.Create(final.Trim());
            ShowFolder(f);
        }

        static void ShowFolder(FolderData f)
        {
            if (OpenWindows.ContainsKey(f.Id)) return;
            FolderWindow w = new FolderWindow(f);
            w.DeleteRequested = delegate { OpenWindows.Remove(f.Id); };
            w.Closed += delegate { OpenWindows.Remove(f.Id); };
            OpenWindows[f.Id] = w;
            w.Show();
        }

        static void ShowAll()
        {
            if (OpenWindows.Count == 0)
            {
                foreach (FolderData f in DataStore.Folders) ShowFolder(f);
                return;
            }
            foreach (FolderWindow w in OpenWindows.Values)
            {
                w.Show();
                w.Activate();
            }
        }

        // ---------------- 托盘 ----------------

        static void BuildTray()
        {
            _tray = new WinForms.NotifyIcon();
            _tray.Icon = IconUtil.CreateAppIcon();
            _tray.Text = "桌面图标文件夹";
            _tray.Visible = true;

            WinForms.ContextMenuStrip menu = new WinForms.ContextMenuStrip();
            menu.Items.Add("新建图标文件夹…", null, delegate { CreateFolder(null); });
            menu.Items.Add("显示全部文件夹", null, delegate { ShowAll(); });
            menu.Items.Add(new WinForms.ToolStripSeparator());
            menu.Items.Add("退出", null, delegate { Quit(); });
            _tray.ContextMenuStrip = menu;
            _tray.DoubleClick += delegate { CreateFolder(null); };
        }

        static void Quit()
        {
            _running = false;
            try { if (_signal != null) _signal.Set(); } catch { }
            if (_tray != null)
            {
                _tray.Visible = false;
                _tray.Dispose();
                _tray = null;
            }
            foreach (FolderWindow w in new List<FolderWindow>(OpenWindows.Values)) w.Close();
            if (Application.Current != null) Application.Current.Shutdown();
        }

        // ---------------- 桌面右键菜单 ----------------

        static void WriteIconFile()
        {
            try
            {
                if (File.Exists(DataStore.IconFile)) return;
                System.Drawing.Icon ico = IconUtil.CreateAppIcon();
                using (FileStream fs = new FileStream(DataStore.IconFile, FileMode.Create)) ico.Save(fs);
            }
            catch { }
        }

        static void InstallRegistry()
        {
            try
            {
                string exe = System.Diagnostics.Process.GetCurrentProcess().MainModule.FileName;
                using (Microsoft.Win32.RegistryKey k = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(
                    @"Software\Classes\DesktopBackground\Shell\DeskFolderNew"))
                {
                    k.SetValue(null, "新建图标文件夹");
                    k.SetValue("Icon", DataStore.IconFile);
                    k.SetValue("Position", "Top");
                }
                using (Microsoft.Win32.RegistryKey k = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(
                    @"Software\Classes\DesktopBackground\Shell\DeskFolderNew\command"))
                {
                    k.SetValue(null, "\"" + exe + "\" --create");
                }
            }
            catch { }
        }

        // ---------------- 单实例命令队列 ----------------

        static void Enqueue(string name)
        {
            try
            {
                if (!Directory.Exists(DataStore.QueueDir)) Directory.CreateDirectory(DataStore.QueueDir);
                string file = Path.Combine(DataStore.QueueDir,
                    DateTime.Now.Ticks.ToString("D") + "_" + Guid.NewGuid().ToString("N").Substring(0, 8) + ".cmd");
                File.WriteAllText(file, name == null ? "" : Convert.ToBase64String(Encoding.UTF8.GetBytes(name)), Encoding.UTF8);
            }
            catch { }
        }

        static void ProcessQueue()
        {
            try
            {
                if (!Directory.Exists(DataStore.QueueDir)) return;
                FileInfo[] files = new DirectoryInfo(DataStore.QueueDir).GetFiles("*.cmd");
                Array.Sort(files, delegate(FileInfo a, FileInfo b) { return string.CompareOrdinal(a.Name, b.Name); });
                foreach (FileInfo fi in files)
                {
                    string name = null;
                    try
                    {
                        string s = File.ReadAllText(fi.FullName).Trim();
                        if (s.Length > 0) name = Encoding.UTF8.GetString(Convert.FromBase64String(s));
                    }
                    catch { }
                    try { fi.Delete(); } catch { }
                    CreateFolder(name);
                }
            }
            catch { }
        }

        static void StartSignalLoop()
        {
            Thread t = new Thread(delegate()
            {
                while (_running)
                {
                    try
                    {
                        if (!_signal.WaitOne(1000)) continue;
                    }
                    catch { break; }

                    if (!_running) break;
                    Application cur = Application.Current;
                    if (cur == null) break;
                    cur.Dispatcher.BeginInvoke(new Action(delegate { ProcessQueue(); }));
                }
            });
            t.IsBackground = true;
            t.Start();
        }
    }
}
