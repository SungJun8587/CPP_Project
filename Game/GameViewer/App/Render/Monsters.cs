//***************************************************************************
// Monsters.cs : 몬스터 10종(+그 밖의 종류용 기본형)의 그림. Creatures.cs 의 영웅과 같은 규칙(단위 좌표, 발 밑 (0,0), 프레임 0~7 걷기, 8 대기).
//***************************************************************************
using System;
using System.Drawing;
using System.Drawing.Drawing2D;

namespace GameViewer.App.Render
{
    internal static class Monsters
    {
        private static readonly Color Ol = Color.FromArgb(215, 22, 18, 28);
        private static readonly Color Bone = Color.FromArgb(238, 232, 212), BoneD = Color.FromArgb(150, 140, 118);
        private static readonly Color Steel = Color.FromArgb(206, 212, 226), SteelD = Color.FromArgb(96, 104, 126);
        private static readonly Color Wood = Color.FromArgb(142, 98, 56), WoodD = Color.FromArgb(72, 46, 26);

        public static void Paint(Graphics g, uint tpl, int frame)
        {
            float ph = frame < CreatureRenderer.FrameIdle ? frame / 8f * 6.2832f : 0f;
            float sn = (float)Math.Sin(ph), cs = frame < CreatureRenderer.FrameIdle ? (float)Math.Cos(ph) : 1f;
            bool walk = frame < CreatureRenderer.FrameIdle;
            switch (tpl)
            {
                case 1: Slime(g, sn, Color.FromArgb(150, 236, 130), Color.FromArgb(30, 130, 60), 1.0f, false); break;
                case 2: Goblin(g, sn, cs, walk); break;
                case 3: Wolf(g, sn, cs, Color.FromArgb(190, 190, 202), Color.FromArgb(78, 78, 92), 1.0f, false); break;
                case 4: Bat(g, sn); break;
                case 5: Skeleton(g, sn, cs, walk); break;
                case 6: Spider(g, sn, cs); break;
                case 7: Orc(g, sn, cs, walk); break;
                case 8: Wolf(g, sn, cs, Color.FromArgb(120, 120, 142), Color.FromArgb(30, 30, 44), 1.55f, true); break;
                case 9: Ogre(g, sn, cs, walk); break;
                case 10: Slime(g, sn, Color.FromArgb(160, 150, 255), Color.FromArgb(52, 40, 150), 2.3f, true); break;
                default: Slime(g, sn, WorldRenderer.MonsterColor(tpl), Col.Dark(WorldRenderer.MonsterColor(tpl), 0.55f), 1.0f, false); break;
            }
        }

        private static void Legs2(Graphics g, float x, float hipY, float sn, float cs, float w, float len, Color c)
        {
            float lx = -x + sn * 0.2f * len, rx = x - sn * 0.2f * len;
            float ly = -Math.Max(0f, cs) * 0.12f * len, ry = -Math.Max(0f, -cs) * 0.12f * len;
            P.Limb(g, -x, hipY, lx, ly - 0.05f, w, c, Ol);
            P.Limb(g, x, hipY, rx, ry - 0.05f, w, c, Ol);
            P.Ball(g, lx, ly - 0.02f, w * 0.75f, w * 0.4f, Col.Light(c, 0.2f), Col.Dark(c, 0.3f), Ol, 0.04f);
            P.Ball(g, rx, ry - 0.02f, w * 0.75f, w * 0.4f, Col.Light(c, 0.2f), Col.Dark(c, 0.3f), Ol, 0.04f);
        }

