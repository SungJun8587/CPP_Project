//***************************************************************************
// GroundRenderer.cs : 숲 바닥(풀/흙/꽃)과 월드 바깥의 짙은 숲 테두리. 전부 좌표 해시로 만들어서
// 카메라가 움직여도 무늬가 월드에 붙어 있고, 별도 텍스처 없이 결정적으로 같은 모양이 나온다.
// 바닥은 화면보다 큰 비트맵에 한 번 그려 두고 카메라가 여유분 밖으로 나가거나 확대가 바뀔 때만 다시 그린다.
//***************************************************************************
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace GameViewer.App.Render
{
    public sealed class GroundRenderer : IDisposable
    {
        private const int Shades = 40;
        private const int MarginPx = 320;
        private readonly SolidBrush[] _grass = new SolidBrush[Shades];
        private readonly SolidBrush[] _dirt = new SolidBrush[Shades];
        private readonly SolidBrush _outside = new SolidBrush(Color.FromArgb(18, 40, 24));
        private readonly SolidBrush[] _flowers = {
            new SolidBrush(Color.FromArgb(255, 240, 120)), new SolidBrush(Color.FromArgb(250, 250, 250)),
            new SolidBrush(Color.FromArgb(255, 150, 190)), new SolidBrush(Color.FromArgb(170, 190, 255)) };
        private readonly Pen _tuftDark = new Pen(Color.FromArgb(70, 40, 100, 40), 1f);
        private readonly Pen _tuftLight = new Pen(Color.FromArgb(90, 150, 200, 90), 1f);

        private Bitmap _bmp;
        private double _bmpScale;
        private float _bmpWorldX, _bmpWorldY;          // 비트맵 왼쪽 위의 월드 좌표
        public int Rebuilds;

        public GroundRenderer()
        {
            for (int i = 0; i < Shades; ++i)
            {
                float t = i / (float)(Shades - 1);
                _grass[i] = new SolidBrush(Lerp(Color.FromArgb(52, 100, 44), Color.FromArgb(96, 146, 62), t));
                _dirt[i] = new SolidBrush(Lerp(Color.FromArgb(104, 84, 56), Color.FromArgb(136, 112, 72), t));
            }
        }

        public void Dispose()
        {
            if (_bmp != null) _bmp.Dispose();
            foreach (SolidBrush b in _grass) b.Dispose();
            foreach (SolidBrush b in _dirt) b.Dispose();
            foreach (SolidBrush b in _flowers) b.Dispose();
            _outside.Dispose(); _tuftDark.Dispose(); _tuftLight.Dispose();
        }

        public void Draw(Graphics g, ViewState v, float minX, float minY, float maxX, float maxY)
        {
            int bw = v.Width + MarginPx * 2, bh = v.Height + MarginPx * 2;
            float vx0 = v.ToWorldX(0), vy0 = v.ToWorldY(0), vx1 = v.ToWorldX(v.Width), vy1 = v.ToWorldY(v.Height);
            bool dirty = _bmp == null || _bmp.Width != bw || _bmp.Height != bh || Math.Abs(_bmpScale - v.Scale) > 1e-6
                || vx0 < _bmpWorldX || vy0 < _bmpWorldY || vx1 > _bmpWorldX + (float)(bw / v.Scale) || vy1 > _bmpWorldY + (float)(bh / v.Scale);
            if (dirty)
            {
                if (_bmp == null || _bmp.Width != bw || _bmp.Height != bh) { if (_bmp != null) _bmp.Dispose(); _bmp = new Bitmap(bw, bh, PixelFormat.Format32bppArgb); }
                // 비트맵 중심이 지금 화면 중심에 오도록
                _bmpScale = v.Scale;
                _bmpWorldX = v.CamX - (float)(bw / 2.0 / v.Scale);
                _bmpWorldY = v.CamY - (float)(bh / 2.0 / v.Scale);
                ViewState bv = new ViewState();
                bv.Width = bw; bv.Height = bh; bv.Scale = v.Scale; bv.CamX = v.CamX; bv.CamY = v.CamY;
                using (Graphics bg = Graphics.FromImage(_bmp))
                {
                    bg.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    Render(bg, bv, minX, minY, maxX, maxY);
                }
                ++Rebuilds;
            }
            // 비트맵 중 화면에 보이는 부분만 복사한다
            int ox = (int)Math.Round(v.ToScreenX(_bmpWorldX)), oy = (int)Math.Round(v.ToScreenY(_bmpWorldY));
            int sx0 = Math.Max(0, -ox), sy0 = Math.Max(0, -oy);
            int sw = Math.Min(bw - sx0, v.Width - Math.Max(0, ox)), sh = Math.Min(bh - sy0, v.Height - Math.Max(0, oy));
            CompositingMode cm = g.CompositingMode;
            g.CompositingMode = CompositingMode.SourceCopy;
            g.DrawImage(_bmp, new Rectangle(ox + sx0, oy + sy0, sw, sh), sx0, sy0, sw, sh, GraphicsUnit.Pixel);
            g.CompositingMode = cm;
        }

        private void Render(Graphics g, ViewState v, float minX, float minY, float maxX, float maxY)
        {
            // 한 칸이 너무 작아지면 칸 크기를 키워 그리는 개수를 제한한다 (최소 7px).
            float cell = 1f;
            while (cell * v.Scale < 7) cell *= 2f;

            float wx0 = v.ToWorldX(0), wy0 = v.ToWorldY(0), wx1 = v.ToWorldX(v.Width), wy1 = v.ToWorldY(v.Height);
            int cx0 = (int)Math.Floor(wx0 / cell) - 1, cx1 = (int)Math.Ceiling(wx1 / cell) + 1;
            int cy0 = (int)Math.Floor(wy0 / cell) - 1, cy1 = (int)Math.Ceiling(wy1 / cell) + 1;

            g.SmoothingMode = SmoothingMode.None;
            float size = (float)(cell * v.Scale) + 1f;

            for (int cy = cy0; cy <= cy1; ++cy)
            {
                for (int cx = cx0; cx <= cx1; ++cx)
                {
                    float wx = cx * cell, wy = cy * cell;
                    float sx = v.ToScreenX(wx), sy = v.ToScreenY(wy);
                    bool outside = wx + cell <= minX || wy + cell <= minY || wx >= maxX || wy >= maxY;
                    if (outside) { g.FillRectangle(_outside, sx, sy, size, size); continue; }

                    float n = Noise.Value(wx * 0.045f, wy * 0.045f, 7) * 0.65f + Noise.Value(wx * 0.17f, wy * 0.17f, 11) * 0.35f;
                    float jitter = (Noise.Hash01(cx, cy, 3) - 0.5f) * 0.14f;
                    float dirtMask = Noise.Value(wx * 0.03f + 40f, wy * 0.03f + 90f, 19);
                    int idx = (int)((n + jitter) * (Shades - 1));
                    idx = idx < 0 ? 0 : (idx >= Shades ? Shades - 1 : idx);
                    SolidBrush b = dirtMask > 0.72f ? _dirt[idx] : _grass[idx];
                    g.FillRectangle(b, sx, sy, size, size);
                }
            }

            // 풀포기/꽃 (확대했을 때만)
            if (v.Scale >= 9)
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                for (int cy = cy0; cy <= cy1; ++cy)
                {
                    for (int cx = cx0; cx <= cx1; ++cx)
                    {
                        float wx = cx * cell, wy = cy * cell;
                        if (wx < minX || wy < minY || wx + cell > maxX || wy + cell > maxY) continue;
                        uint h = Noise.Hash(cx, cy, 99);
                        float sx = v.ToScreenX(wx + ((h >> 4) & 255) / 255f * cell);
                        float sy = v.ToScreenY(wy + ((h >> 12) & 255) / 255f * cell);
                        if ((h & 3) == 0)
                        {
                            float s = (float)(v.Scale * 0.35);
                            g.DrawLine(_tuftDark, sx, sy, sx - s * 0.5f, sy - s);
                            g.DrawLine(_tuftLight, sx, sy, sx + s * 0.1f, sy - s * 1.2f);
                            g.DrawLine(_tuftDark, sx, sy, sx + s * 0.6f, sy - s * 0.9f);
                        }
                        else if (h % 53 == 0)
                        {
                            SolidBrush fb = _flowers[(h >> 8) % (uint)_flowers.Length];
                            g.FillEllipse(fb, sx - 2.5f, sy - 2.5f, 5, 5);
                            g.FillEllipse(_flowers[0], sx - 1f, sy - 1f, 2, 2);
                        }
                    }
                }
            }
        }

        /// <summary>월드 바깥의 빽빽한 나무 (그려지는 건 뷰포트에 걸린 칸뿐).</summary>
        public void DrawBorderTrees(Graphics g, ViewState v, TreeRenderer trees, float minX, float minY, float maxX, float maxY)
        {
            float step = 4f;
            float wx0 = v.ToWorldX(-80), wy0 = v.ToWorldY(-80), wx1 = v.ToWorldX(v.Width + 80), wy1 = v.ToWorldY(v.Height + 80);
            // 월드 안쪽만 보이면 건너뛴다
            if (wx0 >= minX - 1 && wx1 <= maxX + 1 && wy0 >= minY - 1 && wy1 <= maxY + 1) return;
            int cx0 = (int)Math.Floor(wx0 / step), cx1 = (int)Math.Ceiling(wx1 / step);
            int cy0 = (int)Math.Floor(wy0 / step), cy1 = (int)Math.Ceiling(wy1 / step);
            for (int cy = cy0; cy <= cy1; ++cy)
            {
                for (int cx = cx0; cx <= cx1; ++cx)
                {
                    float wx = cx * step + Noise.Hash01(cx, cy, 5) * step, wy = cy * step + Noise.Hash01(cx, cy, 6) * step;
                    if (wx >= minX - 1 && wx <= maxX + 1 && wy >= minY - 1 && wy <= maxY + 1) continue;
                    float r = 1.8f + Noise.Hash01(cx, cy, 8) * 1.6f;
                    trees.Draw(g, v, wx, wy, r, Noise.Hash(cx, cy, 9), true);
                }
            }
        }

        private static Color Lerp(Color a, Color b, float t)
        {
            return Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }
    }
}
