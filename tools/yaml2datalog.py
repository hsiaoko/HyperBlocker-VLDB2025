#!/usr/bin/env python3
"""Translate an original HyperBlocker YAML rule into hyperdeduce Datalog.

The YAML encoding follows core/common/yaml_config.h at tag vldb2025-paper:
  Relations:  [[table_idx, col, ...], ...]
  Equalities: [[table_l, table_r, col_l, col_r, ...]]  (column pairs optional)
  Sim:        [[table_l, table_r, lcol1, rcol1, lcol2, rcol2, ...]]
  Threshold:  [[table_l, table_r, t1, t2, ...]]  (t_k pairs with Sim column pair k)
  Conseq.Equality: [table_l, table_r, col_l, col_r]  (output pair)

The original SIM predicate uses Jaro-Winkler. Equality columns become a shared
Datalog variable so the optimizer blocks on them first (equalities-first).

Usage:
  yaml2datalog.py RULE.yaml TABLE_A.csv TABLE_B.csv -o rules.dl
"""
import argparse
import csv
import sys

import yaml


def column_count(path):
    with open(path, newline="") as handle:
        header = next(csv.reader(handle))
    return len(header)


def translate(rule, ncols, table_names, name, measure):
    pre = rule["Preconditions"]
    eq = pre.get("Equalities", [[]])[0]
    sim = pre["Sim"][0]
    threshold = pre["Threshold"][0]
    con = rule["Conseq"]["Equality"]

    # One variable per (table, column); equality columns share a variable.
    variables = {}
    for table in (0, 1):
        for col in range(ncols[table]):
            variables[(table, col)] = "%s%d" % ("ab"[table], col)

    for k in range(2, len(eq), 2):
        tl, tr = eq[0], eq[1]
        shared = "k%d" % ((k - 2) // 2)
        variables[(tl, eq[k])] = shared
        variables[(tr, eq[k + 1])] = shared

    lines = []
    head = "%s(%s, %s)" % (name, variables[(con[0], con[2])],
                           variables[(con[1], con[3])])
    lines.append("%s :-" % head)
    for table in (0, 1):
        args = ", ".join(variables[(table, col)] for col in range(ncols[table]))
        lines.append("  %s(%s)," % (table_names[table], args))

    sim_tl, sim_tr = sim[0], sim[1]
    sim_atoms = []
    for k in range(2, len(sim), 2):
        pair = (k - 2) // 2
        if 2 + pair >= len(threshold):
            sys.exit("threshold missing for similarity pair %d" % pair)
        left = variables[(sim_tl, sim[k])]
        right = variables[(sim_tr, sim[k + 1])]
        sim_atoms.append("  %s(%s, %s, %s)" %
                         (measure, left, right, threshold[2 + pair]))
    lines.extend(atom + "," for atom in sim_atoms[:-1])
    if sim_atoms:
        lines.append(sim_atoms[-1] + ".")
    else:
        lines[-1] = lines[-1].rstrip(",") + "."
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rule", help="original YAML rule file")
    parser.add_argument("table_a", help="table A CSV (for column count)")
    parser.add_argument("table_b", help="table B CSV (for column count)")
    parser.add_argument("-o", "--output", help="output Datalog file (default stdout)")
    parser.add_argument("--name", default="match", help="head predicate name")
    parser.add_argument("--measure", default="jaro_winkler",
                        help="similarity measure (default jaro_winkler)")
    parser.add_argument("--name-a", default="tableA", help="table A relation name")
    parser.add_argument("--name-b", default="tableB", help="table B relation name")
    args = parser.parse_args()

    with open(args.rule) as handle:
        rule = yaml.safe_load(handle)
    ncols = [column_count(args.table_a), column_count(args.table_b)]
    text = translate(rule, ncols, [args.name_a, args.name_b], args.name,
                     args.measure)
    if args.output:
        with open(args.output, "w") as handle:
            handle.write(text)
        print("wrote %s" % args.output)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()