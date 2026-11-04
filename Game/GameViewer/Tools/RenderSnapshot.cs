//***************************************************************************
// RenderSnapshot : 서버/창 없이 데모 전투를 시뮬레이션하며 장면을 PNG 로 저장한다. (렌더러 회귀 확인용)
//   RenderSnapshot.exe <GameData폴더> <출력폴더> [초...]     예) RenderSnapshot.exe GameData out 4 9 15
//   RenderSnapshot.exe --sheet <출력폴더> [배율]               영웅/몬스터 그림 모음(걷기/공격 프레임 포함) 한 장
//   RenderSnapshot.exe --selftest <GameData폴더>               렌더러 점검: 모든 그림이 그려지는지, 확대 범위 전체/보스전 장면이 예외 없이 그려지는지
//   RenderSnapshot.exe --bench <GameData폴더> [프레임수]       데모 장면을 그리는 평균 시간(ms) 측정
// 환경변수: SNAP_BOSS=1 영웅이 보스를 쫓음, SNAP_CAM=x,y 카메라 고정, SNAP_ASSETS=폴더 이미지 에셋 폴더
// 시간은 가상 시계(16ms 단위)로 진행하므로 실제로 기다리지 않는다.
//***************************************************************************
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using GameViewer.App.Render;
using GameViewer.Core;
using GameViewer.Core.Data;
using GameViewer.Core.Logic;
using GameViewer.Core.Protocol;
using GameViewer.Core.World;

public static class RenderSnapshot
{
    /// <summary>그림 모음: 각 종류를 걷기 4프레임 + 대기 + 공격(영웅)으로 늘어놓는다.</summary>
    private static int Sheet(string outDir, double scale)
    {
        Directory.CreateDirectory(outDir);
        using (SpriteCache cache = new SpriteCache(2000))
        {
            CreatureRenderer cr = new CreatureRenderer(cache);
            int cols = 8;
            int cellW = (int)(scale * 7.2), cellH = (int)(scale * 5.6);
            uint[] heroes = { 1, 2, 3 };
            uint[] mons = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
            int rows = heroes.Length + mons.Length;
            ViewState v = new ViewState(); v.Scale = scale;
            using (Bitmap bmp = new Bitmap(cellW * cols, cellH * rows, PixelFormat.Format32bppArgb))
            using (Graphics g = Graphics.FromImage(bmp))
            {
                g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                g.Clear(Color.FromArgb(74, 118, 56));
                int row = 0;
                for (int pass = 0; pass < 2; ++pass)
                {
                    uint[] list = pass == 0 ? heroes : mons;
                    foreach (uint tpl in list)
                    {
                        for (int c = 0; c < cols; ++c)
                        {
                            Entity e = new Entity();
                            e.Type = (byte)(pass == 0 ? EntityType.Player : EntityType.Monster); e.TemplateId = tpl; e.Id = (ulong)(c + 1);
                            long now = 100000;
                            // c: 0..3 걷기, 4 대기, 5..7 공격(영웅) / 걷기(몬스터) 
                            if (c <= 3 || (pass == 1 && c >= 5))
                            {
                                e.X = 0; e.Y = 0; e.DestX = 100; e.DestY = 0; e.Speed = 4f; e.Moving = true; e.MoveStartMs = now - 1;
                                float stride = pass == 0 ? 0.4f : 0.34f;
                                int fr = (c <= 3 ? c * 2 : (c - 4) * 2 + 1);
                                e.MoveStartMs = now - (long)((fr * stride + 0.01f) / 4f * 1000f);
                            }
                            else if (pass == 0 && c >= 5) { e.AttackAnimMs = now - (c - 5) * 90 - 20; e.X = 0; e.Y = 0; }
                            e.FaceX = (c % 2 == 0 || c >= 4) ? 1 : -1;
                            if (c == 4) e.FaceX = 1;
                            float sx = c * cellW + cellW / 2f, sy = row * cellH + cellH - (float)scale * 0.6f;
                            g.FillEllipse(new SolidBrush(Color.FromArgb(70, 0, 0, 0)), sx - (float)scale * 1.2f, sy - (float)scale * 0.25f, (float)scale * 2.4f, (float)scale * 0.55f);
                            cr.Draw(g, e, v, now, sx, sy, false);
                        }
                        ++row;
                    }
                }
                string path = Path.Combine(outDir, "sheet.png");
                bmp.Save(path, ImageFormat.Png);
                Console.WriteLine("saved " + path + " cache=" + cache.Count);
            }
        }
        return 0;
    }

