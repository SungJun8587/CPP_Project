//***************************************************************************
// GameData.cs : XlsxToJson 이 만든 JSON 표(Hero/Monster/Skill/Status/HeroSkill/Block/Spawn)를 읽어 뷰어용으로 보관.
// 서버와 같은 JSON 을 쓰므로 이름/사거리/반경/쿨다운이 서버 값과 항상 일치한다.
// 파일이 없거나 비어 있어도 동작한다(이름은 "#id"로 표시). 경고는 Warnings 에 모인다.
//***************************************************************************
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace GameViewer.Core.Data
{
    public sealed class HeroRow { public uint Id; public string Name; public int MaxHp; public int Attack, Defense; public double AttackRange; public uint AttackIntervalMs; public double Speed; }
    public sealed class MonsterRow { public uint Id; public string Name; public int MaxHp, Attack, Defense; public double AttackRange, Speed, AggroRange; public uint AttackIntervalMs; }
    public sealed class SkillRow { public uint Id; public string Name; public bool IsArea; public double Range, Radius, ProjectileSpeed; public uint CooldownMs, CastTimeMs, StatusId, MaxTargets, DamagePercent; public int FlatDamage; }
    public sealed class StatusRow { public uint Id; public string Name; public bool IsDot, TargetsSelf; public uint TickMs; public uint DamagePercent; public int AttackPercent, DefensePercent, SpeedPercent; public uint DurationMs; }
    public sealed class BlockRow { public bool IsCircle; public float X, Y, W, H, R; }
    public sealed class SpawnRow { public uint MonsterId, Count; public float X, Y, Radius; }
    public sealed class BossPhaseRow { public uint Phase, HpBelowPercent, EnterStatusId; }
    public sealed class BossRow
    {
        public uint Id, MonsterId, EnrageAfterMs, EnrageStatusId; public string Name;
        public readonly List<BossPhaseRow> Phases = new List<BossPhaseRow>();      // Phase 오름차순

        /// <summary>서버 규칙과 같다: hp*100 &lt;= HpBelowPercent*maxHp 를 만족하는 가장 높은 페이즈 번호(1부터).</summary>
        public int PhaseFor(int hp, int maxHp)
        {
            int phase = 1;
            if (maxHp <= 0) return phase;
            for (int i = 0; i < Phases.Count; ++i)
                if ((long)hp * 100 <= (long)Phases[i].HpBelowPercent * maxHp) phase = (int)Phases[i].Phase;
            return phase;
        }
    }

    public sealed class GameData
    {
        public readonly Dictionary<uint, HeroRow> Heroes = new Dictionary<uint, HeroRow>();
        public readonly Dictionary<uint, MonsterRow> Monsters = new Dictionary<uint, MonsterRow>();
        public readonly Dictionary<uint, SkillRow> Skills = new Dictionary<uint, SkillRow>();
        public readonly Dictionary<uint, StatusRow> Statuses = new Dictionary<uint, StatusRow>();
        public readonly Dictionary<uint, List<uint>> HeroSkills = new Dictionary<uint, List<uint>>();
        public readonly List<BlockRow> Blocks = new List<BlockRow>();      // 선택한 존의 것만
        public readonly List<SpawnRow> Spawns = new List<SpawnRow>();      // 선택한 존의 것만
        public readonly Dictionary<uint, BossRow> Bosses = new Dictionary<uint, BossRow>();       // 몬스터 ID -> 보스 (Boss/BossPhase 시트. 없으면 비어 있음)
        public readonly List<string> Warnings = new List<string>();

        // 서버 FieldZoneConfig 기본 영역과 같다(0..1000). 필요하면 뷰어 설정으로 덮어쓴다.
        public float WorldMinX = 0, WorldMinY = 0, WorldMaxX = 1000, WorldMaxY = 1000;

        public static GameData Load(string dir, uint zoneId)
        {
            GameData d = new GameData();
            if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir))
            {
                d.Warnings.Add("GameData folder not found: " + dir + " (names/ranges unavailable)");
                return d;
            }

            foreach (Dictionary<string, object> o in Rows(d, dir, "Hero"))
            {
                HeroRow r = new HeroRow();
                r.Id = U(o, "Id"); r.Name = MiniJson.Str(o, "Name", "#" + r.Id);
                r.MaxHp = (int)MiniJson.Num(o, "MaxHp", 0); r.Attack = (int)MiniJson.Num(o, "Attack", 0); r.Defense = (int)MiniJson.Num(o, "Defense", 0);
                r.AttackRange = MiniJson.Num(o, "AttackRange", 2); r.AttackIntervalMs = U(o, "AttackIntervalMs"); r.Speed = MiniJson.Num(o, "Speed", 5);
                d.Heroes[r.Id] = r;
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "Monster"))
            {
                MonsterRow r = new MonsterRow();
                r.Id = U(o, "Id"); r.Name = MiniJson.Str(o, "Name", "#" + r.Id);
                r.MaxHp = (int)MiniJson.Num(o, "MaxHp", 0); r.Attack = (int)MiniJson.Num(o, "Attack", 0); r.Defense = (int)MiniJson.Num(o, "Defense", 0); r.AttackRange = MiniJson.Num(o, "AttackRange", 2);
                r.AttackIntervalMs = U(o, "AttackIntervalMs"); r.Speed = MiniJson.Num(o, "Speed", 3); r.AggroRange = MiniJson.Num(o, "AggroRange", 8);
                d.Monsters[r.Id] = r;
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "Skill"))
            {
                SkillRow r = new SkillRow();
                r.Id = U(o, "Id"); r.Name = MiniJson.Str(o, "Name", "#" + r.Id);
                r.IsArea = MiniJson.Str(o, "Shape", "Single") == "Area";
                r.Range = MiniJson.Num(o, "Range", 3); r.Radius = MiniJson.Num(o, "Radius", 0); r.ProjectileSpeed = MiniJson.Num(o, "ProjectileSpeed", 0);
                r.DamagePercent = U(o, "DamagePercent"); r.FlatDamage = (int)MiniJson.Num(o, "FlatDamage", 0); r.CooldownMs = U(o, "CooldownMs"); r.CastTimeMs = U(o, "CastTimeMs"); r.StatusId = U(o, "StatusId"); r.MaxTargets = U(o, "MaxTargets");
                d.Skills[r.Id] = r;
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "Status"))
            {
                StatusRow r = new StatusRow();
                r.Id = U(o, "Id"); r.Name = MiniJson.Str(o, "Name", "#" + r.Id);
                r.IsDot = MiniJson.Str(o, "Kind", "Dot") == "Dot"; r.TargetsSelf = MiniJson.Str(o, "Target", "Hit") == "Self"; r.TickMs = U(o, "TickMs"); r.DamagePercent = U(o, "DamagePercent");
                r.AttackPercent = (int)MiniJson.Num(o, "AttackPercent", 0); r.DefensePercent = (int)MiniJson.Num(o, "DefensePercent", 0);
                r.SpeedPercent = (int)MiniJson.Num(o, "SpeedPercent", 0); r.DurationMs = U(o, "DurationMs");
                d.Statuses[r.Id] = r;
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "HeroSkill"))
            {
                uint hero = U(o, "HeroId");
                List<uint> list;
                if (!d.HeroSkills.TryGetValue(hero, out list)) { list = new List<uint>(); d.HeroSkills[hero] = list; }
                list.Add(U(o, "SkillId"));
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "Block"))
            {
                if (U(o, "ZoneId") != zoneId) continue;
                BlockRow r = new BlockRow();
                r.IsCircle = MiniJson.Str(o, "Type", "Rect") == "Circle";
                r.X = (float)MiniJson.Num(o, "X", 0); r.Y = (float)MiniJson.Num(o, "Y", 0);
                r.W = (float)MiniJson.Num(o, "W", 0); r.H = (float)MiniJson.Num(o, "H", 0); r.R = (float)MiniJson.Num(o, "R", 0);
                d.Blocks.Add(r);
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "Spawn"))
            {
                if (U(o, "ZoneId") != zoneId) continue;
                SpawnRow r = new SpawnRow();
                r.MonsterId = U(o, "MonsterId"); r.Count = U(o, "Count");
                r.X = (float)MiniJson.Num(o, "X", 0); r.Y = (float)MiniJson.Num(o, "Y", 0); r.Radius = (float)MiniJson.Num(o, "Radius", 0);
                d.Spawns.Add(r);
            }
            // 보스 정의는 선택 사항이다 (서버가 보스 없이 구동할 수도 있으므로 파일이 없어도 경고하지 않는다).
            Dictionary<uint, BossRow> byId = new Dictionary<uint, BossRow>();
            foreach (Dictionary<string, object> o in Rows(d, dir, "Boss", true))
            {
                BossRow r = new BossRow();
                r.Id = U(o, "Id"); r.MonsterId = U(o, "MonsterId"); r.Name = MiniJson.Str(o, "Name", "#" + r.Id);
                r.EnrageAfterMs = U(o, "EnrageAfterMs"); r.EnrageStatusId = U(o, "EnrageStatusId");
                byId[r.Id] = r; d.Bosses[r.MonsterId] = r;
            }
            foreach (Dictionary<string, object> o in Rows(d, dir, "BossPhase", true))
            {
                BossRow b;
                if (!byId.TryGetValue(U(o, "BossId"), out b)) continue;
                BossPhaseRow p = new BossPhaseRow();
                p.Phase = U(o, "Phase"); p.HpBelowPercent = U(o, "HpBelowPercent"); p.EnterStatusId = U(o, "EnterStatusId");
                b.Phases.Add(p);
            }
            foreach (BossRow b in byId.Values) b.Phases.Sort(delegate (BossPhaseRow x, BossPhaseRow y) { return x.Phase.CompareTo(y.Phase); });
            return d;
        }

        private static uint U(Dictionary<string, object> o, string key)
        {
            double v = MiniJson.Num(o, key, 0);
            return v < 0 ? 0u : (uint)v;
        }

        // <dir>/<table>.json 의 {"<table>":[ {...}, ... ]} 행을 돌려준다. 없거나 깨지면 경고만 남기고 빈 목록.
        private static List<Dictionary<string, object>> Rows(GameData d, string dir, string table, bool optional = false)
        {
            List<Dictionary<string, object>> rows = new List<Dictionary<string, object>>();
            string path = Path.Combine(dir, table + ".json");
            if (!File.Exists(path)) { if (!optional) d.Warnings.Add(table + ".json not found"); return rows; }
            try
            {
                Dictionary<string, object> root = MiniJson.AsObject(MiniJson.Parse(File.ReadAllText(path, Encoding.UTF8)));
                object arrObj;
                List<object> arr = (root != null && root.TryGetValue(table, out arrObj)) ? MiniJson.AsArray(arrObj) : null;
                if (arr == null) { d.Warnings.Add(table + ".json has no \"" + table + "\" array"); return rows; }
                foreach (object o in arr)
                {
                    Dictionary<string, object> row = MiniJson.AsObject(o);
                    if (row != null) rows.Add(row);
                }
            }
            catch (Exception e)
            {
                d.Warnings.Add(table + ".json: " + e.Message);
            }
            return rows;
        }

        public string HeroName(uint id) { HeroRow r; return Heroes.TryGetValue(id, out r) ? r.Name : "#" + id; }
        public string MonsterName(uint id) { MonsterRow r; return Monsters.TryGetValue(id, out r) ? r.Name : "#" + id; }
        public string SkillName(uint id) { SkillRow r; return Skills.TryGetValue(id, out r) ? r.Name : "#" + id; }
        public string StatusName(uint id) { StatusRow r; return Statuses.TryGetValue(id, out r) ? r.Name : "#" + id; }
        public SkillRow Skill(uint id) { SkillRow r; return Skills.TryGetValue(id, out r) ? r : null; }
        public HeroRow Hero(uint id) { HeroRow r; return Heroes.TryGetValue(id, out r) ? r : null; }
        public MonsterRow Monster(uint id) { MonsterRow r; return Monsters.TryGetValue(id, out r) ? r : null; }
        public BossRow BossOf(uint monsterId) { BossRow r; return Bosses.TryGetValue(monsterId, out r) ? r : null; }
        public StatusRow Status(uint id) { StatusRow r; return Statuses.TryGetValue(id, out r) ? r : null; }

        public List<uint> SkillsOf(uint heroId)
        {
            List<uint> l;
            return HeroSkills.TryGetValue(heroId, out l) ? l : new List<uint>();
        }

        /// <summary>heroId 가 0 이면 서버 기본 영웅 — 모르므로 ID 가 가장 작은 영웅으로 추정한다.</summary>
        public uint ResolveHeroId(uint heroId)
        {
            if (heroId != 0) return heroId;
            uint best = 0;
            foreach (uint id in Heroes.Keys) if (best == 0 || id < best) best = id;
            return best;
        }
    }
}
