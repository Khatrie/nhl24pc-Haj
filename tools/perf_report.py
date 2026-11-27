#!/usr/bin/env python3
"""Summarize NHL24 PC diagnostic sessions (nhl24_frames.csv / nhl24_threads.csv).

Complements tools/diag_report.py (spike table, glitches, markers) with sustained-slowdown
clusters, workload correlation, draw-phase breakdowns and build-to-build comparison.

Usage:
  python tools/perf_report.py LOG_DIR_OR_FRAMES_CSV [--threads CSV] [--out report.md]
                            [--compare BASELINE_DIR_OR_CSV] [--json summary.json] [--top N]

Standard library only. Per-frame thread CPU columns (cpu_ms, gpu_thr_ms, ...) are sampled with
Windows thread-time granularity (15.625 ms) and are only meaningful when aggregated, so thread
saturation is taken from nhl24_threads.csv one-second samples where available.
"""

import argparse
import csv
import json
import math
import os
import sys
from collections import defaultdict

SLOW_THRESHOLDS = (55.0, 45.0, 30.0)
NATIVE30_LO, NATIVE30_HI = 31.0, 36.0
CP_THREAD = "shadPS4:GpuCommandProcessor"

TIME_COLUMNS = [
    ("pipe_ms", "pipeline compile"),
    ("shader_ms", "shader compile"),
    ("rb_wait_ms", "readback wait"),
    ("stall_ms", "gpu stall"),
    ("vma_image_create_cpu_ms", "vmaCreateImage"),
    ("image_alloc_cpu_ms", "image alloc+register"),
    ("expand_guest_copy_cpu_ms", "expand guest copy"),
    ("refresh_guest_copy_cpu_ms", "refresh guest copy"),
    ("expand_sync_cpu_ms", "expand buffer sync"),
    ("refresh_sync_cpu_ms", "refresh buffer sync"),
    ("expand_staging_cpu_ms", "expand staging"),
    ("refresh_staging_cpu_ms", "refresh staging"),
    ("expand_detile_cpu_ms", "expand detile"),
    ("refresh_detile_cpu_ms", "refresh detile"),
]

WINDOW_SUMS = [
    "draws", "dispatches", "pipes", "pipe_ms", "shaders", "shader_ms", "rb_wait_ms", "stall_ms",
    "gpu_busy_ms", "img_new", "img_freed", "tc_expand", "refresh", "refresh_kb", "file_reads",
    "file_kb", "page_faults", "gpu_thr_ms", "vma_image_create_cpu_ms", "image_alloc_cpu_ms",
    "expand_guest_copy_cpu_ms", "refresh_guest_copy_cpu_ms", "cpu_inval", "gpu_inval",
    "render_passes", "gfx_submits", "vk_submits", "upload_staging_new_bytes",
    "vk_rendering_begins", "draw_total_cpu_ms",
]

DRAW_PHASES = ["filter", "pipeline", "targets", "resources", "render_state", "vertex_index",
               "barriers", "descriptors", "dynamic_state", "record"]
WINDOW_SUMS += [f"draw_{p}_cpu_ms" for p in DRAW_PHASES] + ["dispatch_cpu_ms"]