    private static int _fail;
    private static void Check(bool ok, string what) { if (!ok) { ++_fail; Console.WriteLine("  FAIL: " + what); } }

    /// <summary>화면에 그려진 픽셀이 배경과 얼마나 다른지 센다.</summary>
    private static int CountDiff(Bitmap bmp, Color bg)
    {
        int n = 0;
        for (int y = 0; y < bmp.Height; y += 2)
            for (int x = 0; x < bmp.Width; x += 2)
            {
                Color c = bmp.GetPixel(x, y);
                if (Math.Abs(c.R - bg.R) + Math.Abs(c.G - bg.G) + Math.Abs(c.B - bg.B) > 24) ++n;
            }
        return n;
    }

    private static int SelfTest(string dataDir)
    {
        // 1) 영웅 3종 + 몬스터 10종 + 기본형: 걷기/대기/공격 프레임이 모두 그려지고 시체/번쩍임도 그려진다
        using (SpriteCache cache = new SpriteCache(2000))
        {
            CreatureRenderer cr = new CreatureRenderer(cache);
            Color bg = Color.FromArgb(74, 118, 56);
            ViewState v = new ViewState(); v.Scale = 24;
            for (int pass = 0; pass < 2; ++pass)
            {
                uint[] tpls = pass == 0 ? new uint[] { 1, 2, 3, 9 } : new uint[] { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
                foreach (uint tpl in tpls)
                    for (int frame = 0; frame < 3; ++frame)
                        foreach (int state in new int[] { 0, 1, 2 })        // 0 일반, 1 피격, 2 사망
                        {
                            using (Bitmap bmp = new Bitmap(320, 240, PixelFormat.Format32bppArgb))
                            using (Graphics g = Graphics.FromImage(bmp))
                            {
                                g.Clear(bg);
                                Entity e = new Entity();
                                e.Type = (byte)(pass == 0 ? EntityType.Player : EntityType.Monster); e.TemplateId = tpl; e.Id = 5; e.FaceX = frame == 1 ? -1 : 1;
                                if (frame == 2 && pass == 0) e.AttackAnimMs = 100000 - 120;
                                if (frame == 0 && state != 2) { e.DestX = 99; e.Speed = 4; e.Moving = true; e.MoveStartMs = 100000 - 400; }
                                e.Dead = state == 2;
                                cr.Draw(g, e, v, 100000, 160, 190, state == 1);
                                int px = CountDiff(bmp, bg);
                                Check(px > (state == 2 ? 20 : 120), (pass == 0 ? "hero " : "monster ") + tpl + " frame " + frame + " state " + state + " draws pixels (" + px + ")");
                            }
                        }
            }
            Check(cache.Misses > 0 && cache.Count < 2000, "sprite cache used");
        }

        // 2) 보스전 장면: 확대 5~60배, 화면 크기 변화에서 예외 없이 그려지고 보이는 내용이 있다
        ViewerConfig cfg = new ViewerConfig(); cfg.DataDir = dataDir; cfg.HeroId = 1;
        GameData data = GameData.Load(cfg.DataDir, cfg.ZoneId);
        ViewerSession s = new ViewerSession(cfg, data);
        DemoDirector demo = new DemoDirector(s);
        AutoPilot pilot = new AutoPilot(s);
        long now = 100000; demo.Start(now); pilot.SetAuto(true);
        int bossShots = 0, telegraphShots = 0, bannerShots = 0;
        using (SceneRenderer r = new SceneRenderer())
        {
            double[] scales = { 20, 5, 60, 9, 33, 20 };
            int[] widths = { 1280, 640, 1920, 800, 1280, 1280 };
            for (int step = 0; step < 90; ++step)
            {
                for (int i = 0; i < 62; ++i) { now += 16; s.Update(now); demo.Update(now); pilot.Update(now);
                    foreach (Entity be in s.World.Entities.Values) if (be.IsBoss && !be.Dead && pilot.TargetId != be.Id) pilot.ChaseTarget(be.Id); }
                ViewState v = new ViewState();
                v.Width = widths[step % widths.Length]; v.Height = v.Width * 5 / 8; v.Scale = scales[step % scales.Length];
                float sx, sy; if (s.SelfPos(now, out sx, out sy)) { v.CamX = sx; v.CamY = sy; }
                v.SelectedId = pilot.TargetId; v.HasMouse = true; v.HoverId = v.SelectedId;
                try
                {
                    using (Bitmap bmp = new Bitmap(v.Width, v.Height, PixelFormat.Format32bppArgb))
                    using (Graphics g = Graphics.FromImage(bmp))
                    {
                        r.Draw(g, s, pilot, v, now);
                        Check(CountDiff(bmp, Color.FromArgb(18, 40, 24)) > 2000, "scene " + step + " has content");
                    }
                }
                catch (Exception e) { Check(false, "scene " + step + " threw " + e); }
                foreach (Entity be in s.World.Entities.Values) if (be.IsBoss && !be.Dead) ++bossShots;
                foreach (ViewEffect f in s.World.Effects) { if (f.Kind == EffectKind.Telegraph) ++telegraphShots; if (f.Kind == EffectKind.Banner) ++bannerShots; }
            }
            Check(bossShots > 0, "boss was in the scenes");
            Check(telegraphShots > 0, "telegraph effects were drawn at least once");
            // 같은 확대/화면 크기로 움직이는 동안 바닥을 계속 다시 그리지 않는다
            int before = r.GroundRebuilds;
            ViewState vv = new ViewState(); vv.Width = 1280; vv.Height = 800; vv.Scale = 20;
            using (Bitmap bmp = new Bitmap(1280, 800, PixelFormat.Format32bppArgb))
            using (Graphics g = Graphics.FromImage(bmp))
            {
                vv.CamX = 500; vv.CamY = 500; r.Draw(g, s, pilot, vv, now);
                int b0 = r.GroundRebuilds;
                for (int i = 0; i < 40; ++i) { vv.CamX += 0.2f; r.Draw(g, s, pilot, vv, now + i * 16); }
                Check(r.GroundRebuilds == b0, "ground is cached while the camera moves a little (" + (r.GroundRebuilds - b0) + " rebuilds)");
                vv.CamX += 40f; r.Draw(g, s, pilot, vv, now);
                Check(r.GroundRebuilds == b0 + 1, "ground rebuilds once after a long move");
            }
        }
        Console.WriteLine(_fail == 0 ? "selftest OK" : "selftest FAILED (" + _fail + ")");
        return _fail == 0 ? 0 : 1;
    }

    private static int Bench(string dataDir, int frames)
    {
        ViewerConfig cfg = new ViewerConfig(); cfg.DataDir = dataDir; cfg.HeroId = 1;
        GameData data = GameData.Load(cfg.DataDir, cfg.ZoneId);
        ViewerSession s = new ViewerSession(cfg, data);
        DemoDirector demo = new DemoDirector(s);
        AutoPilot pilot = new AutoPilot(s);
        long now = 100000; demo.Start(now); pilot.SetAuto(true);
        ViewState v = new ViewState(); v.Width = 1280; v.Height = 800; v.Scale = cfg.PixelsPerUnit;
        using (SceneRenderer r = new SceneRenderer())
        using (Bitmap bmp = new Bitmap(v.Width, v.Height, PixelFormat.Format32bppArgb))
        using (Graphics g = Graphics.FromImage(bmp))
        {
            // 먼저 20초 진행(캐시 데우기 제외)
            for (int i = 0; i < 1250; ++i) { now += 16; s.Update(now); demo.Update(now); pilot.Update(now); }
            r.Draw(g, s, pilot, v, now);
            System.Diagnostics.Stopwatch sw = new System.Diagnostics.Stopwatch();
            double worst = 0, total = 0;
            for (int i = 0; i < frames; ++i)
            {
                now += 16; s.Update(now); demo.Update(now); pilot.Update(now);
                float sx, sy; if (s.SelfPos(now, out sx, out sy)) { v.CamX = sx; v.CamY = sy; }
                v.SelectedId = pilot.TargetId;
                sw.Restart(); r.Draw(g, s, pilot, v, now); sw.Stop();
                double ms = sw.Elapsed.TotalMilliseconds; total += ms; if (ms > worst) worst = ms;
            }
            string[] names = { "ground", "border", "world", "vignette", "hud" };
            for (int i = 0; i < names.Length; ++i) Console.WriteLine("  " + names[i] + " " + (r.PartMs[i] / (frames + 1)).ToString("0.00") + "ms");
            Console.WriteLine("  ground rebuilds=" + r.GroundRebuilds);
            Console.WriteLine("frames=" + frames + " avg=" + (total / frames).ToString("0.00") + "ms worst=" + worst.ToString("0.0") + "ms entities=" + s.World.Entities.Count + " effects=" + s.World.Effects.Count);
        }
        return 0;
    }

    private static string AssetsDir()
    {
        string d = Environment.GetEnvironmentVariable("SNAP_ASSETS");
        return string.IsNullOrEmpty(d) ? Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Assets") : d;
    }

    private static void LoadAssets()
    {
        System.Collections.Generic.List<string> warn = new System.Collections.Generic.List<string>();
        int n = AssetSprites.Load(AssetsDir(), warn);
        foreach (string w in warn) Console.WriteLine("warn: assets: " + w);
        if (n > 0) Console.WriteLine("assets: " + n + " sprite set(s) from " + AssetsDir());
    }

    /// <summary>이미지 에셋 로더 점검: 임시 폴더에 합성 이미지를 만들어 방향 선택/그리기/잘못된 항목 거부/폴백을 확인한다.</summary>
    private static void AssetTests()
    {
        string dir = Path.Combine(Path.GetTempPath(), "gv_assets_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(dir, "t"));
        try
        {
            Color[] cols = { Color.Red, Color.Lime, Color.Blue, Color.Yellow, Color.Cyan, Color.Magenta, Color.White, Color.Orange };
            for (int i = 0; i < 8; ++i)
                using (Bitmap b = new Bitmap(64, 64, PixelFormat.Format32bppArgb))
                {
                    using (Graphics g = Graphics.FromImage(b)) { g.Clear(Color.Transparent); using (Brush br = new SolidBrush(cols[i])) g.FillRectangle(br, 16, 8, 32, 48); }
                    b.Save(Path.Combine(dir, "t", (i * 45).ToString("000") + ".png"), ImageFormat.Png);
                }
            using (Bitmap odd = new Bitmap(32, 32)) odd.Save(Path.Combine(dir, "t", "odd.png"), ImageFormat.Png);
            File.WriteAllText(Path.Combine(dir, "t", "junk.png"), "RIFF....WEBPVP8X");
            string man = "{ \"sprites\": [\n" +
                "{\"kind\":\"monster\",\"template\":99,\"pattern\":\"t/{0:000}.png\",\"angles\":8,\"height\":3},\n" +
                "{\"kind\":\"monster\",\"template\":99,\"pattern\":\"t/{0:000}.png\",\"angles\":8,\"height\":3},\n" +      // 중복
                "{\"kind\":\"hero\",\"template\":5,\"pattern\":\"t/{0:000}.png\",\"angles\":9,\"height\":3},\n" +          // 파일 없음(40도)
                "{\"kind\":\"hero\",\"template\":6,\"pattern\":\"t/{0:000}.png\",\"angles\":8,\"height\":99},\n" +         // 높이 범위
                "{\"kind\":\"hero\",\"template\":7,\"pattern\":\"t/{0:000}.png\",\"angles\":0,\"height\":3},\n" +          // 방향 수
                "{\"kind\":\"boss\",\"template\":7,\"pattern\":\"x\",\"angles\":1,\"height\":3},\n" +                      // kind
                "{\"kind\":\"hero\",\"template\":8,\"pattern\":\"t/junk.png\",\"angles\":1,\"height\":3},\n" +             // 못 읽는 이미지
                "{\"kind\":\"hero\",\"template\":9,\"pattern\":\"t/{1}.png\",\"angles\":1,\"height\":3},\n" +              // 형식 오류
                "{\"kind\":\"hero\",\"template\":3,\"pattern\":\"t/{0:000}.png\",\"angles\":1,\"height\":3}]}";
            File.WriteAllText(Path.Combine(dir, "sprites.json"), man);
            System.Collections.Generic.List<string> warn = new System.Collections.Generic.List<string>();
            int n = AssetSprites.Load(dir, warn);
            Check(n == 2, "asset: 정상 2개만 읽힘 (실제 " + n + ")");
            Check(warn.Count == 7, "asset: 잘못된 항목 7개가 각각 경고 (실제 " + warn.Count + ")");
            AssetSet a = AssetSprites.Find(true, 99);
            Check(a != null && AssetSprites.Find(false, 99) == null && AssetSprites.Find(true, 5) == null, "asset: 종류 구분/없는 종류는 null");
            if (a != null)
            {
                Check(a.IndexFor(0, 1) == 0 && a.IndexFor(1, 0) == 2 && a.IndexFor(0, -1) == 4 && a.IndexFor(-1, 0) == 6 && a.IndexFor(0, 0) == 0 && a.IndexFor(-0.01f, 1) == 0,
                    "asset: 방향 -> 번호 (아래 0, 오른쪽 2, 위 4, 왼쪽 6)");
                Check(Math.Abs(a.Look.H - 3f) < 1e-4f && a.Look.HalfW > 0.9f && a.Look.HalfW < 1.2f, "asset: Look 이 내용 크기에서 나옴 (HalfW=" + a.Look.HalfW + ")");
                Check(CreatureRenderer.LookOf(true, 99).H == 3f, "asset: LookOf 가 에셋 크기를 돌려줌");
                // 방향마다 다른 색이 찍히는지
                using (SpriteCache cache = new SpriteCache(100))
                {
                    CreatureRenderer cr = new CreatureRenderer(cache);
                    ViewState v = new ViewState(); v.Scale = 24;
                    float[][] dirs = { new float[] { 0, 1 }, new float[] { 1, 0 }, new float[] { 0, -1 }, new float[] { -1, 0 } };
                    int[] idx = { 0, 2, 4, 6 };
                    for (int k = 0; k < 4; ++k)
                    {
                        Entity e = new Entity(); e.Type = (byte)EntityType.Monster; e.TemplateId = 99; e.Id = 1; e.FaceX = dirs[k][0]; e.FaceY = dirs[k][1];
                        using (Bitmap bmp = new Bitmap(200, 200, PixelFormat.Format32bppArgb))
                        using (Graphics g = Graphics.FromImage(bmp))
                        {
                            g.Clear(Color.Black);
                            cr.Draw(g, e, v, 100000, 100, 150, false);
                            Color c = bmp.GetPixel(100, 150 - (int)(1.2f * 24));      // 몸 한가운데쯤
                            Color want = cols[idx[k]];
                            Check(Math.Abs(c.R - want.R) + Math.Abs(c.G - want.G) + Math.Abs(c.B - want.B) < 40, "asset: 방향 " + k + " 에서 기대한 이미지가 그려짐 (" + c + ")");
                            // 발 밑은 기준점 위, 아래로는 번지지 않음
                            Check(bmp.GetPixel(100, 152).ToArgb() == Color.Black.ToArgb(), "asset: 발 밑 아래로 그려지지 않음");
                            e.Dead = true; g.Clear(Color.Black); cr.Draw(g, e, v, 100000, 100, 150, false);
                            Check(CountDiff(bmp, Color.Black) > 50, "asset: 시체 그림이 그려짐");
                        }
                    }
                }
            }
            AssetSprites.Clear();
            Check(AssetSprites.Find(true, 99) == null && AssetSprites.Load(Path.Combine(dir, "none"), warn) == 0, "asset: 비우기 / 폴더 없으면 0");
        }
        finally { AssetSprites.Clear(); try { Directory.Delete(dir, true); } catch (Exception) { } }
    }

    public static int Main(string[] args)
    {
        if (args.Length >= 2 && args[0] == "--selftest") AssetTests();
        LoadAssets();
        if (args.Length >= 2 && args[0] == "--sheet") return Sheet(args[1], args.Length > 2 ? double.Parse(args[2]) : 28);
        if (args.Length >= 2 && args[0] == "--selftest") return SelfTest(args[1]);
        if (args.Length >= 2 && args[0] == "--bench") return Bench(args[1], args.Length > 2 ? int.Parse(args[2]) : 300);
        if (args.Length < 2) { Console.WriteLine("usage: RenderSnapshot <GameDataDir> <outDir> [seconds...]"); return 2; }
        Directory.CreateDirectory(args[1]);
        ViewerConfig cfg = new ViewerConfig(); cfg.DataDir = args[0]; cfg.HeroId = 1;
        GameData data = GameData.Load(cfg.DataDir, cfg.ZoneId);
        foreach (string w in data.Warnings) Console.WriteLine("warn: " + w);

        ViewerSession s = new ViewerSession(cfg, data);
        DemoDirector demo = new DemoDirector(s);
        AutoPilot pilot = new AutoPilot(s);
        long now = 100000;
        demo.Start(now);
        pilot.SetAuto(true);
        bool bossMode = Environment.GetEnvironmentVariable("SNAP_BOSS") == "1";      // 내 영웅도 보스를 추적

        double[] shots = new double[Math.Max(1, args.Length - 2)];
        if (args.Length > 2) for (int i = 2; i < args.Length; ++i) shots[i - 2] = double.Parse(args[i]); else shots[0] = 6;

        ViewState v = new ViewState();
        v.Width = 1280; v.Height = 800; v.Scale = cfg.PixelsPerUnit;
        using (SceneRenderer r = new SceneRenderer())
        {
            int next = 0;
            long start = now;
            long limit = start + (long)(shots[shots.Length - 1] * 1000) + 100;
            while (now <= limit && next < shots.Length)
            {
                now += 16;
                s.Update(now); demo.Update(now); pilot.Update(now);
                float sx, sy;
                if (bossMode)
                    foreach (Entity be in s.World.Entities.Values)
                        if (be.IsBoss && !be.Dead && pilot.TargetId != be.Id) pilot.ChaseTarget(be.Id);
                if (s.SelfPos(now, out sx, out sy)) { v.CamX = sx; v.CamY = sy; }
                string cam = Environment.GetEnvironmentVariable("SNAP_CAM");        // "x,y" 이면 카메라를 그 월드 좌표에 고정
                if (!string.IsNullOrEmpty(cam)) { string[] cp = cam.Split(','); v.CamX = float.Parse(cp[0], System.Globalization.CultureInfo.InvariantCulture); v.CamY = float.Parse(cp[1], System.Globalization.CultureInfo.InvariantCulture); }
                v.SelectedId = pilot.TargetId;
                if (now - start >= (long)(shots[next] * 1000))
                {
                    using (Bitmap bmp = new Bitmap(v.Width, v.Height, PixelFormat.Format32bppArgb))
                    using (Graphics g = Graphics.FromImage(bmp))
                    {
                        r.Draw(g, s, pilot, v, now);
                        string path = Path.Combine(args[1], "shot_" + shots[next].ToString("00.0") + "s.png");
                        bmp.Save(path, ImageFormat.Png);
                        Console.WriteLine("saved " + path + "  entities=" + s.World.Entities.Count + " effects=" + s.World.Effects.Count + " note=" + pilot.Note);
                    }
                    ++next;
                }
            }
        }
        return 0;
    }
}
