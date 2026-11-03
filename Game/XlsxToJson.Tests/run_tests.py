#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XlsxToJson 골든 파일 회귀 테스트.

사용법:
  python run_tests.py --exe ..\\Bin\\x64\\Release\\XlsxToJson64.exe            # 전체 실행
  python run_tests.py --exe ... --case enum_const                              # 한 케이스만
  python run_tests.py --exe ... --update                                       # 현재 출력을 골든으로 저장 (diff로 검토할 것)

케이스 폴더 (cases/<이름>/):
  sheets/<시트>.tsv   탭으로 구분한 셀. 숫자는 숫자 셀, TRUE/FALSE는 불리언 셀, 빈 칸은 빈 셀, 그 외는 문자열.
                      숫자처럼 보이는 문자열은 앞에 ' 를 붙인다. 시트 순서는 _order.txt(없으면 파일 이름순).
  args.txt            추가 인자. 경로 자리는 {out} {cpp} {cs} 로 쓴다. 기본: --out {out}
  expect/             성공 케이스의 골든 파일. 작업 폴더 기준 상대 경로 (out/Hero.json, cpp/Hero.gen.h ...). 바이트 단위로 비교
  expect_exit         종료 코드 (기본 0)
  expect_stderr.txt   실패 케이스: 줄마다 stderr에 들어 있어야 하는 문장. 실패하면 out/cpp/cs에 아무 파일도 없어야 한다.
