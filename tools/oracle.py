#!/usr/bin/env python3
"""Independent Datalog reference evaluator.

Parses the same Datalog subset as hyperdeduce and computes the fixpoint with a
naive iteration (re-evaluate every rule until nothing changes). This is a
different algorithm from the engine's semi-naive evaluation, so agreement is
real cross-validation, not a shared implementation. Used by crosscheck.py.
"""
import csv
import os
import re
import sys

COMPARISONS = {"=", "!=", "<", "<=", ">", ">="}


def tokenize(src):
    tokens = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c.isspace():
            i += 1
            continue
        if c == "%" or src[i:i + 2] == "//":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == '"':
            j, buf = i + 1, ""
            escapes = {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}
            while j < n and src[j] != '"':
                if src[j] == "\\":
                    buf += escapes.get(src[j + 1], src[j + 1])
                    j += 2
                else:
                    buf += src[j]
                    j += 1
            tokens.append(("str", buf))
            i = j + 1
            continue
        if c.isdigit() or (c == "-" and i + 1 < n and src[i + 1].isdigit()):
            m = re.match(r"-?\d+(\.\d+)?([eE][+-]?\d+)?", src[i:])
            text = m.group(0)
            i += len(text)
            value = float(text) if ("." in text or "e" in text.lower()) else int(text)
            tokens.append(("num", value))
            continue
        if c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", src[i:])
            tokens.append(("ident", m.group(0)))
            i += len(m.group(0))
            continue
        two = src[i:i + 2]
        if two in (":-", "!=", "<=", ">="):
            tokens.append(("sym", two))
            i += 2
            continue
        if c in "(),.=<>":
            tokens.append(("sym", c))
            i += 1
            continue
        raise ValueError("bad character %r" % c)
    tokens.append(("end", ""))
    return tokens


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.i = 0

    def peek(self):
        return self.tokens[self.i]

    def advance(self):
        token = self.tokens[self.i]
        self.i += 1
        return token

    def expect(self, kind, text=None):
        token = self.advance()
        if token[0] != kind or (text is not None and token[1] != text):
            raise ValueError("expected %s %r, got %r" % (kind, text, token))
        return token

    def term(self):
        kind, value = self.peek()
        if kind == "ident":
            self.advance()
            return ("var", value)
        if kind in ("num", "str"):
            self.advance()
            return ("const", value)
        raise ValueError("expected term, got %r" % (self.peek(),))

    def atom(self):
        name = self.expect("ident")[1]
        terms = []
        if self.peek() == ("sym", "("):
            self.advance()
            if self.peek() != ("sym", ")"):
                while True:
                    terms.append(self.term())
                    if self.peek() == ("sym", ","):
                        self.advance()
                    else:
                        break
            self.expect("sym", ")")
        return (name, terms)

    def literal(self):
        if self.peek()[0] == "ident" and self.tokens[self.i + 1] == ("sym", "("):
            return self.atom()
        first = self.term()
        if self.peek()[0] == "sym" and self.peek()[1] in COMPARISONS:
            op = self.advance()[1]
            second = self.term()
            return (op, [first, second])
        if first[0] == "var":
            return (first[1], [])
        raise ValueError("expected comparison after constant")

    def rule(self):
        head = self.atom()
        body = []
        if self.peek() == ("sym", ":-"):
            self.advance()
            while True:
                body.append(self.literal())
                if self.peek() == ("sym", ","):
                    self.advance()
                else:
                    break
        self.expect("sym", ".")
        return (head, body)

    def program(self):
        rules = []
        while self.peek()[0] != "end":
            rules.append(self.rule())
        return rules


def parse_program(src):
    return Parser(tokenize(src)).program()


def infer_column(cells):
    def all_int():
        for c in cells:
            try:
                int(c)
            except ValueError:
                return False
        return True

    def all_float():
        for c in cells:
            try:
                float(c)
            except ValueError:
                return False
        return True

    if cells and all_int():
        return [int(c) for c in cells]
    if cells and all_float():
        return [float(c) for c in cells]
    return list(cells)


def load_relations(data_dir):
    relations = {}
    for filename in sorted(os.listdir(data_dir)):
        if not filename.endswith(".csv"):
            continue
        name = filename[:-4]
        with open(os.path.join(data_dir, filename), newline="") as handle:
            rows = list(csv.reader(handle))
        if not rows:
            relations[name] = set()
            continue
        ncols = len(rows[0])
        columns = [[row[c] for row in rows] for c in range(ncols)]
        typed = [infer_column(col) for col in columns]
        relations[name] = set(zip(*typed)) if typed else set()
    return relations


def resolve(term, binding):
    if term[0] == "var":
        return binding[term[1]]
    return term[1]


def apply_op(op, a, b):
    if op == "=":
        return a == b
    if op == "!=":
        return a != b
    try:
        if op == "<":
            return a < b
        if op == "<=":
            return a <= b
        if op == ">":
            return a > b
        if op == ">=":
            return a >= b
    except TypeError:
        return False
    return False


def eval_rule(rule, relations):
    head, body = rule
    bindings = [{}]
    for pred, terms in body:
        if pred in COMPARISONS:
            kept = []
            for binding in bindings:
                a = resolve(terms[0], binding)
                b = resolve(terms[1], binding)
                if apply_op(pred, a, b):
                    kept.append(binding)
            bindings = kept
            continue
        relation = relations.get(pred, set())
        kept = []
        for binding in bindings:
            for row in relation:
                if len(row) != len(terms):
                    continue
                extended = dict(binding)
                ok = True
                for term, value in zip(terms, row):
                    if term[0] == "var":
                        if term[1] in extended:
                            if extended[term[1]] != value:
                                ok = False
                                break
                        else:
                            extended[term[1]] = value
                    elif term[1] != value:
                        ok = False
                        break
                if ok:
                    kept.append(extended)
        bindings = kept
    out = set()
    for binding in bindings:
        out.add(tuple(resolve(term, binding) for term in head[1]))
    return out


def evaluate(rules, relations):
    relations = {name: set(rows) for name, rows in relations.items()}
    while True:
        changed = False
        for rule in rules:
            head = rule[0][0]
            derived = eval_rule(rule, relations)
            target = relations.setdefault(head, set())
            new = derived - target
            if new:
                target |= new
                changed = True
        if not changed:
            break
    return relations


def cell_text(value):
    if isinstance(value, float):
        return repr(value)
    return str(value)


def main():
    data_dir, rules_path, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    with open(rules_path) as handle:
        rules = parse_program(handle.read())
    relations = load_relations(data_dir)
    result = evaluate(rules, relations)
    derived = {rule[0][0] for rule in rules}
    os.makedirs(out_dir, exist_ok=True)
    for name in sorted(derived):
        rows = sorted(result.get(name, set()), key=lambda r: tuple(cell_text(v) for v in r))
        with open(os.path.join(out_dir, name + ".csv"), "w", newline="") as handle:
            writer = csv.writer(handle)
            for row in rows:
                writer.writerow([cell_text(v) for v in row])
        print("%s: %d rows" % (name, len(rows)))


if __name__ == "__main__":
    main()