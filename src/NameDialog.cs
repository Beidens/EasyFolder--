using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;

namespace DeskFolder
{
    public static class NativeMethods
    {
        public const int GWL_EXSTYLE = -20;
        public const int WS_EX_TOOLWINDOW = 0x00000080;
        public const int WS_EX_APPWINDOW = 0x00040000;

        [DllImport("user32.dll")]
        public static extern int GetWindowLong(IntPtr hWnd, int nIndex);

        [DllImport("user32.dll")]
        public static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);
    }

    /// <summary>圆角暗色输入框对话框</summary>
    public static class NameDialog
    {
        public static string Show(string title, string initial)
        {
            Window w = new Window();
            w.WindowStyle = WindowStyle.None;
            w.AllowsTransparency = true;
            w.Background = System.Windows.Media.Brushes.Transparent;
            w.ShowInTaskbar = false;
            w.ResizeMode = ResizeMode.NoResize;
            w.Topmost = true;
            w.Width = 340;
            w.Height = 170;
            w.FontFamily = new System.Windows.Media.FontFamily("Microsoft YaHei UI, Segoe UI");

            double dpi = DpiUtil.Scale;
            System.Drawing.Rectangle wa = System.Windows.Forms.Screen.FromPoint(System.Windows.Forms.Cursor.Position).WorkingArea;
            w.Left = (wa.Left + wa.Width / 2.0) / dpi - w.Width / 2;
            w.Top = (wa.Top + wa.Height / 3.0) / dpi - w.Height / 2;

            Border root = new Border();
            root.CornerRadius = new CornerRadius(16);
            root.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(245, 26, 27, 32));
            root.BorderThickness = new Thickness(1);
            root.BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(48, 255, 255, 255));
            root.Padding = new Thickness(20, 16, 20, 16);
            root.Effect = new System.Windows.Media.Effects.DropShadowEffect
            {
                Color = System.Windows.Media.Colors.Black,
                BlurRadius = 28,
                ShadowDepth = 8,
                Opacity = 0.6,
                Direction = 270
            };
            w.Content = root;

            StackPanel sp = new StackPanel();

            TextBlock t = new TextBlock();
            t.Text = title;
            t.FontSize = 14;
            t.FontWeight = FontWeights.SemiBold;
            t.Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 240, 240, 243));
            t.Margin = new Thickness(0, 0, 0, 12);
            sp.Children.Add(t);

            Border box = new Border();
            box.CornerRadius = new CornerRadius(9);
            box.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 41, 42, 48));
            box.BorderThickness = new Thickness(1);
            box.BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(60, 255, 255, 255));
            box.Padding = new Thickness(10, 0, 10, 0);
            box.Height = 38;

            TextBox tb = new TextBox();
            tb.Text = initial;
            tb.FontSize = 14;
            tb.Foreground = System.Windows.Media.Brushes.White;
            tb.Background = System.Windows.Media.Brushes.Transparent;
            tb.BorderThickness = new Thickness(0);
            tb.CaretBrush = System.Windows.Media.Brushes.White;
            tb.VerticalContentAlignment = VerticalAlignment.Center;
            tb.VerticalAlignment = VerticalAlignment.Center;
            box.Child = tb;
            sp.Children.Add(box);

            StackPanel buttons = new StackPanel();
            buttons.Orientation = Orientation.Horizontal;
            buttons.HorizontalAlignment = HorizontalAlignment.Right;
            buttons.Margin = new Thickness(0, 16, 0, 0);

            string result = null;

            Border cancel = MakeButton("取消", false);
            cancel.Margin = new Thickness(0, 0, 8, 0);
            cancel.MouseLeftButtonUp += delegate { w.Close(); };
            buttons.Children.Add(cancel);

            Border ok = MakeButton("确定", true);
            ok.MouseLeftButtonUp += delegate
            {
                string v = tb.Text == null ? "" : tb.Text.Trim();
                if (v.Length == 0) return;
                result = v;
                w.Close();
            };
            buttons.Children.Add(ok);

            sp.Children.Add(buttons);
            root.Child = sp;

            tb.KeyDown += delegate(object s, System.Windows.Input.KeyEventArgs e)
            {
                if (e.Key == System.Windows.Input.Key.Enter)
                {
                    string v = tb.Text == null ? "" : tb.Text.Trim();
                    if (v.Length == 0) return;
                    result = v;
                    w.Close();
                }
            };
            w.KeyDown += delegate(object s, System.Windows.Input.KeyEventArgs e)
            {
                if (e.Key == System.Windows.Input.Key.Escape) w.Close();
            };
            tb.Loaded += delegate { tb.Focus(); tb.SelectAll(); };
            w.Loaded += delegate { w.Activate(); tb.Focus(); tb.SelectAll(); };

            w.ShowDialog();
            return result;
        }

        static Border MakeButton(string text, bool primary)
        {
            Border b = new Border();
            b.CornerRadius = new CornerRadius(9);
            b.Padding = new Thickness(18, 7, 18, 7);
            b.Cursor = System.Windows.Input.Cursors.Hand;
            b.Background = primary
                ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 62, 128, 245))
                : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 52, 53, 60));
            b.MouseEnter += delegate
            {
                b.Background = primary
                    ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 82, 146, 255))
                    : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 68, 69, 78));
            };
            b.MouseLeave += delegate
            {
                b.Background = primary
                    ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 62, 128, 245))
                    : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromArgb(255, 52, 53, 60));
            };

            TextBlock t = new TextBlock();
            t.Text = text;
            t.FontSize = 13;
            t.Foreground = System.Windows.Media.Brushes.White;
            t.HorizontalAlignment = HorizontalAlignment.Center;
            t.VerticalAlignment = VerticalAlignment.Center;
            b.Child = t;
            return b;
        }
    }
}