"""
import argparse, os, re, shlex, shutil, subprocess, sys, tempfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
NUM = re.compile(r'^-?\d+(\.\d+)?([eE][-+]?\d+)?$')


def read_cases(case):
    d = case / 'sheets'
    order = d / '_order.txt'
    names = [l.strip() for l in order.read_text(encoding='utf-8').splitlines() if l.strip()] if order.exists() \
        else sorted(p.stem for p in d.glob('*.tsv'))
    out = []
    for n in names:
        rows = [l.split('\t') for l in (d / (n + '.tsv')).read_text(encoding='utf-8').split('\n') if l != '' or False]
        out.append((n, rows))
    return out


def typed(cell):
    if cell == '': return ('E', '')
    if cell in ('TRUE', 'FALSE'): return ('B', cell == 'TRUE')
    if cell.startswith("'"): return ('S', cell[1:])
    if NUM.match(cell): return ('N', cell)
    return ('S', cell)


def build_xlsx(case, dest):
    import openpyxl
    wb = openpyxl.Workbook(); wb.remove(wb.active)
    for name, rows in read_cases(case):
        ws = wb.create_sheet(name)
        for r in rows:
            ws.append([None if (k := typed(c))[0] == 'E' else (float(k[1]) if k[0] == 'N' and ('.' in k[1] or 'e' in k[1].lower()) else int(k[1]) if k[0] == 'N' else k[1]) for c in r])
    wb.save(dest)


def build_stub(case, dest):
    """개발 중 xlnt 없이 돌려 볼 때 쓰는 TSV 폴더 형식 (스텁 리더 전용)."""
    dest.mkdir(parents=True, exist_ok=True)
    names = []
    for name, rows in read_cases(case):
        names.append(name)
        lines = []
        for r in rows:
            cells = []
            for c in r:
                k, v = typed(c)
                cells.append({'E': 'E:', 'B': 'B:%d' % v if k == 'B' else '', 'S': 'S:' + str(v), 'N': 'N:' + str(v)}[k])
            lines.append('\t'.join(cells))
        (dest / (name + '.tsv')).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    (dest / '_order.txt').write_text('\n'.join(names) + '\n', encoding='utf-8')


def tree(root):
    return {str(p.relative_to(root)).replace('\\', '/'): p for p in sorted(root.rglob('*')) if p.is_file() and not p.name.startswith('.')}


def run_exe(exe, args, cwd):
    return subprocess.run([str(exe)] + args, cwd=cwd, capture_output=True)


def expand(case, work, input_path):
    text = (case / 'args.txt').read_text(encoding='utf-8').strip() if (case / 'args.txt').exists() else '--out {out}'
    args = shlex.split(text.format(out='out', cpp='cpp', cs='cs'))
    return [str(input_path)] + args


def run_case(exe, case, stub, update):
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        inp = work / ('in' if stub else 'in.xlsx')
        (build_stub if stub else build_xlsx)(case, inp)
        r = run_exe(exe, expand(case, work, inp), work)
        want_exit = int((case / 'expect_exit').read_text().strip()) if (case / 'expect_exit').exists() else 0
        problems = []
        if r.returncode != want_exit:
            problems.append('exit %d (expected %d)\n%s' % (r.returncode, want_exit, r.stderr.decode('utf-8', 'replace')))
        got = {k: v for k, v in tree(work).items() if k.split('/')[0] in ('out', 'cpp', 'cs')}

        if want_exit != 0:
            err = r.stderr.decode('utf-8', 'replace')
            f = case / 'expect_stderr.txt'
            if update and not f.exists():
                f.write_text('', encoding='utf-8')
            for line in (f.read_text(encoding='utf-8').splitlines() if f.exists() else []):
                if line.strip() and line not in err:
                    problems.append('stderr에 없음: ' + line)
            if got:
                problems.append('실패했는데 파일이 쓰임: ' + ', '.join(got))
            return problems

        exp_dir = case / 'expect'
        if update:
            shutil.rmtree(exp_dir, ignore_errors=True)
            for k, p in got.items():
                (exp_dir / k).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, exp_dir / k)
            return problems
        want = tree(exp_dir) if exp_dir.exists() else {}
        for k in sorted(set(want) | set(got)):
            if k not in got: problems.append('없음: ' + k)
            elif k not in want: problems.append('예상 밖 파일: ' + k)
            elif got[k].read_bytes() != want[k].read_bytes():
                problems.append('내용 다름: ' + k)
        return problems


# ---------------------------------------------------------------------------
# 동작 시나리오 (여러 번 실행해야 확인되는 것)
# ---------------------------------------------------------------------------
def scenarios(exe, stub):
    base = HERE / 'cases' / 'enum_const'
    results = []

    def check(name, ok, detail=''):
        results.append((name, [] if ok else [detail or 'failed']))

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        inp = work / ('in' if stub else 'in.xlsx')
        (build_stub if stub else build_xlsx)(base, inp)
        a = [str(inp), '--out', 'out', '--cpp-out', 'cpp', '--cs-out', 'cs']

        r = run_exe(exe, a, work)
        check('scn/first-run', r.returncode == 0, r.stderr.decode('utf-8', 'replace'))
        h = sorted((work / 'cpp').glob('*.gen.h'))
        check('scn/bom-on-generated', bool(h) and all(p.read_bytes()[:3] == b'\xef\xbb\xbf' for p in h + sorted((work / 'cs').glob('*.cs'))))
        check('scn/no-bom-on-json', all(p.read_bytes()[:3] != b'\xef\xbb\xbf' for p in (work / 'out').glob('*.json')))

        # 내용이 같으면 다시 쓰지 않는다
        stamp = {p: p.stat().st_mtime_ns for p in h}
        time.sleep(0.05)
        r = run_exe(exe, a, work)
        check('scn/unchanged-not-rewritten', all(p.stat().st_mtime_ns == t for p, t in stamp.items()))

        # 생성 표식이 있는 낡은 파일만 지운다
        stale = work / 'cpp' / 'Old.gen.h'
        stale.write_bytes(b'\xef\xbb\xbf// \xec\x9e\x90\xeb\x8f\x99 \xec\x83\x9d\xec\x84\xb1 \xed\x8c\x8c\xec\x9d\xbc\n')
        mine = work / 'cpp' / 'Mine.gen.h'
        mine.write_text('// hand written\n')
        run_exe(exe, a, work)
        check('scn/stale-generated-removed', not stale.exists())
        check('scn/handwritten-kept', mine.exists())

        # 증분 모드
        ia = a + ['--incremental']
        r = run_exe(exe, ia, work)
        r = run_exe(exe, ia, work)
        check('scn/incremental-skips', b'up to date' in r.stdout, r.stdout.decode('utf-8', 'replace'))
        victim = work / 'cpp' / 'Enums.gen.h'
        victim.unlink()
        r = run_exe(exe, ia, work)
        check('scn/incremental-regenerates-missing', victim.exists() and r.returncode == 0)
        time.sleep(1.1)
        os.utime(inp, None)
        r = run_exe(exe, ia, work)
        check('scn/incremental-reruns-when-input-newer', b'up to date' not in r.stdout and r.returncode == 0)
        r = run_exe(exe, ia, work)
        old = time.time() - 86400
        os.utime(inp, (old, old))
        r = run_exe(exe, ia, work)
        check('scn/incremental-reruns-when-input-restored-older', b'up to date' not in r.stdout and r.returncode == 0)
        r = run_exe(exe, [str(inp), '--out', 'out', '--cpp-out', 'cpp', '--incremental'], work)
        check('scn/incremental-args-change-reruns', b'up to date' not in r.stdout and r.returncode == 0)

        # 엑셀을 읽을 수 없으면 경고만 내고 기존 출력 사용
        run_exe(exe, ia, work)
        time.sleep(1.1)
        if stub:
            os.rename(inp / '_order.txt', inp / '_order.bak'); os.utime(inp, None)
        else:
            inp.write_bytes(b'not a zip')
        r = run_exe(exe, ia, work)
        check('scn/unreadable-input-warns-and-succeeds', r.returncode == 0 and b'warning' in r.stderr, r.stderr.decode('utf-8', 'replace'))
        r = run_exe(exe, a, work)
        check('scn/unreadable-input-without-incremental-fails', r.returncode == 2)
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--case')
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--stub', action='store_true', help='개발용: TSV 폴더를 입력으로 쓰는 스텁 리더 빌드')
    o = ap.parse_args()
    exe = Path(o.exe).resolve()

    cases = sorted(p for p in (HERE / 'cases').iterdir() if p.is_dir() and (not o.case or p.name == o.case))
    results = [(c.name, run_case(exe, c, o.stub, o.update)) for c in cases]
    if not o.update and not o.case:
        results += scenarios(exe, o.stub)

    failed = 0
    for name, problems in results:
        print(('PASS ' if not problems else 'FAIL ') + name)
        for p in problems:
            print('     ' + p.replace('\n', '\n     '))
        failed += bool(problems)
    print('%d/%d passed' % (len(results) - failed, len(results)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