def fnum(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def resolve_paths(path, threads_override=None):
    if os.path.isdir(path):
        frames = os.path.join(path, "nhl24_frames.csv")
        threads = os.path.join(path, "nhl24_threads.csv")
    else:
        frames = path
        threads = os.path.join(os.path.dirname(path) or ".", "nhl24_threads.csv")
    if threads_override:
        threads = threads_override
    return frames, threads if os.path.exists(threads) else None


COLUMNS = set()


def load_frames(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        COLUMNS.update(reader.fieldnames)
        numeric = [c for c in reader.fieldnames if c != "flags"]
        for raw in reader:
            row = {c: fnum(raw.get(c)) for c in numeric}
            row["flags"] = raw.get("flags") or ""
            rows.append(row)
    return rows


def load_threads(path):
    per_second = defaultdict(dict)
    if not path:
        return per_second
    with open(path, newline="") as f:
        for raw in csv.DictReader(f):
            t = int(math.floor(fnum(raw.get("t_s"))))
            name = raw.get("name") or ""
            pct = fnum(raw.get("cpu_pct"))
            per_second[t][name] = per_second[t].get(name, 0.0) + pct
    return per_second


def percentile(sorted_values, p):
    if not sorted_values:
        return 0.0
    k = (len(sorted_values) - 1) * p / 100.0
    lo = math.floor(k)
    hi = min(lo + 1, len(sorted_values) - 1)
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (k - lo)


def low_fps(sorted_dt, fraction):
    n = max(1, int(len(sorted_dt) * fraction))
    worst = sorted_dt[-n:]
    return 1000.0 / (sum(worst) / len(worst)) if worst else 0.0


def pearson(xs, ys):
    n = len(xs)
    if n < 3:
        return float("nan")
    mx, my = sum(xs) / n, sum(ys) / n
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    if sxx <= 0 or syy <= 0:
        return float("nan")
    return sxy / math.sqrt(sxx * syy)


def build_windows(frames, threads):
    buckets = defaultdict(list)
    for r in frames:
        buckets[int(math.floor(r["t_s"]))].append(r)
    windows = []
    for t in sorted(buckets):
        rows = buckets[t]
        elapsed = sum(r["dt_ms"] for r in rows)
        w = {"t": t, "frames": len(rows), "elapsed_ms": elapsed}
        for c in WINDOW_SUMS:
            w[c] = sum(r.get(c, 0.0) for r in rows)
        w["fps"] = 1000.0 * len(rows) / elapsed if elapsed > 0 else 0.0
        w["max_dt"] = max(r["dt_ms"] for r in rows)
        in30 = sum(1 for r in rows if NATIVE30_LO <= r["dt_ms"] <= NATIVE30_HI)
        w["frac30"] = in30 / len(rows)
        w["markers"] = sum(1 for r in rows if "M" in r["flags"])
        sample = threads.get(t) or threads.get(t - 1) or {}
        w["cp_pct"] = sample.get(CP_THREAD)
        if w["cp_pct"] is None and elapsed > 0:
            w["cp_pct_est"] = 100.0 * w["gpu_thr_ms"] / elapsed
        w["gpu_busy_pct"] = 100.0 * w["gpu_busy_ms"] / elapsed if elapsed > 0 else 0.0
        w["kind"] = classify_window(w)
        windows.append(w)
    return windows


def cp_of(w):
    if w.get("cp_pct") is not None:
        return w["cp_pct"]
    return w.get("cp_pct_est", 0.0)


def classify_window(w):
    if w["frames"] == 0:
        return "empty"
    if w["max_dt"] > 1000.0:
        return "pause"
    if w["fps"] >= 55.0:
        return "ok"
    cp = cp_of(w)
    if w["frac30"] >= 0.85 and 28.0 <= w["fps"] <= 31.5 and cp < 85.0 and w["pipe_ms"] < 50.0:
        return "steady30"
    return "slow"


def clusters(windows, threshold, min_len=3):
    out, cur = [], []
    for w in windows:
        if w["kind"] in ("slow",) and w["fps"] < threshold:
            if cur and w["t"] != cur[-1]["t"] + 1:
                if len(cur) >= min_len:
                    out.append(cur)
                cur = []
            cur.append(w)
        else:
            if len(cur) >= min_len:
                out.append(cur)
            cur = []
    if len(cur) >= min_len:
        out.append(cur)
    return out


def summarize(frames, windows):
    dts = sorted(r["dt_ms"] for r in frames)
    active = [r for r in frames if r["draws"] > 0]
    total_s = sum(dts) / 1000.0
    kinds = defaultdict(int)
    for w in windows:
        kinds[w["kind"]] += 1
    s = {
        "frames": len(frames),
        "duration_s": total_s,
        "p50": percentile(dts, 50),
        "p95": percentile(dts, 95),
        "p99": percentile(dts, 99),
        "p999": percentile(dts, 99.9),
        "max": dts[-1] if dts else 0.0,
        "avg_fps": len(frames) / total_s if total_s else 0.0,
        "low1_fps": low_fps(dts, 0.01),
        "low01_fps": low_fps(dts, 0.001),
        "over_budget": sum(1 for d in dts if d > 17.5),
        "over_33": sum(1 for d in dts if d > 34.0),
        "over_50": sum(1 for d in dts if d > 50.0),
        "over_100": sum(1 for d in dts if d > 100.0),
        "active_over_50": sum(1 for r in active if r["dt_ms"] > 50.0),
        "markers": sum(1 for r in frames if "M" in r["flags"]),
        "window_kinds": dict(kinds),
        "compile_ms": sum(r["pipe_ms"] + r["shader_ms"] for r in frames),
        "readback_wait_ms": sum(r["rb_wait_ms"] for r in frames),
    }
    for thr in SLOW_THRESHOLDS:
        cl = clusters(windows, thr)
        s[f"below_{int(thr)}_clusters"] = len(cl)
        s[f"below_{int(thr)}_seconds"] = sum(len(c) for c in cl)
    run, longest, runs = 0, 0, 0
    for r in frames:
        if r["dt_ms"] > 17.5:
            run += 1
        else:
            if run >= 3:
                runs += 1
            longest = max(longest, run)
            run = 0
    s["slow_runs_ge3"] = runs
    s["longest_slow_run"] = max(longest, run)
    return s


def attribute(r):
    parts = [(r.get(col, 0.0), label) for col, label in TIME_COLUMNS]
    parts = [(v, l) for v, l in parts if v >= 1.0]
    parts.sort(reverse=True)
    return parts


def spike_cause(r):
    if r["draws"] == 0 and r["dispatches"] == 0:
        if r.get("sim_ms", 0) > 0.5 * r["dt_ms"]:
            return "no rendering; guest sim thread time"
        return "no rendering"
    if r["pipe_ms"] + r["shader_ms"] > 0.4 * r["dt_ms"]:
        return "compilation"
    if r["rb_wait_ms"] > 0.3 * r["dt_ms"]:
        return "readback"
    tex = (r.get("image_alloc_cpu_ms", 0) + r.get("expand_guest_copy_cpu_ms", 0) +
           r.get("refresh_guest_copy_cpu_ms", 0) + r.get("expand_sync_cpu_ms", 0) +
           r.get("refresh_sync_cpu_ms", 0))
    if tex > 0.3 * r["dt_ms"] or r["refresh_kb"] > 100 * 1024:
        return "texture streaming"
    if r["file_kb"] > 4096:
        return "file reads"
    return "unattributed"


def fmt_cp(w):
    if w.get("cp_pct") is not None:
        return f"{w['cp_pct']:.0f}%"
    return f"~{w.get('cp_pct_est', 0.0):.0f}%"


def report(name, frames, windows, threads, top):
    s = summarize(frames, windows)
    L = []
    L.append(f"# Session report: {name}\n")
    L.append("## Summary\n")
    L.append(f"- Frames {s['frames']:,}, {s['duration_s'] / 60:.1f} min, average {s['avg_fps']:.1f} FPS"
             f", markers {s['markers']}")
    L.append(f"- Frame time P50 {s['p50']:.2f} / P95 {s['p95']:.2f} / P99 {s['p99']:.2f}"
             f" / P99.9 {s['p999']:.2f} / max {s['max']:.2f} ms")
    L.append(f"- 1% low {s['low1_fps']:.1f} FPS, 0.1% low {s['low01_fps']:.1f} FPS")
    L.append(f"- Frames over 17.5 ms {s['over_budget']:,}, over 34 ms {s['over_33']:,},"
             f" over 50 ms {s['over_50']:,} ({s['active_over_50']:,} while rendering), over 100 ms {s['over_100']:,}")
    L.append(f"- Slow runs (>=3 consecutive frames over budget) {s['slow_runs_ge3']:,}; longest {s['longest_slow_run']} frames")
    L.append(f"- Total compile time {s['compile_ms']:.0f} ms, readback wait {s['readback_wait_ms']:.0f} ms")
    k = s["window_kinds"]
    L.append(f"- One-second windows: ok {k.get('ok', 0)}, slow {k.get('slow', 0)},"
             f" steady-30 (likely guest 30 FPS cadence) {k.get('steady30', 0)}, pause {k.get('pause', 0)}")
    for thr in SLOW_THRESHOLDS:
        L.append(f"- Sustained below {int(thr)} FPS (>=3 s, excluding steady-30): "
                 f"{s[f'below_{int(thr)}_clusters']} clusters, {s[f'below_{int(thr)}_seconds']} s")
    L.append("")

    L.append("## Sustained slowdown clusters (below 55 FPS, longest first)\n")
    cl = sorted(clusters(windows, 55.0), key=len, reverse=True)[:top]
    if cl:
        L.append("| Start s | Len s | FPS | Draws/s | Disp/s | CP CPU | GPU busy | Refresh MiB/s | Img new/s | Expand/s | File MiB/s | Compile ms/s |")
        L.append("|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
        for c in cl:
            n = len(c)
            agg = lambda key: sum(w[key] for w in c) / n
            fps = sum(w["frames"] for w in c) / (sum(w["elapsed_ms"] for w in c) / 1000.0)
            cps = [cp_of(w) for w in c]
            L.append(f"| {c[0]['t']} | {n} | {fps:.1f} | {agg('draws'):,.0f} | {agg('dispatches'):,.0f} |"
                     f" {sum(cps) / n:.0f}% | {agg('gpu_busy_pct'):.0f}% | {agg('refresh_kb') / 1024:.1f} |"
                     f" {agg('img_new'):.1f} | {agg('tc_expand'):.1f} | {agg('file_kb') / 1024:.2f} |"
                     f" {agg('pipe_ms') + agg('shader_ms'):.1f} |")
    else:
        L.append("None.")
    L.append("")

    ok = [w for w in windows if w["kind"] == "ok"]
    slow = [w for w in windows if w["kind"] == "slow"]
    if ok and slow:
        L.append("## Normal vs slow windows (means per second)\n")
        L.append("| Metric | ok | slow | ratio |")
        L.append("|---|---:|---:|---:|")
        rows = [("FPS", "fps"), ("Draws", "draws"), ("Dispatches", "dispatches"),
                ("Render passes", "render_passes"), ("GPU busy %", "gpu_busy_pct"),
                ("Refresh MiB", "refresh_kb"), ("Images created", "img_new"),
                ("Expansions", "tc_expand"), ("File MiB", "file_kb"), ("Page faults", "page_faults"),
                ("CPU invalidations", "cpu_inval"), ("Compile ms", "pipe_ms")]
        for label, key in rows:
            a = sum(w[key] for w in ok) / len(ok)
            b = sum(w[key] for w in slow) / len(slow)
            if key in ("refresh_kb", "file_kb"):
                a, b = a / 1024, b / 1024
            ratio = b / a if a else float("nan")
            L.append(f"| {label} | {a:,.2f} | {b:,.2f} | {ratio:.2f} |")
        a = sum(cp_of(w) for w in ok) / len(ok)
        b = sum(cp_of(w) for w in slow) / len(slow)
        L.append(f"| GPU command processor CPU % | {a:.1f} | {b:.1f} | {b / a if a else float('nan'):.2f} |")
        L.append("")

    rendering = [w for w in windows if w["kind"] in ("ok", "slow") and w["draws"] > 0]
    if len(rendering) > 10:
        L.append("## Correlation with window FPS (rendering windows, excluding steady-30 and pauses)\n")
        fps = [w["fps"] for w in rendering]
        L.append("| Metric | Pearson r |")
        L.append("|---|---:|")
        corr_rows = [("draws", "draws"), ("dispatches", "dispatches"), ("render passes", "render_passes"),
                     ("GPU CP CPU %", None), ("GPU busy %", "gpu_busy_pct"), ("refresh KiB", "refresh_kb"),
                     ("images created", "img_new"), ("expansions", "tc_expand"), ("file KiB", "file_kb"),
                     ("page faults", "page_faults"), ("compile ms", "pipe_ms")]
        for label, key in corr_rows:
            xs = [cp_of(w) for w in rendering] if key is None else [w[key] for w in rendering]
            L.append(f"| {label} | {pearson(xs, fps):+.2f} |")
        L.append("")
        draws_fps = sorted(rendering, key=lambda w: w["draws"])
        L.append("## FPS by draw-call load\n")
        L.append("| Draws/s bucket | Windows | Mean FPS | Mean CP CPU |")
        L.append("|---|---:|---:|---:|")
        edges = [0, 40000, 60000, 80000, 100000, 120000, 10 ** 9]
        for lo, hi in zip(edges, edges[1:]):
            b = [w for w in draws_fps if lo <= w["draws"] < hi]
            if b:
                label = f"{lo // 1000}k–{hi // 1000}k" if hi < 10 ** 9 else f">= {lo // 1000}k"
                L.append(f"| {label} | {len(b)} | {sum(w['fps'] for w in b) / len(b):.1f} |"
                         f" {sum(cp_of(w) for w in b) / len(b):.0f}% |")
        L.append("")

    if "vk_rendering_begins" in COLUMNS and ok:
        L.append("## Render passes actually begun\n")
        for label, ws in (("ok", ok), ("slow", slow)):
            d = sum(w["draws"] for w in ws)
            b = sum(w["vk_rendering_begins"] for w in ws)
            if d:
                L.append(f"- {label} windows: {b / len(ws):,.0f} begins/s, {b / d:.2f} per draw")
        L.append("")

    if "draw_total_cpu_ms" in COLUMNS and sum(w["draw_total_cpu_ms"] for w in windows) > 0:
        L.append("## GPU command thread time per draw (NHLPORT_DIAG_DRAW_PHASES=1)\n")
        L.append("| Phase | ok µs/draw | slow µs/draw | slow ms/s |")
        L.append("|---|---:|---:|---:|")
        groups = [g for g in (ok, slow) if g]
        def per_draw(ws, key):
            d = sum(w["draws"] for w in ws)
            return 1000.0 * sum(w[key] for w in ws) / d if d else 0.0
        keys = [(p, f"draw_{p}_cpu_ms") for p in DRAW_PHASES] + [("all draws", "draw_total_cpu_ms")]
        for label, key in keys:
            a = per_draw(ok, key) if ok else 0.0
            b = per_draw(slow, key) if slow else 0.0
            ms = sum(w[key] for w in slow) / len(slow) if slow else 0.0
            L.append(f"| {label} | {a:.2f} | {b:.2f} | {ms:.1f} |")
        for label, ws in (("ok", ok), ("slow", slow)):
            if ws:
                disp = sum(w["dispatch_cpu_ms"] for w in ws) / len(ws)
                draw = sum(w["draw_total_cpu_ms"] for w in ws) / len(ws)
                L.append(f"\n- {label}: draws {draw:.0f} ms/s + dispatches {disp:.0f} ms/s of GPU command thread time"
                         f" (CP CPU {sum(cp_of(w) for w in ws) / len(ws):.0f}%); the rest is PM4 processing,"
                         " submission, texture/buffer work outside draws, and waits")
        L.append("")

    L.append(f"## Top {top} frame-time spikes\n")
    L.append("| Frame | t s | dt ms | Draws | Refresh MiB | Img new | Expand | Faults | File KiB | Cause | Largest timed components |")
    L.append("|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|")
    for r in sorted(frames, key=lambda r: r["dt_ms"], reverse=True)[:top]:
        parts = ", ".join(f"{l} {v:.0f}" for v, l in attribute(r)[:4]) or "-"
        L.append(f"| {int(r['frame'])} | {r['t_s']:.1f} | {r['dt_ms']:.1f} | {int(r['draws'])} |"
                 f" {r['refresh_kb'] / 1024:.0f} | {int(r['img_new'])} | {int(r['tc_expand'])} |"
                 f" {int(r['page_faults']):,} | {r['file_kb']:.0f} | {spike_cause(r)} | {parts} |")
    L.append("")

    big = [r for r in frames if r["dt_ms"] > 50.0]
    if big:
        causes = defaultdict(int)
        for r in big:
            causes[spike_cause(r)] += 1
        L.append("## Causes of frames over 50 ms (heuristic)\n")
        for c, n in sorted(causes.items(), key=lambda kv: -kv[1]):
            L.append(f"- {c}: {n}")
        totals = defaultdict(float)
        for r in big:
            for col, label in TIME_COLUMNS:
                totals[label] += r.get(col, 0.0)
        frame_total = sum(r["dt_ms"] for r in big)
        L.append(f"\nTimed components across those frames ({frame_total:,.0f} ms total):\n")
        for label, v in sorted(totals.items(), key=lambda kv: -kv[1])[:10]:
            if v > 0:
                L.append(f"- {label}: {v:,.0f} ms ({100 * v / frame_total:.1f}%)")
        L.append("")

    if threads:
        totals, counts = defaultdict(float), defaultdict(int)
        for sample in threads.values():
            for n, pct in sample.items():
                totals[n] += pct
                counts[n] += 1
        L.append("## Thread CPU (one-second samples)\n")
        L.append("| Thread | Mean % | P95 % | Seconds >= 90% |")
        L.append("|---|---:|---:|---:|")
        series = defaultdict(list)
        for sample in threads.values():
            for n, pct in sample.items():
                series[n].append(pct)
        for n in sorted(series, key=lambda n: -sum(series[n]) / len(series[n]))[:12]:
            v = sorted(series[n])
            L.append(f"| {n} | {sum(v) / len(v):.1f} | {percentile(v, 95):.1f} | {sum(1 for x in v if x >= 90)} |")
        L.append("")

    if s["markers"]:
        L.append("## Marker windows (+-5 s around each F1 marker)\n")
        L.append("| Marker | t s | FPS | P99 ms | Max ms | Draws/s |")
        L.append("|---:|---:|---:|---:|---:|---:|")
        marks = [r for r in frames if "M" in r["flags"]]
        for i, m in enumerate(marks, 1):
            near = [r for r in frames if abs(r["t_s"] - m["t_s"]) <= 5.0]
            d = sorted(r["dt_ms"] for r in near)
            el = sum(d) / 1000.0
            L.append(f"| {i} | {m['t_s']:.1f} | {len(d) / el:.1f} | {percentile(d, 99):.1f} | {d[-1]:.1f} |"
                     f" {sum(r['draws'] for r in near) / el:,.0f} |")
        L.append("")
    return "\n".join(L), s


def compare(base, new):
    L = ["## Comparison (baseline → new)\n", "| Metric | Baseline | New | Change |", "|---|---:|---:|---:|"]
    keys = [("p50", "P50 ms"), ("p95", "P95 ms"), ("p99", "P99 ms"), ("p999", "P99.9 ms"), ("max", "Max ms"),
            ("avg_fps", "Average FPS"), ("low1_fps", "1% low FPS"), ("low01_fps", "0.1% low FPS"),
            ("over_50", "Frames > 50 ms"), ("over_100", "Frames > 100 ms"),
            ("below_55_seconds", "Seconds sustained < 55 FPS"), ("below_45_seconds", "Seconds sustained < 45 FPS"),
            ("below_30_seconds", "Seconds sustained < 30 FPS"), ("compile_ms", "Compile ms"),
            ("readback_wait_ms", "Readback wait ms")]
    for k, label in keys:
        a, b = base.get(k, 0), new.get(k, 0)
        ch = f"{100 * (b - a) / a:+.1f}%" if a else "n/a"
        L.append(f"| {label} | {a:,.2f} | {b:,.2f} | {ch} |")
    L.append("\nSessions differ in length and scene mix; normalize by comparing equivalent scenes"
             " (F1 markers) before attributing changes to a build.\n")
    return "\n".join(L)


def analyze(path, threads_path, top):
    frames_path, threads_path = resolve_paths(path, threads_path)
    frames = load_frames(frames_path)
    threads = load_threads(threads_path)
    windows = build_windows(frames, threads)
    return report(frames_path, frames, windows, threads, top)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session")
    ap.add_argument("--threads")
    ap.add_argument("--compare")
    ap.add_argument("--out")
    ap.add_argument("--json")
    ap.add_argument("--top", type=int, default=15)
    a = ap.parse_args()
    text, summary = analyze(a.session, a.threads, a.top)
    if a.compare:
        _, base = analyze(a.compare, None, a.top)
        text += "\n" + compare(base, summary)
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    else:
        sys.stdout.write(text + "\n")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)


if __name__ == "__main__":
    main()
