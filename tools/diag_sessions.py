#!/usr/bin/env python3
"""Session handling for the stacked NHL24 PC diagnostic logs.

The game appends every run to the same nhl24_frames.csv / nhl24_threads.csv / nhl24_diag.log
(NHLPORT_DIAG_APPEND=0 turns that off). Each CSV row starts with a `session` column holding the
run's local start time (e.g. 20261127-153100), and the diag log starts each run with
"# ===== session <id> =====". When a new build changes the CSV columns, the game moves the old
file aside as nhl24_frames.until-<id>.csv and starts a new one, so one file never mixes layouts.

Older per-run logs without a session column are treated as one session named "-".

Session selectors (--session in the report tools):
  latest        the most recent run (default)
  all           every run in the file
  last:N        the N most recent runs
  ID[,ID...]    explicit runs; an ID may be shortened to any unique part of it
                (e.g. 1531 for 20261127-153100)

Usage:
  python tools/diag_sessions.py LOG_DIR      list the runs stacked in LOG_DIR

Standard library only.
"""

import csv
import os
import re
import sys

LEGACY = "-"
SESSION_LINE = re.compile(r"^# ===== session (\S+) =====")
BUILD_RE = re.compile(r"([0-9a-f]{7,12}(?:-dirty)?)\)?\s*$")


def scan(path):
    """Ordered [(session_id, row_count, duration_s)] for a stacked CSV, in one pass."""
    if not path or not os.path.exists(path):
        return []
    order, counts, last_t = [], {}, {}
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        header = next(reader, [])
        has_session = bool(header) and header[0] == "session"
        t_idx = header.index("t_s") if "t_s" in header else None
        for row in reader:
            if not row:
                continue
            sid = row[0] if has_session else LEGACY
            if sid not in counts:
                counts[sid] = 0
                last_t[sid] = 0.0
                order.append(sid)
            counts[sid] += 1
            if t_idx is not None and t_idx < len(row):
                try:
                    last_t[sid] = max(last_t[sid], float(row[t_idx]))
                except ValueError:
                    pass
    return [(sid, counts[sid], last_t[sid]) for sid in order]


def select(spec, ids):
    """Resolves a selector (see module doc) against the ordered session ids."""
    if not ids:
        return []
    spec = (spec or "latest").strip()
    if spec == "latest":
        return [ids[-1]]
    if spec == "all":
        return list(ids)
    if spec.startswith("last:"):
        n = max(1, int(spec[5:]))
        return list(ids[-n:])
    chosen = []
    for part in (p.strip() for p in spec.split(",") if p.strip()):
        exact = [i for i in ids if i == part]
        hits = exact or [i for i in ids if part in i]
        if not hits:
            raise SystemExit(f"no session matches '{part}'; sessions: {', '.join(ids)}")
        if len(hits) > 1:
            raise SystemExit(f"'{part}' matches several sessions: {', '.join(hits)}")
        if hits[0] not in chosen:
            chosen.append(hits[0])
    return [i for i in ids if i in chosen]


def iter_rows(path, sessions=None):
    """Yields csv.DictReader rows (with their 'session' key) from the chosen sessions.

    sessions=None yields everything. Legacy files without a session column yield every row
    with session '-'."""
    wanted = None if sessions is None else set(sessions)
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        legacy = "session" not in (reader.fieldnames or [])
        for row in reader:
            if legacy:
                row["session"] = LEGACY
            if wanted is None or row.get("session") in wanted:
                yield row


def fieldnames(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        return csv.DictReader(f).fieldnames or []


def log_lines(path, sessions=None):
    """Yields the diag-log lines (without newline) that belong to the chosen sessions."""
    wanted = None if sessions is None else set(sessions)
    current = LEGACY
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            m = SESSION_LINE.match(line)
            if m:
                current = m.group(1)
            if wanted is None or current in wanted:
                yield line


def log_info(path):
    """{session_id: {'port', 'build', 'config', 'env', 'started'}} from the diag-log headers."""
    info = {}
    if not path or not os.path.exists(path):
        return info
    current = LEGACY
    for line in log_lines(path):
        m = SESSION_LINE.match(line)
        if m:
            current = m.group(1)
            continue
        if not line.startswith("# "):
            continue
        d = info.setdefault(current, {})
        for key, prefix in (("port", "# port: "), ("config", "# config: "),
                            ("env", "# environment overrides:"), ("started", "# started ")):
            if line.startswith(prefix) and key not in d:
                d[key] = line[len(prefix):].strip()
        if "port" in d and "build" not in d:
            b = BUILD_RE.search(d["port"])
            d["build"] = b.group(1) if b else d["port"][-24:]
    return info


def label(sid):
    """20261127-153100 -> 2026-11-27 15:31:00; anything else unchanged."""
    m = re.fullmatch(r"(\d{4})(\d\d)(\d\d)-(\d\d)(\d\d)(\d\d)", sid)
    return f"{m[1]}-{m[2]}-{m[3]} {m[4]}:{m[5]}:{m[6]}" if m else sid


def list_text(frames_path, log_path=None):
    rows = scan(frames_path)
    info = log_info(log_path)
    out = [f"{len(rows)} session(s) in {frames_path}", "",
           f"{'session':<16} {'started':<19} {'frames':>8} {'min':>6}  build / env"]
    for sid, n, dur in rows:
        d = info.get(sid, {})
        env = d.get("env", "")
        env = "" if env in ("", "none") else f"  [{env.strip()}]"
        out.append(f"{sid:<16} {label(sid):<19} {n:>8} {dur / 60:>6.1f}  {d.get('build', '?')}{env}")
    return "\n".join(out)


def resolve_dir(target):
    """(frames_csv, threads_csv, diag_log) for a log directory or a frames CSV path."""
    if os.path.isdir(target):
        base, frames = target, os.path.join(target, "nhl24_frames.csv")
    else:
        base, frames = os.path.dirname(target) or ".", target
    threads = os.path.join(base, "nhl24_threads.csv")
    log = os.path.join(base, "nhl24_diag.log")
    return (frames if os.path.exists(frames) else None,
            threads if os.path.exists(threads) else None,
            log if os.path.exists(log) else None)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    frames, _, log = resolve_dir(sys.argv[1])
    if not frames:
        sys.exit(f"no nhl24_frames.csv at {sys.argv[1]}")
    print(list_text(frames, log))


if __name__ == "__main__":
    main()
