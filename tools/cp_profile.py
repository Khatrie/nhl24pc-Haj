#!/usr/bin/env python3
"""Report from the GPU command-thread sampling profiler (NHLPORT_CP_SAMPLE=1).

The game writes log/nhl24_cp_samples-<session>.txt: call stacks of the shadPS4:GpuCommandProcessor
thread sampled NHLPORT_CP_SAMPLE_HZ times a second (default 500), counted per second of the
session. This tool names the frames with the linker map of the build that ran (build/nhl24.map,
written by every build) and splits the time into slow and normal seconds using the frames CSV of
the same session.

Usage:
  python tools/cp_profile.py LOG_DIR [--session SEL] [--map build/nhl24.map] [--top N]
                             [--folded out.txt] [--out report.md]

  --session   which samples file (latest by default; any unique part of the session id)
  --folded    also write folded stacks (flamegraph.pl / speedscope input) for the slow seconds

Self time is where the thread was executing; inclusive time counts a function once per sample if
it is anywhere on the stack. Frames outside nhl24.exe are shown as module+offset; "[guest/jit]"
is code without a module (guest code or generated code), where unwinding stops.

Standard library only (uses llvm-undname for MSVC names when it is on PATH).
"""

import argparse
import bisect
import glob
import ntpath
import os
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diag_sessions  # noqa: E402
import perf_report  # noqa: E402

GUEST = "[guest/jit]"


def load_samples(path):
    header, modules, stacks, counts = {}, [], {}, []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("#"):
                toks = line[1:].split()
                for i in range(len(toks) - 1):
                    if toks[i] in ("session", "hz", "samples", "failed", "unknown_module",
                                   "suspended_us"):
                        header[toks[i]] = toks[i + 1]
            elif line.startswith("module "):
                _, base, size, p = line.split(" ", 3)
                modules.append((int(base, 16), int(base, 16) + int(size, 16), p))
            elif line.startswith("stack "):
                toks = line.split()
                stacks[int(toks[1])] = [int(t, 16) for t in toks[2:]]
            elif line.startswith("count "):
                _, sec, sid, n = line.split()
                counts.append((int(sec), int(sid), int(n)))
    modules.sort()
    return header, modules, stacks, counts


MAP_LINE = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\b")
LLD_LINE = re.compile(r"^\s*([0-9a-fA-F]{8,16})\s+([0-9a-fA-F]{8,16})\s+([0-9a-fA-F]+)\s+(\S.*)$")


def load_map(path):
    """Sorted (address, name) from a link.exe-style or lld-native map file."""
    syms = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MAP_LINE.match(line)
            if m:
                addr = int(m.group(2), 16)
                if addr:
                    syms.append((addr, m.group(1)))
                continue
            m = LLD_LINE.match(line)
            if m and int(m.group(3), 16) == 0 and not m.group(4).startswith(("<", ".")):
                # lld native format: symbol lines carry alignment 0 and an indented name
                syms.append((int(m.group(1), 16), m.group(4).strip()))
    syms.sort()
    return syms


def demangle_all(names):
    names = sorted(set(n for n in names if n.startswith("?")))
    out = {}
    tool = shutil.which("llvm-undname")
    if tool and names:
        try:
            res = subprocess.run([tool], input="\n".join(names), capture_output=True, text=True,
                                 timeout=120)
            lines = [l for l in res.stdout.splitlines() if l.strip()]
            # llvm-undname echoes each input then the result
            pairs = [(lines[i], lines[i + 1]) for i in range(0, len(lines) - 1, 2)]
            for raw, dem in pairs:
                out[raw] = shorten(dem)
        except (OSError, subprocess.SubprocessError):
            pass
    for n in names:
        if n not in out:
            out[n] = simple_demangle(n)
    return out