        // ------------------------------------------------------------------ 슬라임 / 왕 슬라임
        private static void Slime(Graphics g, float sn, Color light, Color dark, float s, bool crown)
        {
            float sq = sn * 0.07f;
            float w = 0.95f * s * (1f + sq), h = 0.92f * s * (1f - sq * 1.4f);
            P.Fill(g, 0f, -0.03f * s, w * 1.0f, h * 0.2f, Col.Alpha(Col.Dark(dark, 0.4f), 200));
            P.Ball(g, 0f, -h * 0.95f, w, h, Col.Alpha(light, 240), Col.Alpha(dark, 235), Col.Alpha(Col.Dark(dark, 0.5f), 220), 0.05f * s);
            P.Ball(g, w * 0.06f, -h * 0.78f, w * 0.52f, h * 0.45f, Col.Alpha(Col.Light(light, 0.2f), 110), Col.Alpha(light, 20), Color.Transparent, 0f);
            P.Fill(g, -w * 0.35f, -h * 1.5f, w * 0.26f, h * 0.14f, Color.FromArgb(170, 255, 255, 255));
            P.Fill(g, w * 0.3f, -h * 0.4f, 0.07f * s, 0.07f * s, Color.FromArgb(120, 255, 255, 255));
            P.Fill(g, -w * 0.5f, -h * 0.5f, 0.05f * s, 0.05f * s, Color.FromArgb(110, 255, 255, 255));
            P.Eye(g, -w * 0.33f, -h * 1.02f, 0.17f * s, 0f, Color.FromArgb(24, 30, 30));
            P.Eye(g, w * 0.33f, -h * 1.02f, 0.17f * s, 0f, Color.FromArgb(24, 30, 30));
            using (Pen p = new Pen(Color.FromArgb(120, 20, 40, 30), 0.05f * s) { StartCap = LineCap.Round, EndCap = LineCap.Round })
                g.DrawArc(p, -w * 0.18f, -h * 0.85f, w * 0.36f, h * 0.22f, 10, 160);
            if (crown)
            {
                float cy = -h * 1.88f, cw = 0.62f * s;
                P.Poly(g, new PointF[] { new PointF(-cw, cy + 0.34f * s), new PointF(-cw, cy - 0.1f * s), new PointF(-cw * 0.5f, cy + 0.12f * s), new PointF(0f, cy - 0.28f * s), new PointF(cw * 0.5f, cy + 0.12f * s), new PointF(cw, cy - 0.1f * s), new PointF(cw, cy + 0.34f * s) },
                    Color.FromArgb(255, 226, 110), Color.FromArgb(190, 130, 40), Ol, 0.05f);
                P.Ball(g, 0f, cy + 0.12f * s, 0.07f * s, 0.07f * s, Color.FromArgb(255, 120, 120), Color.FromArgb(170, 30, 50), Ol, 0.02f);
            }
        }

        // ------------------------------------------------------------------ 고블린
        private static void Goblin(Graphics g, float sn, float cs, bool walk)
        {
            Color sk = Color.FromArgb(132, 190, 84), skD = Color.FromArgb(58, 108, 44);
            float bob = walk ? -Math.Abs(cs) * 0.07f : 0f;
            Legs2(g, 0.2f, -0.7f + bob, sn, cs, 0.26f, 0.8f, skD);
            P.Round(g, -0.42f, -1.32f + bob, 0.84f, 0.72f, 0.24f, sk, skD, Ol, 0.05f);
            P.Poly(g, new PointF[] { new PointF(-0.42f, -0.78f + bob), new PointF(0.42f, -0.78f + bob), new PointF(0.3f, -0.4f), new PointF(0f, -0.3f), new PointF(-0.3f, -0.4f) }, Wood, WoodD, Ol, 0.04f);
            P.Limb(g, -0.4f, -1.22f + bob, -0.62f - sn * 0.05f, -0.7f + bob, 0.22f, sk, Ol);
            float cx = 0.66f + sn * 0.04f;
            P.Limb(g, 0.4f, -1.22f + bob, cx, -0.78f + bob, 0.22f, sk, Ol);
            P.Limb(g, cx, -0.78f + bob, cx + 0.14f, -1.7f + bob, 0.1f, Wood, Ol);
            P.Ball(g, cx + 0.16f, -1.8f + bob, 0.2f, 0.26f, Col.Light(Wood, 0.2f), WoodD, Ol, 0.04f);
            P.Poly(g, new PointF[] { new PointF(-0.4f, -1.62f + bob), new PointF(-1.08f, -1.98f + bob), new PointF(-0.46f, -1.3f + bob) }, sk, skD, Ol, 0.04f);
            P.Poly(g, new PointF[] { new PointF(0.4f, -1.62f + bob), new PointF(1.08f, -1.98f + bob), new PointF(0.46f, -1.3f + bob) }, sk, skD, Ol, 0.04f);
            P.Ball(g, 0f, -1.6f + bob, 0.5f, 0.45f, Col.Light(sk, 0.2f), skD, Ol, 0.05f);
            P.Fill(g, -0.19f, -1.64f + bob, 0.1f, 0.09f, Color.FromArgb(255, 230, 60)); P.Fill(g, 0.19f, -1.64f + bob, 0.1f, 0.09f, Color.FromArgb(255, 230, 60));
            P.Fill(g, -0.19f, -1.64f + bob, 0.03f, 0.08f, Color.Black); P.Fill(g, 0.19f, -1.64f + bob, 0.03f, 0.08f, Color.Black);
            P.Line(g, -0.3f, -1.78f + bob, -0.1f, -1.72f + bob, 0.05f, skD); P.Line(g, 0.3f, -1.78f + bob, 0.1f, -1.72f + bob, 0.05f, skD);
            P.Fill(g, 0f, -1.5f + bob, 0.07f, 0.09f, skD);
            P.Poly(g, new PointF[] { new PointF(-0.2f, -1.38f + bob), new PointF(0.2f, -1.38f + bob), new PointF(0.12f, -1.28f + bob), new PointF(-0.12f, -1.28f + bob) }, Color.FromArgb(60, 20, 20), Color.FromArgb(120, 30, 30), Color.Transparent, 0f);
            P.Poly(g, new PointF[] { new PointF(-0.15f, -1.38f + bob), new PointF(-0.09f, -1.38f + bob), new PointF(-0.12f, -1.3f + bob) }, Color.White, Bone, Color.Transparent, 0f);
            P.Poly(g, new PointF[] { new PointF(0.09f, -1.38f + bob), new PointF(0.15f, -1.38f + bob), new PointF(0.12f, -1.3f + bob) }, Color.White, Bone, Color.Transparent, 0f);
        }

