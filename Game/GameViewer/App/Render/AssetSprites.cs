//***************************************************************************
// AssetSprites.cs : 이미지 파일로 된 캐릭터/몬스터 그림(회전 렌더 시트)을 읽는다.
// Assets/sprites.json 에 적힌 종류만 이미지로 그리고, 적히지 않은 종류는 코드로 그린 그림(Creatures/Monsters)을 그대로 쓴다.
//   { "sprites": [ { "kind": "monster", "template": 9, "pattern": "demon/{0:000}.png", "angles": 18, "height": 4.6 } ] }
//   kind     monster | hero          template  Monster/Hero 시트의 Id
//   pattern  이미지 경로(sprites.json 기준). {0} 에 각도(도, 정수)가 들어간다. 각도 = 번호 * 360 / angles
//   angles   방향 수(1..72). 0도는 화면 아래(카메라 쪽)를 보는 모습이고, 각도가 커지면 화면 오른쪽 -> 위 -> 왼쪽 순으로 돈다.
//   height   그림의 실제 내용(투명하지 않은 부분)이 차지할 높이(단위). 가로는 같은 비율로 정해진다.
//   kind "tree" 는 template 이 없고, Block 의 원(나무) 하나를 이 이미지로 그린다. height 는 반경 1 일 때의 높이이고 실제 높이는 반경에 비례한다.
//            방향 번호는 나무마다 다른 모양을 고르는 데 쓴다(회전 렌더 한 벌을 그대로 쓰면 된다).
// 모든 방향의 투명하지 않은 영역을 합친 상자를 기준으로 발 밑(아래 가운데)과 크기를 잡으므로, 돌 때 흔들리거나 크기가 변하지 않는다.
// 걷기/대기 움직임은 이미지를 위아래로 튀기고 눌러서 흉내 낸다(프레임 애니메이션 시트는 지원하지 않는다).
//***************************************************************************
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using GameViewer.Core.Data;

namespace GameViewer.App.Render
{
    /// <summary>방향별 이미지 한 벌.</summary>
    public sealed class AssetSet
    {
        public Bitmap[] Frames;
        public float Scale;         // 원본 픽셀 하나가 차지하는 단위
        public float Cx, Foot;      // 기준점(원본 픽셀): 가로 중심, 발 밑
        public Look Look;
        public int Count { get { return Frames.Length; } }

        /// <summary>바라보는 방향(화면 좌표 벡터)에 가장 가까운 이미지 번호.</summary>
        public int IndexFor(float fx, float fy)
        {
            if (Frames.Length == 1 || (fx * fx + fy * fy) < 0.0001f) return 0;
            double deg = Math.Atan2(fx, fy) * 180.0 / Math.PI;
            if (deg < 0) deg += 360.0;
            return (int)Math.Round(deg * Frames.Length / 360.0) % Frames.Length;
        }
    }

    public static class AssetSprites
    {
        private static Dictionary<long, AssetSet> _sets;

        private static long Key(bool monster, uint tpl) { return ((monster ? 1L : 0L) << 32) | tpl; }

        /// <summary>나무 이미지(kind "tree")가 있으면 그 세트, 없으면 null.</summary>
        public static AssetSet FindTree()
        {
            Dictionary<long, AssetSet> m = _sets;
            if (m == null) return null;
            AssetSet s;
            return m.TryGetValue(TreeKey, out s) ? s : null;
        }

        private const long TreeKey = 2L << 32;

        /// <summary>이미지가 지정된 종류면 그 세트, 아니면 null (코드로 그린 그림을 쓴다).</summary>
        public static AssetSet Find(bool monster, uint tpl)
        {
            Dictionary<long, AssetSet> m = _sets;
            if (m == null) return null;
            AssetSet s;
            return m.TryGetValue(Key(monster, tpl), out s) ? s : null;
        }

        public static int Count { get { return _sets == null ? 0 : _sets.Count; } }

        public static void Clear()
        {
            Dictionary<long, AssetSet> m = _sets;
            _sets = null;
            if (m == null) return;
            foreach (AssetSet s in m.Values) foreach (Bitmap b in s.Frames) b.Dispose();
        }

        /// <summary>
        /// dir/sprites.json 을 읽어 이미지를 올린다. 폴더나 파일이 없으면 0을 돌려주고(정상), 항목이 잘못되면 그 항목만 건너뛰고 warnings 에 적는다.
        /// 이전에 읽은 것은 버린다.
        /// </summary>
        public static int Load(string dir, List<string> warnings)
        {
            Clear();
            string manifest = Path.Combine(dir, "sprites.json");
            if (!File.Exists(manifest)) return 0;
            Dictionary<long, AssetSet> sets = new Dictionary<long, AssetSet>();
            try
            {
                Dictionary<string, object> root = MiniJson.AsObject(MiniJson.Parse(File.ReadAllText(manifest, System.Text.Encoding.UTF8)));
                List<object> list = root == null ? null : MiniJson.AsArray(root.ContainsKey("sprites") ? root["sprites"] : null);
                if (list == null) { warnings.Add(manifest + ": \"sprites\" 배열이 없습니다"); return 0; }
                for (int i = 0; i < list.Count; ++i)
                {
                    string why = null;
                    long key; AssetSet set = LoadOne(dir, MiniJson.AsObject(list[i]), out key, out why);
                    if (set == null) { warnings.Add(manifest + " [" + i + "]: " + why); continue; }
                    if (sets.ContainsKey(key)) { Dispose(set); warnings.Add(manifest + " [" + i + "]: 같은 종류가 두 번 지정됨"); continue; }
                    sets[key] = set;
                }
            }
            catch (Exception e) { warnings.Add(manifest + ": " + e.Message); foreach (AssetSet s in sets.Values) Dispose(s); return 0; }
            _sets = sets;
            return sets.Count;
        }

