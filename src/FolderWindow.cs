using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Shapes;
using IoPath = System.IO.Path;

namespace DeskFolder
{
    /// <summary>桌面上的图标文件夹窗口：收起时是一个图标，点击后像手机文件夹一样展开</summary>
    public class FolderWindow : Window
    {
        const double TileSize = 84;
        const double CollapsedW = 100;
        const double CollapsedH = 112;
        const double PanelW = 388;
        const double CellSize = 90;
        const double ShadowPad = 26;
        const double HeaderH = 32;

        public FolderData Data;
        public Action<FolderWindow> DeleteRequested;

        readonly double _dpi;
        readonly Canvas _root;
        Canvas _iconLayer;
        Border _tile;
        TextBlock _label;
        UniformGrid _preview;
        Path _emptyGlyph;

        Border _panel;
        TextBlock _titleText;
        ScaleTransform _panelScale;
        Storyboard _expandStory;
        readonly List<UIElement> _cells = new List<UIElement>();
        double _panelCenterX;
        double _panelCenterY;

        bool _expanded;
        bool _menuOpen;
        bool _suppressDeactivate;
        Point _pressScreen;
        double _pressLeft;
        double _pressTop;
        bool _pressed;
        bool _dragged;
        DateTime _lastToggle = DateTime.MinValue;
        DateTime _lastExpand = DateTime.MinValue;

        public FolderWindow(FolderData data)
        {
            Data = data;
            _dpi = DpiUtil.Scale;

            WindowStyle = WindowStyle.None;
            AllowsTransparency = true;
            Background = Brushes.Transparent;
            ShowInTaskbar = false;
            ResizeMode = ResizeMode.NoResize;
            Topmost = false;
            WindowStartupLocation = WindowStartupLocation.Manual;
            SnapsToDevicePixels = true;
            UseLayoutRounding = true;
            FontFamily = new FontFamily("Microsoft YaHei UI, Segoe UI");

            Width = CollapsedW;
            Height = CollapsedH;
            Left = DpiUtil.PxToDip(Data.X);
            Top = DpiUtil.PxToDip(Data.Y);

            _root = new Canvas();
            Content = _root;

            BuildIconLayer();
            _root.Children.Add(_iconLayer);
            UpdateLabel();
            UpdatePreview();

            Loaded += delegate { ClampCollapsed(); };
            SourceInitialized += OnSourceInit;
            Deactivated += OnDeactivated;
            PreviewKeyDown += OnKeyDown;
        }

        // ================== 收起状态的图标 ==================