        // ------------------------------------------------------------------ 늑대 / 늑대 우두머리 (오른쪽을 본다)
        private static void Wolf(Graphics g, float sn, float cs, Color fur, Color furD, float s, bool alpha)
        {
            float bob = -Math.Abs(cs) * 0.05f * s;
            // 꼬리
            using (GraphicsPath tail = new GraphicsPath())
            {
                tail.AddBezier(-0.85f * s, -1.1f * s + bob, -1.4f * s, -1.1f * s, -1.5f * s, -1.5f * s + sn * 0.1f, -1.75f * s, -1.35f * s + sn * 0.1f);
                using (Pen o = new Pen(Ol, 0.34f * s) { StartCap = LineCap.Round, EndCap = LineCap.Round }) g.DrawPath(o, tail);
                using (Pen p = new Pen(Col.Mix(fur, furD, 0.3f), 0.28f * s) { StartCap = LineCap.Round, EndCap = LineCap.Round }) g.DrawPath(p, tail);
            }
            // 뒷다리 (뒤쪽 한 쌍은 어둡게)
            float[] lxs = { -0.6f, -0.35f, 0.5f, 0.75f };
            float[] off = { 1f, -1f, -1f, 1f };
            for (int i = 0; i < 4; ++i)
            {
                float fx = lxs[i] * s + sn * off[i] * 0.3f * s;
                float lift = Math.Max(0f, (float)Math.Cos(Math.Asin(Math.Max(-1f, Math.Min(1f, sn))) * off[i])) * 0.0f;
                float fy = -Math.Max(0f, cs * off[i]) * 0.14f * s;
                Color lc = i % 2 == 0 ? furD : Col.Mix(fur, furD, 0.35f);
                P.Limb(g, lxs[i] * s, -0.72f * s + bob, fx, fy - 0.05f, 0.24f * s, lc, Ol);
                P.Ball(g, fx + 0.04f * s, fy - 0.02f, 0.17f * s, 0.09f * s, Col.Light(lc, 0.2f), Col.Dark(lc, 0.3f), Ol, 0.03f);
            }
            // 몸통 + 배 + 털 결
            P.Ball(g, 0f, -1.0f * s + bob, 1.0f * s, 0.52f * s, fur, furD, Ol, 0.05f);
            P.Ball(g, 0.0f, -0.82f * s + bob, 0.7f * s, 0.26f * s, Col.Alpha(Col.Light(fur, 0.5f), 180), Col.Alpha(fur, 0), Color.Transparent, 0f);
            if (alpha)
                for (int i = 0; i < 5; ++i)
                    P.Poly(g, new PointF[] { new PointF((0.55f - i * 0.32f) * s, -1.46f * s + bob), new PointF((0.4f - i * 0.32f) * s, -1.86f * s + bob), new PointF((0.25f - i * 0.32f) * s, -1.44f * s + bob) }, Col.Light(fur, 0.55f), fur, Ol, 0.03f);
            // 머리
            float hx = 0.95f * s, hy = -1.3f * s + bob;
            P.Poly(g, new PointF[] { new PointF(hx + 0.15f * s, hy - 0.15f * s), new PointF(hx + 0.75f * s, hy + 0.0f * s), new PointF(hx + 0.72f * s, hy + 0.17f * s), new PointF(hx + 0.1f * s, hy + 0.28f * s) }, Col.Light(fur, 0.1f), furD, Ol, 0.04f);
            P.Ball(g, hx, hy, 0.43f * s, 0.36f * s, Col.Light(fur, 0.15f), furD, Ol, 0.05f);
            P.Fill(g, hx + 0.77f * s, hy - 0.01f * s, 0.07f * s, 0.06f * s, Color.FromArgb(20, 20, 24));
            P.Poly(g, new PointF[] { new PointF(hx - 0.2f * s, hy - 0.25f * s), new PointF(hx - 0.1f * s, hy - 0.62f * s), new PointF(hx + 0.12f * s, hy - 0.3f * s) }, fur, furD, Ol, 0.04f);
            P.Poly(g, new PointF[] { new PointF(hx + 0.05f * s, hy - 0.3f * s), new PointF(hx + 0.2f * s, hy - 0.64f * s), new PointF(hx + 0.32f * s, hy - 0.26f * s) }, fur, furD, Ol, 0.04f);
            Color eye = alpha ? Color.FromArgb(255, 70, 50) : Color.FromArgb(255, 220, 70);
            if (alpha) P.Glow(g, hx + 0.2f * s, hy - 0.08f * s, 0.4f * s, Color.FromArgb(255, 60, 40), 160);
            P.Fill(g, hx + 0.2f * s, hy - 0.08f * s, 0.075f * s, 0.06f * s, eye);
            P.Poly(g, new PointF[] { new PointF(hx + 0.4f * s, hy + 0.2f * s), new PointF(hx + 0.62f * s, hy + 0.18f * s), new PointF(hx + 0.52f * s, hy + 0.3f * s) }, Color.White, Bone, Color.Transparent, 0f);
            if (alpha) P.Line(g, hx - 0.05f * s, hy - 0.28f * s, hx + 0.25f * s, hy + 0.12f * s, 0.04f * s, Color.FromArgb(180, 200, 70, 70));
        }

