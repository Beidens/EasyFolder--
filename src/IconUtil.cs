using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DeskFolder
{
    public static class IconUtil
    {
        static readonly Dictionary<string, ImageSource> Cache = new Dictionary<string, ImageSource>(StringComparer.OrdinalIgnoreCase);
        static ImageSource _fallback;

        public static ImageSource Get(string path, int size)
        {
            string key = path + "|" + size;
            ImageSource cached;
            if (Cache.TryGetValue(key, out cached)) return cached;

            ImageSource img = Load(path, size);
            Cache[key] = img;
            return img;
        }

        public static void ClearCache()
        {
            Cache.Clear();
        }

        static ImageSource Load(string path, int size)
        {
            if (string.IsNullOrEmpty(path)) return null;
            if (!File.Exists(path) && !Directory.Exists(path)) return null;

            IntPtr hbm = IntPtr.Zero;
            try
            {
                Guid iid = typeof(IShellItemImageFactory).GUID;
                IShellItemImageFactory factory;
                int hr = SHCreateItemFromParsingName(path, IntPtr.Zero, ref iid, out factory);
                if (hr == 0 && factory != null)
                {
                    SIZE sz = new SIZE();
                    sz.cx = size;
                    sz.cy = size;
                    hr = factory.GetImage(sz, SIIGBF.ICONONLY | SIIGBF.BIGGERSIZEOK, out hbm);
                }
            }
            catch { }

            if (hbm != IntPtr.Zero)
            {
                ImageSource img = FromHBitmap(hbm);
                DeleteObject(hbm);
                if (img != null) return img;
            }

            return FromShellIconInfo(path);
        }

        static ImageSource FromHBitmap(IntPtr hbm)
        {
            try
            {
                BITMAP bm = new BITMAP();
                if (GetObject(hbm, Marshal.SizeOf(typeof(BITMAP)), ref bm) == 0) return null;
                int w = bm.bmWidth;
                int h = Math.Abs(bm.bmHeight);
                if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return null;

                int stride = w * 4;
                byte[] bits = new byte[stride * h];

                BITMAPINFO bmi = new BITMAPINFO();
                bmi.bmiColors = new uint[1];
                bmi.bmiHeader = new BITMAPINFOHEADER();
                bmi.bmiHeader.biSize = Marshal.SizeOf(typeof(BITMAPINFOHEADER));
                bmi.bmiHeader.biWidth = w;
                bmi.bmiHeader.biHeight = -h;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = 0;

                IntPtr dc = CreateCompatibleDC(IntPtr.Zero);
                int lines = GetDIBits(dc, hbm, 0, (uint)h, bits, ref bmi, 0);
                DeleteDC(dc);
                if (lines == 0) return null;

                bool hasAlpha = false;
                for (int i = 3; i < bits.Length; i += 4)
                {
                    if (bits[i] != 0) { hasAlpha = true; break; }
                }
                if (!hasAlpha)
                {
                    for (int i = 3; i < bits.Length; i += 4) bits[i] = 255;
                }

                BitmapSource src = BitmapSource.Create(w, h, 96, 96, PixelFormats.Pbgra32, null, bits, stride);
                src.Freeze();
                return src;
            }
            catch { return null; }
        }

        static ImageSource FromShellIconInfo(string path)
        {
            IntPtr hIcon = IntPtr.Zero;
            try
            {
                SHFILEINFO info = new SHFILEINFO();
                uint flags = SHGFI_ICON | SHGFI_LARGEICON;
                IntPtr res = SHGetFileInfo(path, 0, ref info, (uint)Marshal.SizeOf(typeof(SHFILEINFO)), flags);
                if (res == IntPtr.Zero || info.hIcon == IntPtr.Zero) return null;
                hIcon = info.hIcon;

                using (var bmp = System.Drawing.Icon.FromHandle(hIcon).ToBitmap())
                using (var ms = new MemoryStream())
                {
                    bmp.Save(ms, System.Drawing.Imaging.ImageFormat.Png);
                    ms.Position = 0;
                    BitmapFrame frame = BitmapFrame.Create(ms, BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
                    frame.Freeze();
                    return frame;
                }
            }
            catch { return null; }
            finally
            {
                if (hIcon != IntPtr.Zero) DestroyIcon(hIcon);
            }
        }

        public static ImageSource Fallback
        {
            get
            {
                if (_fallback == null)
                {
                    var dv = new DrawingVisual();
                    using (DrawingContext dc = dv.RenderOpen())
                    {
                        dc.DrawRoundedRectangle(new SolidColorBrush(Color.FromArgb(255, 62, 63, 70)), null,
                            new System.Windows.Rect(0, 0, 48, 48), 10, 10);
                        var pen = new System.Windows.Media.Pen(new SolidColorBrush(Color.FromArgb(255, 150, 151, 158)), 1.5);
                        dc.DrawLine(pen, new System.Windows.Point(14, 24), new System.Windows.Point(34, 24));
                        dc.DrawLine(pen, new System.Windows.Point(24, 14), new System.Windows.Point(24, 34));
                    }
                    var bmp = new RenderTargetBitmap(48, 48, 96, 96, PixelFormats.Pbgra32);
                    bmp.Render(dv);
                    bmp.Freeze();
                    _fallback = bmp;
                }
                return _fallback;
            }
        }

        public static void Launch(string path)
        {
            try
            {
                System.Diagnostics.ProcessStartInfo psi = new System.Diagnostics.ProcessStartInfo();
                psi.FileName = path;
                psi.UseShellExecute = true;
                psi.WorkingDirectory = System.IO.Path.GetDirectoryName(path);
                System.Diagnostics.Process.Start(psi);
            }
            catch { }
        }

        /// <summary>生成托盘/右键菜单用的文件夹图标</summary>
        public static System.Drawing.Icon CreateAppIcon()
        {
            var bmp = new System.Drawing.Bitmap(32, 32, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = System.Drawing.Graphics.FromImage(bmp))
            {
                g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                g.Clear(System.Drawing.Color.Transparent);

                var rect = new System.Drawing.Rectangle(2, 2, 28, 28);
                using (var path = RoundRect(rect, 8))
                using (var br = new System.Drawing.Drawing2D.LinearGradientBrush(
                    new System.Drawing.Rectangle(0, 0, 32, 32),
                    System.Drawing.Color.FromArgb(255, 84, 145, 250),
                    System.Drawing.Color.FromArgb(255, 44, 92, 210), 90f))
                {
                    g.FillPath(br, path);
                }

                using (var white = new System.Drawing.SolidBrush(System.Drawing.Color.FromArgb(255, 245, 247, 250)))
                using (var dim = new System.Drawing.SolidBrush(System.Drawing.Color.FromArgb(140, 255, 255, 255)))
                {
                    g.FillRectangle(white, 7, 8, 8, 8);
                    g.FillRectangle(dim, 17, 8, 8, 8);
                    g.FillRectangle(dim, 7, 18, 8, 8);
                    g.FillRectangle(dim, 17, 18, 8, 8);
                }
            }
            IntPtr h = bmp.GetHicon();
            return System.Drawing.Icon.FromHandle(h);
        }

        static System.Drawing.Drawing2D.GraphicsPath RoundRect(System.Drawing.Rectangle r, int radius)
        {
            var path = new System.Drawing.Drawing2D.GraphicsPath();
            int d = radius * 2;
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        // ---------------- Win32 ----------------

        [StructLayout(LayoutKind.Sequential)]
        struct SIZE { public int cx; public int cy; }

        [StructLayout(LayoutKind.Sequential)]
        struct BITMAP
        {
            public int bmType;
            public int bmWidth;
            public int bmHeight;
            public int bmWidthBytes;
            public short bmPlanes;
            public short bmBitsPixel;
            public IntPtr bmBits;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct BITMAPINFOHEADER
        {
            public int biSize;
            public int biWidth;
            public int biHeight;
            public short biPlanes;
            public short biBitCount;
            public int biCompression;
            public int biSizeImage;
            public int biXPelsPerMeter;
            public int biYPelsPerMeter;
            public int biClrUsed;
            public int biClrImportant;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct BITMAPINFO
        {
            public BITMAPINFOHEADER bmiHeader;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 1)]
            public uint[] bmiColors;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct SHFILEINFO
        {
            public IntPtr hIcon;
            public int iIcon;
            public uint dwAttributes;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string szDisplayName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)]
            public string szTypeName;
        }

        [ComImport, Guid("BCC18B79-BA16-442F-80C4-8A59C30C463B"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        interface IShellItemImageFactory
        {
            [PreserveSig]
            int GetImage(SIZE size, SIIGBF flags, out IntPtr phbm);
        }

        [Flags]
        enum SIIGBF
        {
            RESIZETOFIT = 0x00,
            BIGGERSIZEOK = 0x01,
            MEMORYONLY = 0x02,
            ICONONLY = 0x04,
            THUMBNAILONLY = 0x08,
            INCACHEONLY = 0x10
        }

        const uint SHGFI_ICON = 0x100;
        const uint SHGFI_LARGEICON = 0x0;

        [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
        static extern int SHCreateItemFromParsingName(
            [MarshalAs(UnmanagedType.LPWStr)] string pszPath, IntPtr pbc, ref Guid riid,
            [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory ppv);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr SHGetFileInfo(string pszPath, uint dwFileAttributes, ref SHFILEINFO psfi, uint cbFileInfo, uint uFlags);

        [DllImport("user32.dll")]
        static extern bool DestroyIcon(IntPtr hIcon);

        [DllImport("gdi32.dll")]
        static extern IntPtr CreateCompatibleDC(IntPtr hDC);

        [DllImport("gdi32.dll")]
        static extern bool DeleteDC(IntPtr hDC);

        [DllImport("gdi32.dll")]
        static extern bool DeleteObject(IntPtr hObject);

        [DllImport("gdi32.dll")]
        static extern int GetObject(IntPtr hObject, int nCount, ref BITMAP lpObject);

        [DllImport("gdi32.dll")]
        static extern int GetDIBits(IntPtr hdc, IntPtr hbm, uint start, uint cLines, byte[] lpvBits, ref BITMAPINFO lpbmi, uint usage);
    }
}