        private static void Dispose(AssetSet s) { foreach (Bitmap b in s.Frames) if (b != null) b.Dispose(); }

        private static AssetSet LoadOne(string dir, Dictionary<string, object> o, out long key, out string why)
        {
            key = 0; why = null;
            if (o == null) { why = "항목이 객체가 아닙니다"; return null; }
            string kind = MiniJson.Str(o, "kind", "");
            bool tree = kind == "tree";
            if (kind != "monster" && kind != "hero" && !tree) { why = "kind 는 monster, hero, tree 중 하나"; return null; }
            double tpl = tree ? 0 : MiniJson.Num(o, "template", -1);
            if (tpl < 0 || tpl > uint.MaxValue || tpl != Math.Floor(tpl)) { why = "template 이 없거나 잘못됨"; return null; }
            int n = (int)MiniJson.Num(o, "angles", 0);
            if (n < 1 || n > 72) { why = "angles 는 1..72"; return null; }
            double height = MiniJson.Num(o, "height", 0);
            if (!(height >= 0.5 && height <= 12.0)) { why = "height 는 0.5..12 (단위)"; return null; }
            string pattern = MiniJson.Str(o, "pattern", "");
            if (pattern.Length == 0) { why = "pattern 이 없습니다"; return null; }

            Bitmap[] frames = new Bitmap[n];
            try
            {
                int w = 0, h = 0;
                for (int i = 0; i < n; ++i)
                {
                    string rel;
                    try { rel = string.Format(CultureInfo.InvariantCulture, pattern, (int)Math.Round(i * 360.0 / n)); }
                    catch (FormatException) { why = "pattern 형식이 잘못됨: " + pattern; return Fail(frames); }
                    string path = Path.GetFullPath(Path.Combine(dir, rel));
                    if (!File.Exists(path)) { why = "파일이 없습니다: " + rel; return Fail(frames); }
                    frames[i] = ReadArgb(path, out why);
                    if (frames[i] == null) return Fail(frames);
                    if (i == 0) { w = frames[0].Width; h = frames[0].Height; }
                    else if (frames[i].Width != w || frames[i].Height != h) { why = "이미지 크기가 서로 다릅니다: " + rel; return Fail(frames); }
                }
                int x0 = w, y0 = h, x1 = -1, y1 = -1;
                foreach (Bitmap b in frames) Bounds(b, ref x0, ref y0, ref x1, ref y1);
                if (x1 < 0) { why = "투명하지 않은 픽셀이 없습니다"; return Fail(frames); }
                AssetSet s = new AssetSet();
                s.Frames = frames;
                int contentH = y1 - y0 + 1;
                s.Scale = (float)height / contentH;
                s.Cx = w / 2f; s.Foot = y1 + 1;
                float half = Math.Max(s.Cx - x0, x1 + 1 - s.Cx) * s.Scale;
                s.Look = new Look(half, (float)height);
                key = tree ? TreeKey : Key(kind == "monster", (uint)tpl);
                frames = null;
                return s;
            }
            finally { if (frames != null) for (int i = 0; i < frames.Length; ++i) if (frames[i] != null) frames[i].Dispose(); }
        }

        private static AssetSet Fail(Bitmap[] frames) { return null; }

        /// <summary>파일을 읽어 32비트 ARGB 비트맵으로(파일 잠금 없이) 만든다. GDI+ 가 못 읽는 형식(WebP 등)은 오류.</summary>
        private static Bitmap ReadArgb(string path, out string why)
        {
            why = null;
            try
            {
                using (Image src = Image.FromFile(path))
                {
                    if (src.Width > 4096 || src.Height > 4096) { why = "이미지가 너무 큽니다(4096 초과): " + Path.GetFileName(path); return null; }
                    Bitmap dst = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppArgb);
                    using (Graphics g = Graphics.FromImage(dst))
                    {
                        g.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                        g.DrawImage(src, 0, 0, src.Width, src.Height);
                    }
                    return dst;
                }
            }
            catch (Exception) { why = "이미지를 읽을 수 없습니다(PNG/JPG/GIF/BMP 만 가능, WebP 불가): " + Path.GetFileName(path); return null; }
        }

        /// <summary>알파가 8을 넘는 픽셀의 범위를 (x0,y0,x1,y1) 에 넓혀 담는다.</summary>
        private static void Bounds(Bitmap b, ref int x0, ref int y0, ref int x1, ref int y1)
        {
            int w = b.Width, h = b.Height;
            BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                int[] row = new int[w];
                for (int y = 0; y < h; ++y)
                {
                    Marshal.Copy(new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), row, 0, w);
                    for (int x = 0; x < w; ++x)
                    {
                        if (((row[x] >> 24) & 255) <= 8) continue;
                        if (x < x0) x0 = x; if (x > x1) x1 = x;
                        if (y < y0) y0 = y; if (y > y1) y1 = y;
                    }
                }
            }
            finally { b.UnlockBits(d); }
        }
    }
}