        // ------------------------------------------------------------------ 박쥐
        private static void Bat(Graphics g, float sn)
        {
            float cy = -1.3f + sn * 0.1f;
            float flap = sn * 0.85f;
            Color m = Color.FromArgb(120, 70, 150), mD = Color.FromArgb(44, 24, 76);
            for (int side = -1; side <= 1; side += 2)
            {
                float sx = side;
                PointF[] wing = {
                    new PointF(sx * 0.2f, cy - 0.1f), new PointF(sx * 0.8f, cy - 0.5f - flap * 0.8f), new PointF(sx * 1.6f, cy - 0.3f - flap * 1.0f),
                    new PointF(sx * 1.35f, cy + 0.0f - flap * 0.55f), new PointF(sx * 1.1f, cy + 0.12f - flap * 0.3f), new PointF(sx * 0.82f, cy + 0.0f - flap * 0.2f), new PointF(sx * 0.5f, cy + 0.2f) };
                P.Poly(g, wing, m, mD, Ol, 0.05f);
                P.Line(g, sx * 0.2f, cy - 0.1f, sx * 1.6f, cy - 0.3f - flap * 1.0f, 0.05f, Color.FromArgb(70, 40, 100));
                P.Line(g, sx * 0.3f, cy, sx * 1.35f, cy - flap * 0.55f, 0.04f, Color.FromArgb(70, 40, 100));
            }
            P.Ball(g, 0f, cy + 0.05f, 0.34f, 0.42f, Col.Light(m, 0.2f), mD, Ol, 0.05f);
            P.Poly(g, new PointF[] { new PointF(-0.26f, cy - 0.3f), new PointF(-0.34f, cy - 0.78f), new PointF(-0.06f, cy - 0.4f) }, m, mD, Ol, 0.04f);
            P.Poly(g, new PointF[] { new PointF(0.26f, cy - 0.3f), new PointF(0.34f, cy - 0.78f), new PointF(0.06f, cy - 0.4f) }, m, mD, Ol, 0.04f);
            P.Glow(g, -0.13f, cy - 0.12f, 0.3f, Color.FromArgb(255, 60, 60), 150); P.Glow(g, 0.13f, cy - 0.12f, 0.3f, Color.FromArgb(255, 60, 60), 150);
            P.Fill(g, -0.13f, cy - 0.12f, 0.07f, 0.06f, Color.FromArgb(255, 90, 80)); P.Fill(g, 0.13f, cy - 0.12f, 0.07f, 0.06f, Color.FromArgb(255, 90, 80));
            P.Poly(g, new PointF[] { new PointF(-0.1f, cy + 0.12f), new PointF(-0.04f, cy + 0.12f), new PointF(-0.07f, cy + 0.26f) }, Color.White, Bone, Color.Transparent, 0f);
            P.Poly(g, new PointF[] { new PointF(0.04f, cy + 0.12f), new PointF(0.1f, cy + 0.12f), new PointF(0.07f, cy + 0.26f) }, Color.White, Bone, Color.Transparent, 0f);
        }

