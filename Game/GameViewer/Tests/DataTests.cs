using System;
using System.Collections.Generic;
using System.IO;
using GameViewer.Core;
using GameViewer.Core.Data;

namespace GameViewer.Tests
{
    public static class DataTests
    {
        public static void MiniJsonBasics()
        {
            Dictionary<string, object> o = MiniJson.AsObject(MiniJson.Parse("﻿{ \"a\": 1.5, \"b\": [1, 2, {\"c\": \"x\\ny\\u00e9\"}], \"t\": true, \"n\": null, \"neg\": -3e2 }"));
            T.Near(1.5, MiniJson.Num(o, "a", 0), 1e-9, "number");
            T.Eq(3, MiniJson.AsArray(o["b"]).Count, "array count");
            T.Eq("x\né".Replace("é", "yé"), MiniJson.Str(MiniJson.AsObject(MiniJson.AsArray(o["b"])[2]), "c", ""), "string escapes");
            T.Check(MiniJson.Bool(o, "t", false), "bool");
            T.Check(o["n"] == null, "null");
            T.Near(-300, MiniJson.Num(o, "neg", 0), 1e-9, "exponent");
            T.Eq("한글", MiniJson.Str(MiniJson.AsObject(MiniJson.Parse("{\"k\":\"한글\"}")), "k", ""), "unicode passthrough");

            string[] bad = { "", "{", "{\"a\":}", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "\"abc", "tru", "{} x" };
            foreach (string b in bad)
            {
                bool threw = false;
                try { MiniJson.Parse(b); } catch (JsonException) { threw = true; }
                T.Check(threw, "rejects: " + b);
            }
        }

        public static void GameDataLoads()
        {
            GameData d = GameData.Load(Program.Dir("GameData"), 1);
            T.Eq(0, d.Warnings.Count, "no warnings: " + string.Join("; ", d.Warnings.ToArray()));
            T.Eq(3, d.Heroes.Count, "heroes");
            T.Eq("전사", d.HeroName(1), "hero name (Korean)");
            T.Eq(10, d.Monsters.Count, "monsters");
            T.Eq(7, d.Skills.Count, "skills");
            T.Check(d.Skill(2).IsArea && d.Skill(2).Radius == 4.0, "skill 2 area radius 4");
            T.Check(!d.Skill(1).IsArea, "skill 1 single");
            T.Eq(3, d.SkillsOf(1).Count, "hero 1 skills");
            T.Check(d.Status(1).IsDot && !d.Status(3).IsDot, "status kinds");
            T.Eq(3, d.Blocks.Count, "blocks for zone 1");
            T.Check(d.Blocks[1].IsCircle && d.Blocks[1].R == 5.0f, "circle block");
            T.Eq(11, d.Spawns.Count, "spawns of zone 1 only (fixture has 14 rows over zones 1 and 2)");
            GameData other = GameData.Load(Program.Dir("GameData"), 99);
            T.Eq(0, other.Blocks.Count, "other zone has no blocks");
            T.Eq("#77", d.SkillName(77), "unknown id fallback name");
            T.Eq(1u, d.ResolveHeroId(0), "hero 0 resolves to lowest id");

            GameData missing = GameData.Load("no_such_dir_xyz", 1);
            T.Check(missing.Warnings.Count > 0, "missing dir warns");
            T.Eq(0, missing.Heroes.Count, "missing dir empty");
        }

        public static void Config()
        {
            string err; bool exit;
            ViewerConfig c = ViewerConfig.FromArgs(new string[] { "--host", "10.0.0.5", "--port", "7100", "--hero", "2", "--auto", "--scale", "9.5" }, out err, out exit);
            T.Check(c != null && err == null, "valid args");
            T.Eq("10.0.0.5", c.Host, "host"); T.Eq(7100, c.Port, "port"); T.Eq(2u, c.HeroId, "hero"); T.Check(c.Auto, "auto"); T.Near(9.5, c.PixelsPerUnit, 1e-9, "scale");

            c = ViewerConfig.FromArgs(new string[] { "--port", "70000" }, out err, out exit);
            T.Check(c == null && err != null, "port out of range rejected");
            c = ViewerConfig.FromArgs(new string[] { "--bogus" }, out err, out exit);
            T.Check(c == null && err != null, "unknown option rejected");
            c = ViewerConfig.FromArgs(new string[] { "--port" }, out err, out exit);
            T.Check(c == null && err != null, "missing value rejected");

            // dump -> load 왕복 + 명령행 우선
            string tmp = Path.Combine(Path.GetTempPath(), "gv_cfg_test.json");
            ViewerConfig def = new ViewerConfig();
            File.WriteAllText(tmp, def.ToJson().Replace("\"Port\": 7000", "\"Port\": 7777").Replace("\"HeroId\": 1", "\"HeroId\": 3"));
            c = ViewerConfig.FromArgs(new string[] { "--config", tmp, "--hero", "2" }, out err, out exit);
            T.Check(c != null, "config file loads: " + err);
            T.Eq(7777, c.Port, "file value used"); T.Eq(2u, c.HeroId, "command line overrides file");
            File.Delete(tmp);

            File.WriteAllText(tmp, "{ \"Port\": 0 }");
            c = ViewerConfig.FromArgs(new string[] { "--config", tmp }, out err, out exit);
            T.Check(c == null, "bad port in file rejected");
            File.Delete(tmp);
        }
    }
}