def shorten(dem):
    """'private: void __cdecl Ns::Class::func(args) const' -> 'Ns::Class::func'."""
    head = dem.split("(", 1)[0]
    depth, cut = 0, 0
    for i, ch in enumerate(head):  # last space outside template brackets
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == " " and depth == 0:
            cut = i + 1
    return head[cut:] or dem


def simple_demangle(name):
    """Rough MSVC demangling: ?func@Class@Ns@@... -> Ns::Class::func."""
    if not name.startswith("?"):
        return name
    body = name[1:]
    special = ""
    if body.startswith("?0"):
        special, body = "(ctor)", body[2:]
    elif body.startswith("?1"):
        special, body = "(dtor)", body[2:]
    elif body.startswith("?R"):
        special, body = "operator()", body[3:]
    head = body.split("@@", 1)[0]
    parts = [p for p in head.split("@") if p and not p.startswith("?$")]
    parts = [p.lstrip("?$") for p in parts]
    if special:
        parts = [special] + parts
    return "::".join(reversed(parts)) if parts else name


class Symbolizer:
    def __init__(self, modules, syms):
        self.modules = modules
        self.mod_bases = [m[0] for m in modules]
        self.syms = syms
        self.sym_addrs = [s[0] for s in syms]
        self.cache = {}
        self.exe = next((m for m in modules if ntpath.basename(m[2]).lower() == "nhl24.exe"),
                        None)
        self.dem = demangle_all([s[1] for s in syms]) if syms else {}

    def name(self, pc):
        if pc in self.cache:
            return self.cache[pc]
        i = bisect.bisect_right(self.mod_bases, pc) - 1
        if i < 0 or pc >= self.modules[i][1]:
            res = GUEST
        else:
            base, end, path = self.modules[i]
            mod = ntpath.basename(path)
            res = None
            if self.exe and base == self.exe[0] and self.syms:
                j = bisect.bisect_right(self.sym_addrs, pc) - 1
                if j >= 0:
                    raw = self.syms[j][1]
                    res = self.dem.get(raw, raw)
            if res is None:
                res = f"{mod}+0x{pc - base:x}" if mod.lower() != "nhl24.exe" else f"nhl24.exe+0x{pc - base:x}"
                # group driver/system frames by module for readability
                if mod.lower() != "nhl24.exe":
                    res = f"{mod}!?"
        self.cache[pc] = res
        return res


def frame_names(sym, stack):
    # Return addresses point after the call; step back one byte for frames above the leaf.
    return [sym.name(pc if i == 0 else pc - 1) for i, pc in enumerate(stack)]


def pick_samples(log_dir, spec):
    files = sorted(glob.glob(os.path.join(log_dir, "nhl24_cp_samples-*.txt")))
    if not files:
        raise SystemExit(f"no nhl24_cp_samples-*.txt in {log_dir} (run with NHLPORT_CP_SAMPLE=1)")
    ids = [os.path.basename(f)[len("nhl24_cp_samples-"):-4] for f in files]
    chosen = diag_sessions.select(spec or "latest", ids)
    if len(chosen) != 1:
        raise SystemExit("select exactly one session")
    return files[ids.index(chosen[0])], chosen[0]


def second_kinds(log_dir, session):
    frames_path = os.path.join(log_dir, "nhl24_frames.csv")
    threads_path = os.path.join(log_dir, "nhl24_threads.csv")
    if not os.path.exists(frames_path):
        return {}
    frames = perf_report.load_frames(frames_path, [session])
    threads = perf_report.load_threads(threads_path if os.path.exists(threads_path) else None,
                                       [session])
    return {w["t"]: w for w in perf_report.build_windows(frames, threads)}


