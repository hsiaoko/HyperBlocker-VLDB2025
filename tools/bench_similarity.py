#!/usr/bin/env python3
"""Frozen harness for the similarity-join kernel hillclimb.

Runs the engine alternating CPU/GPU and reports the median similarity-join
operator time over N repeats, with the work count (pairs evaluated, rows out)
and an error check (the derived match count must be identical every run).

Usage:
  bench_similarity.py CLI DATA_DIR RULES REPEATS GPU_DEVICE [--header]

GPU runs pin to one device via CUDA_VISIBLE_DEVICES so a busy shared machine
does not contaminate the kernel timing. HYPERDEDUCE_BENCH=1 makes the engine
print the operator time to stderr.
"""
import os
import re
import statistics
import subprocess
import sys

BENCH = re.compile(
    r"\[bench\] \w+ similarity_join measure=\S+ ms=([\d.]+) pairs=(\d+) out=(\d+)")
MATCH = re.compile(r"match: (\d+) rows")


def run(cli, data, rules, backend, device, header):
    env = dict(os.environ, HYPERDEDUCE_BENCH="1")
    cmd = [cli, "--data_dir", data, "--rules", rules, "--backend", backend]
    if header:
        cmd.append("--has_header")
    if backend == "gpu":
        cmd += ["--gpus", "1"]
        env["CUDA_VISIBLE_DEVICES"] = str(device)
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        raise RuntimeError("run failed (%s): %s" % (backend, proc.stderr[-500:]))
    bench = BENCH.search(proc.stderr)
    match = MATCH.search(proc.stdout)
    if not bench or not match:
        raise RuntimeError("missing bench/match output: %s" % proc.stderr[-500:])
    return (float(bench.group(1)), int(bench.group(2)), int(bench.group(3)),
            int(match.group(1)))


def summarize(name, samples):
    times = [s[0] for s in samples]
    med = statistics.median(times)
    print("%s: median %.3f ms  range %.3f..%.3f  n=%d  pairs=%d out=%d" %
          (name, med, min(times), max(times), len(times), samples[0][1],
           samples[0][2]))
    return med


def main():
    cli, data, rules, repeats, device = sys.argv[1:6]
    repeats = int(repeats)
    header = "--header" in sys.argv[6:]
    cpu, gpu = [], []
    matches = set()
    for _ in range(repeats):
        c = run(cli, data, rules, "cpu", device, header)
        g = run(cli, data, rules, "gpu", device, header)
        cpu.append(c)
        gpu.append(g)
        matches.add(c[3])
        matches.add(g[3])
    cpu_med = summarize("cpu", cpu)
    gpu_med = summarize("gpu", gpu)
    errors = 0 if len(matches) == 1 else 1
    print("match_count=%s  error_count=%d" %
          (sorted(matches) if len(matches) == 1 else "VARIES", errors))
    if errors == 0 and cpu_med > 0:
        print("gpu/cpu speedup=%.2fx (>1 means GPU faster)" % (cpu_med / gpu_med))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()