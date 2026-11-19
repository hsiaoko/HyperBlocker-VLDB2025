#!/usr/bin/env python3
"""Cross-check the hyperdeduce CLI against the independent Python oracle.

Runs the C++ engine and oracle.py on the same base relations and rules, then
compares the derived relation row sets. Exits nonzero on any mismatch.
"""
import csv
import os
import subprocess
import sys


def read_rows(path):
    if not os.path.exists(path):
        return None
    with open(path, newline="") as handle:
        return {tuple(cell.strip() for cell in row) for row in csv.reader(handle)}


def main():
    cli, data_dir, rules, work_dir = sys.argv[1:5]
    out_cpp = os.path.join(work_dir, "cpp")
    out_py = os.path.join(work_dir, "py")
    os.makedirs(out_cpp, exist_ok=True)

    subprocess.run(
        [cli, "--data_dir", data_dir, "--rules", rules,
         "--output_dir", out_cpp, "--backend", "cpu"],
        check=True, stdout=subprocess.DEVNULL)
    subprocess.run(
        [sys.executable, os.path.join(os.path.dirname(__file__), "oracle.py"),
         data_dir, rules, out_py],
        check=True, stdout=subprocess.DEVNULL)

    failures = 0
    for filename in sorted(os.listdir(out_py)):
        if not filename.endswith(".csv"):
            continue
        expected = read_rows(os.path.join(out_py, filename))
        actual = read_rows(os.path.join(out_cpp, filename))
        name = filename[:-4]
        if actual is None:
            print("FAIL %s: engine produced no output" % name)
            failures += 1
        elif actual == expected:
            print("PASS %s: %d rows agree" % (name, len(expected)))
        else:
            missing = expected - actual
            extra = actual - expected
            print("FAIL %s: %d missing, %d extra" % (name, len(missing), len(extra)))
            for row in list(missing)[:5]:
                print("  missing %r" % (row,))
            for row in list(extra)[:5]:
                print("  extra   %r" % (row,))
            failures += 1
    if failures:
        print("%d relation(s) disagree" % failures)
        sys.exit(1)
    print("all derived relations agree with the oracle")


if __name__ == "__main__":
    main()