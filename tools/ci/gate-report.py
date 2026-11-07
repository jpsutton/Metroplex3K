#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write release-gate/<version>.md from the results of tools/release-gate.sh (see docs/RELEASE-GATE.md).

usage: gate-report.py --version V --commit SHA --out FILE --tests-rc N --conformance-rc N --probe-rc N
                      [--tsv probe_verify.tsv] [--ref-used 0|1] [--shots-dir DIR] [--host TEXT] [--date TEXT]
"""
import argparse
import collections
import csv


def main():
    p = argparse.ArgumentParser()
    for name in ("version", "commit", "out"):
        p.add_argument("--" + name, required=True)
    for name in ("tests-rc", "conformance-rc", "probe-rc"):
        p.add_argument("--" + name, type=int, required=True)
    p.add_argument("--tsv")
    p.add_argument("--ref-used", type=int, default=0)
    p.add_argument("--shots-dir", default="")
    p.add_argument("--host", default="")
    p.add_argument("--date", default="")
    a = p.parse_args()

    groups = collections.OrderedDict()
    fails = []
    if a.tsv:
        with open(a.tsv, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f, delimiter="\t"):
                g = groups.setdefault(row["group"], collections.Counter())
                g[row["status"]] += 1
                if row["status"] == "FAIL":
                    fails.append(row)

    ok = a.tests_rc == 0 and a.conformance_rc == 0 and a.probe_rc == 0 and not fails
    out = [
        f"# Release gate report {a.version}",
        "",
        f"Version: {a.version}",
        f"Commit: {a.commit}",
        f"Date: {a.date}",
        f"Host: {a.host}",
        "",
        "## Steps",
        "",
        "| Step | Result |",
        "|------|--------|",
        f"| Unit tests (`ctest`) | {'PASS' if a.tests_rc == 0 else 'FAIL'} |",
        f"| `conformance --strict` | {'PASS' if a.conformance_rc == 0 else 'FAIL'} |",
        f"| `probe_verify` | {'PASS' if a.probe_rc == 0 else 'FAIL'} |",
        "",
        "## probe_verify groups",
        "",
        "| Group | PASS | FAIL | INFO |",
        "|-------|------|------|------|",
    ]
    for g, c in groups.items():
        out.append(f"| {g} | {c['PASS']} | {c['FAIL']} | {c['INFO']} |")
    if not groups:
        out.append("| (no rows) | 0 | 0 | 0 |")
    out += [
        "",
        f"Reference saves for the l3 population check: {'used' if a.ref_used else 'not used (l3 not run)'}.",
    ]
    if a.shots_dir:
        out.append(f"Screenshots written to `{a.shots_dir}` for comparison with the original (not part of the pass/fail result).")
    if fails:
        out += ["", "## Failed checks", ""]
        for r in fails:
            out.append(f"- `{r['group']}` {r['check']}: found {r['found']}, expected {r['expected']}")
    out += ["", f"Result: {'PASS' if ok else 'FAIL'}", ""]
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("\n".join(out))
    return 0 if ok else 1


raise SystemExit(main())