def table(title, counter, total, top):
    L = [f"### {title}\n", "| % | Samples | Function |", "|---:|---:|---|"]
    for name, n in counter.most_common(top):
        L.append(f"| {100.0 * n / total:.1f} | {n:,} | `{name[:150]}` |")
    L.append("")
    return L


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log_dir")
    ap.add_argument("--session")
    ap.add_argument("--map", default=None)
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--folded")
    ap.add_argument("--out")
    a = ap.parse_args()

    path, session = pick_samples(a.log_dir, a.session)
    header, modules, stacks, counts = load_samples(path)
    map_path = a.map or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build",
                                     "nhl24.map")
    syms = load_map(map_path) if os.path.exists(map_path) else []
    sym = Symbolizer(modules, syms)
    named = {sid: frame_names(sym, st) for sid, st in stacks.items()}
    kinds = second_kinds(a.log_dir, session)

    groups = {"slow": "slow seconds (< 55 FPS, not a steady 30 FPS cadence)",
              "busy": "seconds with >= 40k draws/s", "all": "all seconds"}
    L = [f"# GPU command thread profile: session {session}\n"]
    exe_info = diag_sessions.log_info(os.path.join(a.log_dir, "nhl24_diag.log")).get(session, {})
    total_samples = sum(c for _, _, c in counts)
    dur = max((s for s, _, _ in counts), default=0) + 1
    L.append(f"- Build {exe_info.get('build', '?')}; {header.get('hz', '?')} Hz; {total_samples:,} samples over"
             f" about {dur} s; {header.get('failed', '0')} failed; {header.get('unknown_module', '0')} stopped"
             " in code without a module")
    try:
        overhead = float(header.get("suspended_us", 0)) / (dur * 1e6) * 100.0
        L.append(f"- Command thread suspended {overhead:.2f}% of the time by the sampler")
    except ValueError:
        pass
    L.append(f"- Symbols: {len(syms):,} from {os.path.normpath(map_path) if syms else 'none (no map file)'}")
    if syms and sym.exe is None:
        L.append("- nhl24.exe not in the module list; symbols unused")
    L.append("- Check that the map belongs to the build that ran (same build hash as the diag log).\n")

    folded = Counter()
    for gname, gdesc in groups.items():
        self_c, incl_c, mod_c, callers = Counter(), Counter(), Counter(), defaultdict(Counter)
        total = 0
        for sec, sid, n in counts:
            w = kinds.get(sec)
            if gname == "slow" and not (w and w["kind"] == "slow"):
                continue
            if gname == "busy" and not (w and w["draws"] + w["dispatches"] >= 40000):
                continue
            names = named.get(sid, [GUEST])
            total += n
            self_c[names[0]] += n
            for nm in set(names):
                incl_c[nm] += n
            leaf_pc = stacks[sid][0] if stacks.get(sid) else 0
            i = bisect.bisect_right(sym.mod_bases, leaf_pc) - 1
            mod_c[ntpath.basename(modules[i][2]) if i >= 0 and leaf_pc < modules[i][1] else GUEST] += n
            if len(names) > 1:
                callers[names[0]][names[1]] += n
            if gname == "slow" and a.folded:
                folded[";".join(reversed(names))] += n
        L.append(f"## {gdesc}: {total:,} samples\n")
        if not total:
            L.append("(none)\n")
            continue
        L += table("Self (leaf) time", self_c, total, a.top)
        L += table("Inclusive time", incl_c, total, a.top)
        L.append("### Leaf module\n")
        for m, n in mod_c.most_common(10):
            L.append(f"- {m}: {100.0 * n / total:.1f}%")
        L.append("\n### Main caller of the top leaf functions\n")
        for name, n in self_c.most_common(12):
            if callers[name]:
                c, cn = callers[name].most_common(1)[0]
                L.append(f"- `{name[:90]}` ← `{c[:90]}` ({100.0 * cn / n:.0f}%)")
        L.append("")
    text = "\n".join(L)
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    else:
        sys.stdout.write(text + "\n")
    if a.folded:
        with open(a.folded, "w", encoding="utf-8") as f:
            for k, n in folded.most_common():
                f.write(f"{k} {n}\n")


if __name__ == "__main__":
    main()