        void BuildIconLayer()
        {
            _iconLayer = new Canvas();
            _iconLayer.Width = CollapsedW;
            _iconLayer.Height = CollapsedH;
            Canvas.SetLeft(_iconLayer, 0);
            Canvas.SetTop(_iconLayer, 0);

            _tile = new Border();
            _tile.Width = TileSize;
            _tile.Height = TileSize;
            _tile.CornerRadius = new CornerRadius(18);
            _tile.BorderThickness = new Thickness(1);
            _tile.BorderBrush = new SolidColorBrush(Color.FromArgb(80, 255, 255, 255));
            _tile.Background = BuildTileBrush(Data.Hue);
            _tile.Cursor = Cursors.Hand;
            _tile.RenderTransformOrigin = new Point(0.5, 0.5);
            _tile.RenderTransform = new ScaleTransform(1, 1);
            _tile.AllowDrop = true;

            Grid inner = new Grid();
            inner.Margin = new Thickness(7);

            _preview = new UniformGrid();
            _preview.Rows = 3;
            _preview.Columns = 3;
            inner.Children.Add(_preview);

            _emptyGlyph = new Path();
            _emptyGlyph.Data = Geometry.Parse("M0,7 L9,7 L12,10 L26,10 L26,24 L0,24 Z");
            _emptyGlyph.Fill = new SolidColorBrush(Color.FromArgb(70, 255, 255, 255));
            _emptyGlyph.Width = 24;
            _emptyGlyph.Height = 22;
            _emptyGlyph.Stretch = Stretch.Uniform;
            _emptyGlyph.HorizontalAlignment = HorizontalAlignment.Center;
            _emptyGlyph.VerticalAlignment = VerticalAlignment.Center;
            inner.Children.Add(_emptyGlyph);

            _tile.Child = inner;
            Canvas.SetLeft(_tile, (CollapsedW - TileSize) / 2);
            Canvas.SetTop(_tile, 0);
            _iconLayer.Children.Add(_tile);

            _label = new TextBlock();
            _label.FontSize = 12;
            _label.FontWeight = FontWeights.SemiBold;
            _label.Foreground = Brushes.White;
            _label.TextAlignment = TextAlignment.Center;
            _label.TextTrimming = TextTrimming.CharacterEllipsis;
            _label.Width = CollapsedW;
            _label.MaxHeight = 18;
            _label.UseLayoutRounding = true;
            _label.Effect = new DropShadowEffect { Color = Colors.Black, BlurRadius = 8, ShadowDepth = 0, Opacity = 0.95 };
            Canvas.SetLeft(_label, 0);
            Canvas.SetTop(_label, TileSize + 2);
            _iconLayer.Children.Add(_label);

            _tile.MouseLeftButtonDown += IconMouseDown;
            _tile.MouseMove += IconMouseMove;
            _tile.MouseLeftButtonUp += IconMouseUp;
            _tile.MouseRightButtonUp += delegate { ShowFolderMenu(); };
            _tile.MouseEnter += delegate { AnimateTile(1.06); };
            _tile.MouseLeave += delegate { AnimateTile(1.0); };
            _tile.DragEnter += TileDragOver;
            _tile.DragOver += TileDragOver;
            _tile.Drop += TileDrop;
            _tile.LostMouseCapture += delegate { _pressed = false; _dragged = false; };

            _label.MouseLeftButtonDown += IconMouseDown;
            _label.MouseMove += IconMouseMove;
            _label.MouseLeftButtonUp += IconMouseUp;
            _label.MouseRightButtonUp += delegate { ShowFolderMenu(); };
            _label.LostMouseCapture += delegate { _pressed = false; _dragged = false; };
        }

        static Brush BuildTileBrush(int hue)
        {
            Color c1 = Colors2.Hsv(hue, 0.46, 0.36, 0.95);
            Color c2 = Colors2.Hsv(hue + 10, 0.55, 0.19, 0.97);
            return new LinearGradientBrush(c1, c2, 90);
        }

        void AnimateTile(double scale)
        {
            ScaleTransform st = (ScaleTransform)_tile.RenderTransform;
            DoubleAnimation a = new DoubleAnimation(scale, new Duration(TimeSpan.FromMilliseconds(140)));
            a.EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut };
            st.BeginAnimation(ScaleTransform.ScaleXProperty, a);
            st.BeginAnimation(ScaleTransform.ScaleYProperty, a);
        }

        void UpdateLabel()
        {
            _label.Text = Data.Name;
            _label.ToolTip = Data.Name;
        }

        void UpdatePreview()
        {
            _preview.Children.Clear();
            int n = Math.Min(9, Data.Items.Count);
            _emptyGlyph.Visibility = n == 0 ? Visibility.Visible : Visibility.Collapsed;
            for (int i = 0; i < n; i++)
            {
                ImageSource src = IconUtil.Get(Data.Items[i], 32);
                if (src == null) continue;
                Image img = new Image();
                img.Source = src;
                img.Stretch = Stretch.Uniform;
                img.Margin = new Thickness(1.5);
                _preview.Children.Add(img);
            }
        }

        // ================== 拖动 / 点击 ==================

        static Point CursorPos()
        {
            System.Drawing.Point p = System.Windows.Forms.Control.MousePosition;
            return new Point(p.X, p.Y);
        }