        // ------------------------------------------------------------------ 해골 병사
        private static void Skeleton(Graphics g, float sn, float cs, bool walk)
        {
            float bob = walk ? -Math.Abs(cs) * 0.06f : 0f;
            float lx = -0.2f + sn * 0.17f, rx = 0.2f - sn * 0.17f;
            P.Limb(g, -0.18f, -0.95f + bob, lx, -0.08f - Math.Max(0f, cs) * 0.1f, 0.12f, Bone, Ol);
            P.Limb(g, 0.18f, -0.95f + bob, rx, -0.08f - Math.Max(0f, -cs) * 0.1f, 0.12f, Bone, Ol);
            P.Ball(g, lx + 0.03f, -0.05f - Math.Max(0f, cs) * 0.1f, 0.2f, 0.08f, Bone, BoneD, Ol, 0.03f);
            P.Ball(g, rx + 0.03f, -0.05f - Math.Max(0f, -cs) * 0.1f, 0.2f, 0.08f, Bone, BoneD, Ol, 0.03f);
            P.Ball(g, 0f, -1.0f + bob, 0.3f, 0.14f, Bone, BoneD, Ol, 0.04f);
            P.Line(g, 0f, -1.1f + bob, 0f, -1.75f + bob, 0.1f, Bone);
            for (int i = 0; i < 4; ++i)
            {
                float y = -1.2f - i * 0.15f + bob, w = 0.4f - i * 0.02f;
                using (Pen p = new Pen(Ol, 0.16f) { StartCap = LineCap.Round, EndCap = LineCap.Round }) g.DrawArc(p, -w, y - 0.1f, w * 2, 0.26f, 0, 180);
                using (Pen p = new Pen(Bone, 0.1f) { StartCap = LineCap.Round, EndCap = LineCap.Round }) g.DrawArc(p, -w, y - 0.1f, w * 2, 0.26f, 0, 180);
            }
            P.Limb(g, -0.4f, -1.7f + bob, -0.7f - sn * 0.05f, -1.05f + bob, 0.1f, Bone, Ol);
            P.Ball(g, -0.72f, -1.0f + bob, 0.32f, 0.32f, Color.FromArgb(170, 120, 74), Color.FromArgb(82, 52, 30), Ol, 0.05f);
            P.Ball(g, -0.72f, -1.0f + bob, 0.1f, 0.1f, Steel, SteelD, Ol, 0.03f);
            float hx = 0.72f + sn * 0.03f;
            P.Limb(g, 0.4f, -1.7f + bob, hx, -1.12f + bob, 0.1f, Bone, Ol);
            P.Poly(g, new PointF[] { new PointF(hx - 0.06f, -1.12f + bob), new PointF(hx + 0.06f, -1.12f + bob), new PointF(hx + 0.08f, -2.2f + bob), new PointF(hx, -2.34f + bob), new PointF(hx - 0.08f, -2.2f + bob) }, Color.FromArgb(190, 160, 130), Color.FromArgb(104, 80, 62), Ol, 0.03f);
            P.Line(g, hx - 0.24f, -1.22f + bob, hx + 0.24f, -1.22f + bob, 0.08f, WoodD);
            P.Ball(g, 0f, -2.05f + bob, 0.36f, 0.36f, Bone, BoneD, Ol, 0.05f);
            P.Fill(g, -0.14f, -2.08f + bob, 0.09f, 0.1f, Color.FromArgb(14, 10, 16)); P.Fill(g, 0.14f, -2.08f + bob, 0.09f, 0.1f, Color.FromArgb(14, 10, 16));
            P.Fill(g, -0.14f, -2.08f + bob, 0.03f, 0.03f, Color.FromArgb(255, 90, 70)); P.Fill(g, 0.14f, -2.08f + bob, 0.03f, 0.03f, Color.FromArgb(255, 90, 70));
            P.Fill(g, 0f, -1.96f + bob, 0.04f, 0.06f, Color.FromArgb(40, 30, 30));
            P.Round(g, -0.2f, -1.84f + bob, 0.4f, 0.13f, 0.04f, Bone, BoneD, Ol, 0.03f);
            for (int i = 1; i < 4; ++i) P.Line(g, -0.2f + i * 0.1f, -1.84f + bob, -0.2f + i * 0.1f, -1.71f + bob, 0.02f, Ol);
        }

