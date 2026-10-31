using System;
using System.Windows.Media;

namespace DeskFolder
{
    /// <summary>DPI 与颜色工具</summary>
    public static class DpiUtil
    {
        public static double Scale = 1.0;

        public static void Init()
        {
            try
            {
                using (var g = System.Drawing.Graphics.FromHwnd(IntPtr.Zero))
                {
                    Scale = g.DpiX / 96.0;
                }
            }
            catch
            {
                Scale = 1.0;
            }
            if (Scale <= 0.1 || Scale > 8) Scale = 1.0;
        }

        /// <summary>物理像素 -> WPF 设备无关像素</summary>
        public static double PxToDip(double px) { return px / Scale; }

        /// <summary>WPF 设备无关像素 -> 物理像素</summary>
        public static double DipToPx(double dip) { return dip * Scale; }
    }

    public static class Colors2
    {
        public static Color Hsv(double h, double s, double v, double alpha)
        {
            if (h < 0) h += 360;
            h = h % 360;
            int i = (int)Math.Floor(h / 60.0) % 6;
            double f = h / 60.0 - Math.Floor(h / 60.0);
            double p = v * (1 - s);
            double q = v * (1 - f * s);
            double t = v * (1 - (1 - f) * s);
            double r = 0, g = 0, b = 0;
            switch (i)
            {
                case 0: r = v; g = t; b = p; break;
                case 1: r = q; g = v; b = p; break;
                case 2: r = p; g = v; b = t; break;
                case 3: r = p; g = q; b = v; break;
                case 4: r = t; g = p; b = v; break;
                default: r = v; g = p; b = q; break;
            }
            byte a = (byte)Math.Max(0, Math.Min(255, (int)Math.Round(alpha * 255)));
            return Color.FromArgb(a,
                (byte)Math.Max(0, Math.Min(255, (int)Math.Round(r * 255))),
                (byte)Math.Max(0, Math.Min(255, (int)Math.Round(g * 255))),
                (byte)Math.Max(0, Math.Min(255, (int)Math.Round(b * 255))));
        }

        public static SolidColorBrush Brush(byte a, byte r, byte g, byte b)
        {
            return new SolidColorBrush(Color.FromArgb(a, r, g, b));
        }
    }
}