        void IconMouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left) return;
            if (_expanded) return;
            _pressed = true;
            _dragged = false;
            _pressScreen = CursorPos();
            _pressLeft = Left;
            _pressTop = Top;
            ((UIElement)sender).CaptureMouse();
            e.Handled = true;
        }

        void IconMouseMove(object sender, MouseEventArgs e)
        {
            if (!_pressed) return;
            if (e.LeftButton != MouseButtonState.Pressed)
            {
                _pressed = false;
                return;
            }
            Point cur = CursorPos();
            double dx = cur.X - _pressScreen.X;
            double dy = cur.Y - _pressScreen.Y;
            if (!_dragged && Math.Abs(dx) + Math.Abs(dy) > 4) _dragged = true;
            if (_dragged)
            {
                Left = _pressLeft + DpiUtil.PxToDip(dx);
                Top = _pressTop + DpiUtil.PxToDip(dy);
            }
        }

        void IconMouseUp(object sender, MouseButtonEventArgs e)
        {
            if (!_pressed) return;
            bool wasDragged = _dragged;
            _pressed = false;
            _dragged = false;
            UIElement el = (UIElement)sender;
            if (el.IsMouseCaptured) el.ReleaseMouseCapture();

            if (wasDragged)
            {
                ClampCollapsed();
                Data.X = (int)Math.Round(DpiUtil.DipToPx(Left));
                Data.Y = (int)Math.Round(DpiUtil.DipToPx(Top));
                DataStore.Save();
                return;
            }

            if ((DateTime.Now - _lastToggle).TotalMilliseconds < 350) return;
            _lastToggle = DateTime.Now;
            Expand();
        }

        void ClampCollapsed()
        {
            Rect wa = WorkAreaDip(Left + CollapsedW / 2, Top + CollapsedH / 2);
            double l = Left;
            double t = Top;
            if (l + CollapsedW > wa.Right - 4) l = wa.Right - CollapsedW - 4;
            if (l < wa.Left + 4) l = wa.Left + 4;
            if (t + CollapsedH > wa.Bottom - 4) t = wa.Bottom - CollapsedH - 4;
            if (t < wa.Top + 4) t = wa.Top + 4;
            if (Math.Abs(l - Left) > 0.5 || Math.Abs(t - Top) > 0.5)
            {
                Left = l;
                Top = t;
                Data.X = (int)Math.Round(DpiUtil.DipToPx(Left));
                Data.Y = (int)Math.Round(DpiUtil.DipToPx(Top));
                DataStore.Save();
            }
        }

        static Rect WorkAreaDip(double cx, double cy)
        {
            double dpi = DpiUtil.Scale;
            System.Drawing.Point p = new System.Drawing.Point((int)Math.Round(cx * dpi), (int)Math.Round(cy * dpi));
            System.Drawing.Rectangle r = System.Windows.Forms.Screen.FromPoint(p).WorkingArea;
            return new Rect(r.Left / dpi, r.Top / dpi, r.Width / dpi, r.Height / dpi);
        }

        void OnSourceInit(object sender, EventArgs e)
        {
            IntPtr hwnd = new System.Windows.Interop.WindowInteropHelper(this).Handle;
            int ex = NativeMethods.GetWindowLong(hwnd, NativeMethods.GWL_EXSTYLE);
            ex = (ex | NativeMethods.WS_EX_TOOLWINDOW) & ~NativeMethods.WS_EX_APPWINDOW;
            NativeMethods.SetWindowLong(hwnd, NativeMethods.GWL_EXSTYLE, ex);
        }

        void OnDeactivated(object sender, EventArgs e)
        {
            if (!_expanded || _menuOpen || _suppressDeactivate) return;
            if ((DateTime.Now - _lastExpand).TotalMilliseconds < 250) return;
            Collapse();
        }

        void OnKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape && _expanded)
            {
                Collapse();
                e.Handled = true;
            }
        }

        // ================== 展开 ==================

        void Expand()
        {
            if (_expanded) return;
            _expanded = true;
            _lastExpand = DateTime.Now;
            _suppressDeactivate = true;

            double icx = DpiUtil.PxToDip(Data.X) + CollapsedW / 2;
            double icy = DpiUtil.PxToDip(Data.Y) + 42;

            BuildPanel();
            _panel.Visibility = Visibility.Visible;
            _panel.Measure(new Size(PanelW, double.PositiveInfinity));
            PlacePanel(icx, icy);

            _panelScale = new ScaleTransform(0.35, 0.35, _panelCenterX, _panelCenterY);
            _panel.RenderTransform = _panelScale;
            _panel.Opacity = 0;
            _panel.IsHitTestVisible = true;

            _iconLayer.IsHitTestVisible = false;

            Duration dur = new Duration(TimeSpan.FromMilliseconds(240));
            CubicEase ease = new CubicEase { EasingMode = EasingMode.EaseOut };

            Storyboard sb = new Storyboard();

            DoubleAnimation ax = new DoubleAnimation(0.35, 1.0, dur);
            ax.EasingFunction = ease;
            AddAnim(sb, ax, _panelScale, ScaleTransform.ScaleXProperty);

            DoubleAnimation ay = new DoubleAnimation(0.35, 1.0, dur);
            ay.EasingFunction = ease;
            AddAnim(sb, ay, _panelScale, ScaleTransform.ScaleYProperty);

            DoubleAnimation fadeIn = new DoubleAnimation(0, 1, new Duration(TimeSpan.FromMilliseconds(200)));
            AddAnim(sb, fadeIn, _panel, UIElement.OpacityProperty);

            DoubleAnimation fadeIcon = new DoubleAnimation(1, 0, new Duration(TimeSpan.FromMilliseconds(130)));
            AddAnim(sb, fadeIcon, _iconLayer, UIElement.OpacityProperty);

            int idx = 0;
            foreach (UIElement c in _cells)
            {
                c.Opacity = 0;
                TranslateTransform tt = new TranslateTransform(0, 10);
                c.RenderTransform = tt;

                TimeSpan delay = TimeSpan.FromMilliseconds(120 + Math.Min(idx, 14) * 22);
                CubicEase ci = new CubicEase { EasingMode = EasingMode.EaseOut };

                DoubleAnimation op = new DoubleAnimation(0, 1, new Duration(TimeSpan.FromMilliseconds(190)));
                op.BeginTime = delay;
                op.EasingFunction = ci;
                AddAnim(sb, op, c, UIElement.OpacityProperty);

                DoubleAnimation ty = new DoubleAnimation(10, 0, new Duration(TimeSpan.FromMilliseconds(190)));
                ty.BeginTime = delay;
                ty.EasingFunction = ci;
                AddAnim(sb, ty, tt, TranslateTransform.YProperty);
                idx++;
            }

            sb.Completed += delegate
            {
                _iconLayer.Visibility = Visibility.Collapsed;
                _iconLayer.BeginAnimation(UIElement.OpacityProperty, null);
                _iconLayer.Opacity = 1;
                _suppressDeactivate = false;
            };

            _expandStory = sb;
            sb.Begin();
            Activate();
        }

        void Collapse()
        {
            if (!_expanded) return;
            _expanded = false;
            _suppressDeactivate = true;

            if (_expandStory != null)
            {
                _expandStory.Stop();
                _expandStory = null;
            }

            if (_panel == null)
            {
                _suppressDeactivate = false;
                return;
            }

            double ix = Canvas.GetLeft(_iconLayer) + CollapsedW / 2;
            double iy = Canvas.GetTop(_iconLayer) + 42;

            _iconLayer.Visibility = Visibility.Visible;
            _iconLayer.IsHitTestVisible = false;
            _iconLayer.Opacity = 0;

            if (_panelScale == null) _panelScale = new ScaleTransform(1, 1, _panelCenterX, _panelCenterY);
            _panelScale.CenterX = ix - ShadowPad;
            _panelScale.CenterY = iy - ShadowPad;
            _panelScale.ScaleX = 1;
            _panelScale.ScaleY = 1;

            CubicEase easeIn = new CubicEase { EasingMode = EasingMode.EaseIn };
            Duration dur = new Duration(TimeSpan.FromMilliseconds(190));

            Storyboard sb = new Storyboard();

            DoubleAnimation sx = new DoubleAnimation(0.38, dur);
            sx.EasingFunction = easeIn;
            AddAnim(sb, sx, _panelScale, ScaleTransform.ScaleXProperty);

            DoubleAnimation sy = new DoubleAnimation(0.38, dur);
            sy.EasingFunction = easeIn;
            AddAnim(sb, sy, _panelScale, ScaleTransform.ScaleYProperty);

            DoubleAnimation op = new DoubleAnimation(0, new Duration(TimeSpan.FromMilliseconds(140)));
            op.BeginTime = TimeSpan.FromMilliseconds(40);
            AddAnim(sb, op, _panel, UIElement.OpacityProperty);

            DoubleAnimation opIcon = new DoubleAnimation(1, new Duration(TimeSpan.FromMilliseconds(170)));
            opIcon.BeginTime = TimeSpan.FromMilliseconds(110);
            AddAnim(sb, opIcon, _iconLayer, UIElement.OpacityProperty);

            sb.Completed += delegate { FinishCollapse(); };
            sb.Begin();
        }

        void FinishCollapse()
        {
            if (_panel != null) _panel.Visibility = Visibility.Collapsed;
            if (_panelScale != null)
            {
                _panelScale.BeginAnimation(ScaleTransform.ScaleXProperty, null);
                _panelScale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
                _panelScale.ScaleX = 1;
                _panelScale.ScaleY = 1;
            }
            if (_panel != null) _panel.BeginAnimation(UIElement.OpacityProperty, null);

            _iconLayer.BeginAnimation(UIElement.OpacityProperty, null);
            _iconLayer.Opacity = 1;
            _iconLayer.Visibility = Visibility.Visible;
            _iconLayer.IsHitTestVisible = true;
            Canvas.SetLeft(_iconLayer, 0);
            Canvas.SetTop(_iconLayer, 0);

            Width = CollapsedW;
            Height = CollapsedH;
            Left = DpiUtil.PxToDip(Data.X);
            Top = DpiUtil.PxToDip(Data.Y);
            _suppressDeactivate = false;
        }

        void PlacePanel(double icx, double icy)
        {
            double ph = _panel.DesiredSize.Height;
            double winW = PanelW + ShadowPad * 2;
            double winH = ph + ShadowPad * 2;

            Rect wa = WorkAreaDip(icx, icy);
            double l = icx - winW / 2;
            double t = icy - winH / 2;

            if (winW <= wa.Width - 16)
            {
                if (l + winW > wa.Right - 8) l = wa.Right - winW - 8;
                if (l < wa.Left + 8) l = wa.Left + 8;
            }
            else l = wa.Left + 8;

            if (winH <= wa.Height - 16)
            {
                if (t + winH > wa.Bottom - 8) t = wa.Bottom - winH - 8;
                if (t < wa.Top + 8) t = wa.Top + 8;
            }
            else t = wa.Top + 8;

            Width = winW;
            Height = winH;
            Left = l;
            Top = t;

            double ix = icx - l;
            double iy = icy - t;
            if (ix < 0) ix = 0;
            if (ix > winW) ix = winW;
            if (iy < 0) iy = 0;
            if (iy > winH) iy = winH;

            Canvas.SetLeft(_iconLayer, ix - CollapsedW / 2);
            Canvas.SetTop(_iconLayer, iy - 42);
            Canvas.SetLeft(_panel, ShadowPad);
            Canvas.SetTop(_panel, ShadowPad);

            _panelCenterX = ix - ShadowPad;
            _panelCenterY = iy - ShadowPad;
        }

        // ================== 展开面板内容 ==================

        void BuildPanel()
        {
            if (_panel != null)
            {
                _root.Children.Remove(_panel);
                _panel = null;
            }
            _cells.Clear();

            _panel = new Border();
            _panel.Width = PanelW;
            _panel.CornerRadius = new CornerRadius(20);
            _panel.Background = new SolidColorBrush(Color.FromArgb(236, 22, 23, 27));
            _panel.BorderThickness = new Thickness(1);
            _panel.BorderBrush = new SolidColorBrush(Color.FromArgb(46, 255, 255, 255));
            _panel.Padding = new Thickness(14, 10, 14, 14);
            _panel.Effect = new DropShadowEffect { Color = Colors.Black, BlurRadius = 30, ShadowDepth = 8, Opacity = 0.6, Direction = 270 };
            _panel.AllowDrop = true;
            _panel.Visibility = Visibility.Collapsed;
            _panel.DragEnter += PanelDragOver;
            _panel.DragOver += PanelDragOver;
            _panel.Drop += PanelDrop;

            StackPanel sp = new StackPanel();

            // 标题 + 关闭
            Grid header = new Grid();
            header.Height = HeaderH;
            header.Margin = new Thickness(2, 0, 2, 2);

            _titleText = new TextBlock();
            _titleText.Text = Data.Name;
            _titleText.FontSize = 13;
            _titleText.FontWeight = FontWeights.SemiBold;
            _titleText.Foreground = new SolidColorBrush(Color.FromArgb(255, 241, 241, 244));
            _titleText.VerticalAlignment = VerticalAlignment.Center;
            _titleText.TextTrimming = TextTrimming.CharacterEllipsis;
            _titleText.Margin = new Thickness(0, 0, 36, 0);
            _titleText.Cursor = Cursors.Arrow;
            header.Children.Add(_titleText);

            Border close = new Border();
            close.Width = 26;
            close.Height = 26;
            close.CornerRadius = new CornerRadius(8);
            close.Background = Brushes.Transparent;
            close.Cursor = Cursors.Hand;
            close.HorizontalAlignment = HorizontalAlignment.Right;
            close.VerticalAlignment = VerticalAlignment.Center;
            TextBlock closeTxt = new TextBlock();
            closeTxt.Text = "✕";
            closeTxt.FontSize = 12;
            closeTxt.Foreground = new SolidColorBrush(Color.FromArgb(255, 168, 169, 178));
            closeTxt.HorizontalAlignment = HorizontalAlignment.Center;
            closeTxt.VerticalAlignment = VerticalAlignment.Center;
            close.Child = closeTxt;
            close.MouseEnter += delegate { close.Background = new SolidColorBrush(Color.FromArgb(46, 255, 255, 255)); };
            close.MouseLeave += delegate { close.Background = Brushes.Transparent; };
            close.MouseLeftButtonUp += delegate { Collapse(); };
            header.Children.Add(close);

            sp.Children.Add(header);

            if (Data.Items.Count == 0)
            {
                Grid emptyBox = new Grid();
                emptyBox.Height = 2 * CellSize;
                TextBlock hint = new TextBlock();
                hint.Text = "把文件或快捷方式拖进来";
                hint.FontSize = 12;
                hint.Foreground = new SolidColorBrush(Color.FromArgb(255, 138, 139, 148));
                hint.HorizontalAlignment = HorizontalAlignment.Center;
                hint.VerticalAlignment = VerticalAlignment.Center;
                emptyBox.Children.Add(hint);
                sp.Children.Add(emptyBox);
            }
            else
            {
                WrapPanel wrap = new WrapPanel();
                wrap.Width = 360;
                wrap.ItemWidth = CellSize;
                wrap.ItemHeight = CellSize;
                wrap.Orientation = Orientation.Horizontal;
                foreach (string p in Data.Items)
                {
                    Border cell = MakeCell(p);
                    _cells.Add(cell);
                    wrap.Children.Add(cell);
                }
                sp.Children.Add(wrap);
            }

            _panel.Child = sp;
            Canvas.SetLeft(_panel, ShadowPad);
            Canvas.SetTop(_panel, ShadowPad);
            _root.Children.Add(_panel);
        }

        Border MakeCell(string path)
        {
            string name = IoPath.GetFileName(path);
            if (name.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase)) name = name.Substring(0, name.Length - 4);
            if (name.EndsWith(".url", StringComparison.OrdinalIgnoreCase)) name = name.Substring(0, name.Length - 4);
            if (name.Length == 0) name = path;

            Border cell = new Border();
            cell.Width = 82;
            cell.Height = 82;
            cell.Margin = new Thickness(4);
            cell.CornerRadius = new CornerRadius(12);
            cell.Background = Brushes.Transparent;
            cell.Cursor = Cursors.Hand;
            cell.Tag = path;
            cell.ToolTip = path;

            StackPanel sp = new StackPanel();

            Grid box = new Grid();
            box.Width = 48;
            box.Height = 48;
            box.HorizontalAlignment = HorizontalAlignment.Center;
            box.Margin = new Thickness(0, 7, 0, 0);
            ImageSource src = IconUtil.Get(path, 48);
            if (src == null) src = IconUtil.Fallback;
            Image img = new Image();
            img.Source = src;
            img.Stretch = Stretch.Uniform;
            box.Children.Add(img);

            TextBlock tb = new TextBlock();
            tb.Text = name;
            tb.FontSize = 11;
            tb.Foreground = new SolidColorBrush(Color.FromArgb(255, 224, 225, 229));
            tb.TextAlignment = TextAlignment.Center;
            tb.TextTrimming = TextTrimming.CharacterEllipsis;
            tb.Margin = new Thickness(3, 4, 3, 0);
            tb.MaxHeight = 16;

            sp.Children.Add(box);
            sp.Children.Add(tb);
            cell.Child = sp;

            cell.MouseEnter += delegate { cell.Background = new SolidColorBrush(Color.FromArgb(28, 255, 255, 255)); };
            cell.MouseLeave += delegate { cell.Background = Brushes.Transparent; };
            cell.MouseLeftButtonUp += delegate
            {
                IconUtil.Launch(path);
                Collapse();
            };
            cell.MouseRightButtonUp += delegate { ShowItemMenu(path); };
            return cell;
        }

        // ================== 拖放 ==================

        void TileDragOver(object sender, DragEventArgs e)
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
            e.Handled = true;
        }

        void TileDrop(object sender, DragEventArgs e)
        {
            HandleDrop(e);
        }

        void PanelDragOver(object sender, DragEventArgs e)
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
            e.Handled = true;
        }

        void PanelDrop(object sender, DragEventArgs e)
        {
            HandleDrop(e);
        }

        void HandleDrop(DragEventArgs e)
        {
            string[] files = e.Data.GetData(DataFormats.FileDrop) as string[];
            if (files == null) return;
            bool added = false;
            foreach (string f in files)
            {
                if (DataStore.AddItem(Data, f)) added = true;
            }
            if (!added) return;
            IconUtil.ClearCache();
            UpdatePreview();
            if (_expanded) RefreshPanel();
            e.Handled = true;
        }

        // ================== 菜单与操作 ==================

        void ShowFolderMenu()
        {
            if (_expanded) return;
            _menuOpen = true;
            System.Windows.Forms.ContextMenuStrip menu = new System.Windows.Forms.ContextMenuStrip();
            menu.ShowImageMargin = false;
            menu.Items.Add("添加文件…", null, delegate { CloseMenu(menu); AddFilesDialog(); });
            menu.Items.Add("重命名…", null, delegate { CloseMenu(menu); RenameDialog(); });
            menu.Items.Add(new System.Windows.Forms.ToolStripSeparator());
            menu.Items.Add("删除这个文件夹", null, delegate { CloseMenu(menu); ConfirmDelete(); });
            menu.Closed += delegate { _menuOpen = false; };
            menu.Show(System.Windows.Forms.Cursor.Position);
        }

        void CloseMenu(System.Windows.Forms.ContextMenuStrip menu)
        {
            _menuOpen = false;
            menu.Close();
        }

        void ShowItemMenu(string path)
        {
            _menuOpen = true;
            System.Windows.Forms.ContextMenuStrip menu = new System.Windows.Forms.ContextMenuStrip();
            menu.ShowImageMargin = false;
            menu.Items.Add("打开", null, delegate
            {
                CloseMenu(menu);
                IconUtil.Launch(path);
                Collapse();
            });
            menu.Items.Add("在资源管理器中显示", null, delegate
            {
                CloseMenu(menu);
                ShowInExplorer(path);
            });
            menu.Items.Add(new System.Windows.Forms.ToolStripSeparator());
            menu.Items.Add("从文件夹移除", null, delegate { CloseMenu(menu); RemoveItem(path); });
            menu.Closed += delegate { _menuOpen = false; };
            menu.Show(System.Windows.Forms.Cursor.Position);
        }

        static void ShowInExplorer(string path)
        {
            try
            {
                if (System.IO.Directory.Exists(path))
                    System.Diagnostics.Process.Start("explorer.exe", "\"" + path + "\"");
                else
                    System.Diagnostics.Process.Start("explorer.exe", "/select,\"" + path + "\"");
            }
            catch { }
        }

        void AddFilesDialog()
        {
            _suppressDeactivate = true;
            _menuOpen = false;
            System.Windows.Forms.OpenFileDialog dlg = new System.Windows.Forms.OpenFileDialog();
            dlg.Title = "添加到「" + Data.Name + "」";
            dlg.Multiselect = true;
            dlg.Filter = "所有文件|*.*";
            bool added = false;
            if (dlg.ShowDialog() == System.Windows.Forms.DialogResult.OK)
            {
                foreach (string f in dlg.FileNames)
                    if (DataStore.AddItem(Data, f)) added = true;
            }
            _suppressDeactivate = false;
            if (!added) return;
            IconUtil.ClearCache();
            UpdatePreview();
            if (_expanded) RefreshPanel();
        }

        void RenameDialog()
        {
            _suppressDeactivate = true;
            string name = NameDialog.Show("重命名图标文件夹", Data.Name);
            _suppressDeactivate = false;
            if (name == null) return;
            if (!DataStore.Rename(Data, name)) return;
            UpdateLabel();
            if (_titleText != null) _titleText.Text = Data.Name;
        }

        void ConfirmDelete()
        {
            _suppressDeactivate = true;
            System.Windows.Forms.DialogResult r = System.Windows.Forms.MessageBox.Show(
                "确定删除文件夹「" + Data.Name + "」吗？\n（里面的原始文件不会被删除）",
                "删除文件夹",
                System.Windows.Forms.MessageBoxButtons.YesNo,
                System.Windows.Forms.MessageBoxIcon.Question);
            _suppressDeactivate = false;
            if (r != System.Windows.Forms.DialogResult.Yes) return;

            DataStore.Delete(Data);
            if (DeleteRequested != null) DeleteRequested(this);
            Close();
        }

        void RemoveItem(string path)
        {
            DataStore.RemoveItem(Data, path);
            UpdatePreview();
            if (_expanded) RefreshPanel();
        }

        void RefreshPanel()
        {
            double icx = DpiUtil.PxToDip(Data.X) + CollapsedW / 2;
            double icy = DpiUtil.PxToDip(Data.Y) + 42;

            BuildPanel();
            _panel.Visibility = Visibility.Visible;
            _panel.Measure(new Size(PanelW, double.PositiveInfinity));
            PlacePanel(icx, icy);

            _panelScale = new ScaleTransform(1, 1, _panelCenterX, _panelCenterY);
            _panel.RenderTransform = _panelScale;
            _panel.Opacity = 1;
            _panel.Visibility = Visibility.Visible;
            _panel.IsHitTestVisible = true;
            _lastExpand = DateTime.Now.Subtract(TimeSpan.FromHours(1));
        }

        // ================== 工具 ==================

        static void AddAnim(Storyboard sb, Timeline anim, DependencyObject target, DependencyProperty prop)
        {
            Storyboard.SetTarget(anim, target);
            Storyboard.SetTargetProperty(anim, new PropertyPath(prop));
            sb.Children.Add(anim);
        }
    }
}