        // ------------------------------------------------------------------ 독거미
        private static void Spider(Graphics g, float sn, float cs)
        {
            Color b = Color.FromArgb(112, 66, 158), bD = Color.FromArgb(26, 14, 44);
            float bob = -Math.Abs(cs) * 0.04f;
            for (int pass = 0; pass < 2; ++pass)
                for (int side = -1; side <= 1; side += 2)
                    for (int i = 0; i < 4; ++i)
                    {
                        if ((i < 2) != (pass == 0)) continue;
                        float ph = ((i % 2) * 3.1416f) + (side > 0 ? 3.1416f : 0f);
                        float wig = (float)Math.Sin(Math.Asin(Math.Max(-1f, Math.Min(1f, sn))) + ph) * 0.18f;
                        float rootY = -0.75f + bob, rootX = side * (0.3f + i * 0.04f);
                        float kneeX = side * (1.0f + i * 0.14f), kneeY = -1.55f + bob + i * 0.12f;
                        float footX = side * (1.55f + i * 0.2f) + wig * side, footY = -Math.Max(0f, (float)Math.Sin(ph + sn * 3f)) * 0.1f;
                        Color lc = pass == 0 ? bD : Col.Mix(b, bD, 0.5f);
                        P.Limb(g, rootX, rootY, kneeX, kneeY, 0.1f, lc, Ol);
                        P.Limb(g, kneeX, kneeY, footX, footY, 0.08f, lc, Ol);
                    }
            P.Ball(g, 0f, -0.82f + bob, 0.78f, 0.6f, b, bD, Ol, 0.05f);
            P.Poly(g, new PointF[] { new PointF(-0.2f, -1.1f + bob), new PointF(0.2f, -1.1f + bob), new PointF(0.04f, -0.82f + bob), new PointF(0.2f, -0.55f + bob), new PointF(-0.2f, -0.55f + bob), new PointF(-0.04f, -0.82f + bob) }, Color.FromArgb(240, 80, 70), Color.FromArgb(140, 20, 40), Color.Transparent, 0f);
            P.Ball(g, 0f, -0.52f + bob, 0.4f, 0.3f, Col.Light(b, 0.15f), bD, Ol, 0.05f);
            for (int i = 0; i < 3; ++i)
            {
                P.Fill(g, -0.15f + i * 0.0f - 0.02f * i, -0.62f - i * 0.0f + bob, 0.045f, 0.045f, Color.FromArgb(255, 80, 70));
            }
            P.Glow(g, -0.14f, -0.6f + bob, 0.2f, Color.FromArgb(255, 60, 60), 120); P.Glow(g, 0.14f, -0.6f + bob, 0.2f, Color.FromArgb(255, 60, 60), 120);
            P.Fill(g, 0.15f, -0.6f + bob, 0.05f, 0.05f, Color.FromArgb(255, 80, 70)); P.Fill(g, -0.07f, -0.7f + bob, 0.035f, 0.035f, Color.FromArgb(255, 80, 70)); P.Fill(g, 0.07f, -0.7f + bob, 0.035f, 0.035f, Color.FromArgb(255, 80, 70));
            P.Poly(g, new PointF[] { new PointF(-0.16f, -0.36f + bob), new PointF(-0.07f, -0.36f + bob), new PointF(-0.1f, -0.16f + bob) }, Color.FromArgb(220, 230, 120), Color.FromArgb(120, 150, 40), Ol, 0.03f);
            P.Poly(g, new PointF[] { new PointF(0.07f, -0.36f + bob), new PointF(0.16f, -0.36f + bob), new PointF(0.1f, -0.16f + bob) }, Color.FromArgb(220, 230, 120), Color.FromArgb(120, 150, 40), Ol, 0.03f);
        }

        // ------------------------------------------------------------------ 오크 전사
        private static void Orc(Graphics g, float sn, float cs, bool walk)
        {
            Color sk = Color.FromArgb(112, 156, 84), skD = Color.FromArgb(44, 74, 40);
            Color arm = Color.FromArgb(104, 70, 44), armD = Color.FromArgb(54, 34, 22);
            float bob = walk ? -Math.Abs(cs) * 0.09f : 0f;
            Legs2(g, 0.34f, -1.1f + bob, sn, cs, 0.46f, 1.0f, Color.FromArgb(78, 64, 52));
            P.Round(g, -0.82f, -2.3f + bob, 1.64f, 1.35f, 0.5f, sk, skD, Ol, 0.06f);
            P.Poly(g, new PointF[] { new PointF(-0.8f, -1.2f + bob), new PointF(0.8f, -1.2f + bob), new PointF(0.62f, -0.82f + bob), new PointF(-0.62f, -0.82f + bob) }, Wood, WoodD, Ol, 0.04f);
            P.Poly(g, new PointF[] { new PointF(-0.5f, -2.2f + bob), new PointF(0.5f, -2.2f + bob), new PointF(0.3f, -1.4f + bob), new PointF(-0.3f, -1.4f + bob) }, arm, armD, Ol, 0.04f);
            P.Limb(g, -0.85f, -2.15f + bob, -1.1f - sn * 0.06f, -1.3f + bob, 0.46f, sk, Ol);
            float ax = 1.2f + sn * 0.05f;
            P.Limb(g, 0.85f, -2.15f + bob, ax, -1.3f + bob, 0.46f, sk, Ol);
            // 도끼
            P.Limb(g, ax, -0.8f + bob, ax + 0.1f, -2.5f + bob, 0.12f, Wood, Ol);
            P.Poly(g, new PointF[] { new PointF(ax + 0.1f, -2.1f + bob), new PointF(ax + 0.75f, -2.5f + bob), new PointF(ax + 0.65f, -1.7f + bob) }, Steel, SteelD, Ol, 0.04f);
            P.Poly(g, new PointF[] { new PointF(ax + 0.1f, -2.1f + bob), new PointF(ax - 0.55f, -2.5f + bob), new PointF(ax - 0.45f, -1.7f + bob) }, Steel, SteelD, Ol, 0.04f);
            P.Ball(g, ax, -1.3f + bob, 0.2f, 0.2f, Col.Light(sk, 0.1f), skD, Ol, 0.04f);
            P.Ball(g, -0.95f, -2.25f + bob, 0.34f, 0.26f, arm, armD, Ol, 0.04f);
            P.Ball(g, 0.95f, -2.25f + bob, 0.34f, 0.26f, arm, armD, Ol, 0.04f);
            for (int i = -1; i <= 1; i += 2) P.Poly(g, new PointF[] { new PointF(i * 0.85f, -2.42f + bob), new PointF(i * 1.0f, -2.78f + bob), new PointF(i * 1.07f, -2.38f + bob) }, Steel, SteelD, Ol, 0.03f);
            // 머리
            P.Ball(g, 0f, -2.55f + bob, 0.46f, 0.42f, Col.Light(sk, 0.15f), skD, Ol, 0.05f);
            P.Poly(g, new PointF[] { new PointF(-0.07f, -2.95f + bob), new PointF(0.07f, -2.95f + bob), new PointF(0.05f, -2.6f + bob), new PointF(-0.05f, -2.6f + bob) }, Color.FromArgb(30, 26, 30), Color.FromArgb(10, 10, 12), Color.Transparent, 0f);
            P.Line(g, -0.3f, -2.7f + bob, -0.07f, -2.62f + bob, 0.08f, skD); P.Line(g, 0.3f, -2.7f + bob, 0.07f, -2.62f + bob, 0.08f, skD);
            P.Fill(g, -0.17f, -2.6f + bob, 0.07f, 0.06f, Color.FromArgb(255, 200, 60)); P.Fill(g, 0.17f, -2.6f + bob, 0.07f, 0.06f, Color.FromArgb(255, 200, 60));
            P.Poly(g, new PointF[] { new PointF(-0.3f, -2.36f + bob), new PointF(0.3f, -2.36f + bob), new PointF(0.2f, -2.18f + bob), new PointF(-0.2f, -2.18f + bob) }, Color.FromArgb(66, 100, 52), skD, Ol, 0.03f);
            P.Poly(g, new PointF[] { new PointF(-0.22f, -2.36f + bob), new PointF(-0.12f, -2.36f + bob), new PointF(-0.17f, -2.72f + bob + 0.2f) }, Color.White, Bone, Ol, 0.02f);
            P.Poly(g, new PointF[] { new PointF(0.12f, -2.36f + bob), new PointF(0.22f, -2.36f + bob), new PointF(0.17f, -2.72f + bob + 0.2f) }, Color.White, Bone, Ol, 0.02f);
        }

        // ------------------------------------------------------------------ 오우거
        private static void Ogre(Graphics g, float sn, float cs, bool walk)
        {
            Color sk = Color.FromArgb(196, 150, 104), skD = Color.FromArgb(104, 70, 46);
            float bob = walk ? -Math.Abs(cs) * 0.16f : 0f;
            float sw = walk ? sn : 0f;
            Legs2(g, 0.62f, -1.8f + bob, sn, cs, 0.8f, 1.7f, Color.FromArgb(150, 108, 72));
            // 배 + 가슴
            P.Ball(g, 0f, -2.45f + bob, 1.35f, 1.05f, sk, skD, Ol, 0.07f);
            P.Ball(g, -0.1f, -3.25f + bob, 1.2f, 0.78f, Col.Light(sk, 0.1f), skD, Ol, 0.07f);
            P.Fill(g, 0f, -2.2f + bob, 0.12f, 0.1f, skD);
            P.Poly(g, new PointF[] { new PointF(-1.1f, -1.9f + bob), new PointF(1.1f, -1.9f + bob), new PointF(0.9f, -1.0f + bob), new PointF(0.3f, -0.85f + bob), new PointF(-0.2f, -1.1f + bob), new PointF(-0.9f, -0.95f + bob) }, Color.FromArgb(138, 96, 58), Color.FromArgb(64, 40, 24), Ol, 0.06f);
            P.Line(g, -1.1f, -1.9f + bob, 1.1f, -1.9f + bob, 0.16f, Color.FromArgb(70, 44, 26));
            for (int i = 0; i < 5; ++i) P.Fill(g, -0.7f + i * 0.35f, -2.6f + (i % 2) * 0.35f + bob, 0.05f, 0.05f, Col.Alpha(skD, 160));
            // 왼팔
            P.Limb(g, -1.2f, -3.3f + bob, -1.7f - sw * 0.1f, -2.0f + bob, 0.78f, sk, Ol);
            P.Ball(g, -1.75f - sw * 0.1f, -1.9f + bob, 0.4f, 0.4f, Col.Light(sk, 0.1f), skD, Ol, 0.05f);
            // 몽둥이를 든 오른팔
            float cx = 1.75f + sw * 0.12f;
            P.Limb(g, 1.2f, -3.3f + bob, cx, -2.05f + bob, 0.78f, sk, Ol);
            P.Limb(g, cx - 0.1f, -1.4f + bob, cx + 0.45f, -4.0f + bob, 0.34f, Wood, Ol);
            P.Ball(g, cx + 0.5f, -4.25f + bob, 0.66f, 0.8f, Col.Light(Wood, 0.2f), WoodD, Ol, 0.06f);
            for (int i = 0; i < 6; ++i)
            {
                double a = i * 1.0472 + 0.3; float px = cx + 0.5f + (float)Math.Cos(a) * 0.5f, py = -4.25f + bob + (float)Math.Sin(a) * 0.62f;
                P.Poly(g, new PointF[] { new PointF(px - 0.07f, py - 0.07f), new PointF(px + (float)Math.Cos(a) * 0.3f, py + (float)Math.Sin(a) * 0.3f), new PointF(px + 0.07f, py + 0.07f) }, Steel, SteelD, Ol, 0.03f);
            }
            P.Ball(g, cx, -2.0f + bob, 0.42f, 0.42f, Col.Light(sk, 0.1f), skD, Ol, 0.05f);
            // 어깨
            P.Ball(g, -1.25f, -3.5f + bob, 0.55f, 0.42f, Col.Light(sk, 0.1f), skD, Ol, 0.06f);
            P.Ball(g, 1.25f, -3.5f + bob, 0.55f, 0.42f, Col.Light(sk, 0.1f), skD, Ol, 0.06f);
            // 머리: 작은 머리에 큰 턱
            P.Ball(g, 0f, -4.0f + bob, 0.78f, 0.68f, Col.Light(sk, 0.15f), skD, Ol, 0.06f);
            P.Ball(g, 0f, -3.78f + bob, 0.7f, 0.4f, sk, skD, Color.Transparent, 0f);
            P.Poly(g, new PointF[] { new PointF(-0.7f, -4.18f + bob), new PointF(0.7f, -4.18f + bob), new PointF(0.55f, -4.0f + bob), new PointF(-0.55f, -4.0f + bob) }, skD, Col.Dark(skD, 0.3f), Color.Transparent, 0f);
            P.Glow(g, -0.28f, -4.0f + bob, 0.4f, Color.FromArgb(255, 200, 60), 120); P.Glow(g, 0.28f, -4.0f + bob, 0.4f, Color.FromArgb(255, 200, 60), 120);
            P.Fill(g, -0.28f, -3.98f + bob, 0.1f, 0.08f, Color.FromArgb(255, 220, 70)); P.Fill(g, 0.28f, -3.98f + bob, 0.1f, 0.08f, Color.FromArgb(255, 220, 70));
            P.Fill(g, -0.28f, -3.98f + bob, 0.035f, 0.07f, Color.Black); P.Fill(g, 0.28f, -3.98f + bob, 0.035f, 0.07f, Color.Black);
            P.Round(g, -0.4f, -3.55f + bob, 0.8f, 0.2f, 0.08f, Color.FromArgb(70, 30, 30), Color.FromArgb(30, 10, 14), Ol, 0.03f);
            P.Poly(g, new PointF[] { new PointF(-0.38f, -3.55f + bob), new PointF(-0.22f, -3.55f + bob), new PointF(-0.3f, -3.98f + bob + 0.14f) }, Color.White, Bone, Ol, 0.03f);
            P.Poly(g, new PointF[] { new PointF(0.22f, -3.55f + bob), new PointF(0.38f, -3.55f + bob), new PointF(0.3f, -3.98f + bob + 0.14f) }, Color.White, Bone, Ol, 0.03f);
            P.Line(g, -0.05f, -4.55f + bob, 0.1f, -4.72f + bob, 0.06f, Color.FromArgb(60, 40, 30)); P.Line(g, 0.12f, -4.5f + bob, 0.3f, -4.62f + bob, 0.06f, Color.FromArgb(60, 40, 30));
            P.Line(g, 0.25f, -4.3f + bob, 0.45f, -3.9f + bob, 0.05f, Color.FromArgb(150, 80, 70));
        }
    }
}
