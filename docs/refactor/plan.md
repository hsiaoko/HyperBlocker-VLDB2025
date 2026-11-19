# Hyperdeduce rewrite plan

This document records the target architecture and the phase sequence for turning
the HyperBlocker VLDB 2025 artifact into `hyperdeduce`, a deductive reasoning
library over CSV relations. The decision trail lives in
`.audit/hyperdeduce-rewrite.tsv`. The paper code is preserved at tag
`vldb2025-paper`.

## What the library does

You give it one or more CSV relations and one or more Datalog rules. It builds an
execution plan, then evaluates the rules to a fixpoint on multiple CPUs or
multiple GPUs. You read the derived relations back, or write them to CSV.

```
parent("alice", "bob").
parent("bob", "carol").
ancestor(x, y) :- parent(x, y).
ancestor(x, z) :- parent(x, y), ancestor(y, z).
```

A bare identifier in a term position is a variable. Constants are numbers or
double-quoted strings, so facts use quoted strings.

Loading `parent.csv` and these rules derives `ancestor` as the transitive
closure of `parent`.

## Done predicate

The rewrite is done when all of these hold on the real artifact.

1. Given CSV relations and recursive Datalog rules, the engine output equals an
   independent reference evaluator on every test program.
2. The execution plan is inspectable through `Explain()` and is data-aware:
   changing relation statistics changes the chosen join order.
3. The CPU backend uses TBB. The GPU backend produces identical results on 1 and
   on 4 GPUs.
4. `ctest` is green in both the CPU-only and the CUDA build.
5. Adding a builtin predicate touches one registration point.

## Architecture

The domain is encoded in types and graphs, not in scattered conditionals.

### Values and relations

Every cell is 64 bits. A column has a `DataType` of `kInt64`, `kDouble`, or
`kString`. Integers and doubles store their native bits. Strings store a global
dictionary id, so string equality is integer equality. A `Relation` is a schema
plus one `std::vector<uint64_t>` per column. Relations are immutable once built
and shared through `std::shared_ptr<const Relation>`, so a snapshot is cheap and
safe to read from many threads.

### Rules and programs

The parser turns Datalog text into an AST. A `Term` is a constant or a variable.
An `Atom` is a predicate name plus terms. A `Rule` is a head atom and a body of
relational atoms and builtin comparisons. The program model validates safety
(every head variable appears in a positive body atom) and builds the predicate
dependency graph. Strongly connected components give the evaluation strata.

### Execution plan

For each rule the optimizer builds a left-deep join tree over the body atoms.
Constant arguments become pushed-down selections. Join order is chosen from
relation statistics (row counts and per-column distinct counts) to keep
intermediate results small. The output is a physical operator tree: `Scan`,
`Filter`, `HashJoin`, `Project`, `Dedup`. The plan is data, not code, so
`Explain()` can print it and any backend can interpret it.

### Backends

A `Backend` is a pure function from a physical plan plus input relation snapshots
to an output relation. It does not mutate the catalog. The CPU backend implements
the operators with TBB. The GPU backend implements them with CUDA and partitions
work across devices. The fixpoint driver is backend-agnostic.

### Fixpoint engine

The engine owns the catalog and the mutation. For each stratum it evaluates
non-recursive rules once, then runs semi-naive iteration for recursive
predicates: each round evaluates delta rules, deduplicates new facts against the
predicate, and stops when a round derives nothing new.

### Extension points

- A builtin predicate registers arity, binding requirements, and a CPU evaluator
  (and optionally a GPU evaluator) at one registry.
- A join algorithm is a new physical operator plus an optimizer rule.
- A backend implements the `Backend` interface.
- An input format is a loader that produces a `Relation`.

## Scope of this rewrite

Version one covers positive Datalog, builtin comparisons, recursion through
semi-naive evaluation, and CPU plus multi-GPU execution. Negation, aggregation,
and the old string-similarity predicates are out of scope; the registry and
operator seams leave room for them. Performance tuning is not promised here; any
speedup claim must pass the benchmark checklist first.

## Phases

Each phase ends in a verified, committed state. The CPU backend lands before the
GPU backend so it can serve as the correctness oracle.

1. Scaffold. New CMake with optional CUDA, directories mirroring the namespace,
   gtest and TBB wired in, this plan and the decision trail committed.
2. Core types, CSV loading, dictionary. Tests cover loading and type round-trips.
3. Datalog parser, program model, stratification. Tests cover parsing, errors,
   and recursive strata.
4. Logical and physical plan, optimizer, `Explain()`. Tests show statistics
   changing the join order.
5. CPU backend, semi-naive fixpoint, CLI. Cross-checked against an independent
   Python reference on transitive closure and multi-rule programs.
6. GPU backend. Single GPU cross-checked against the CPU oracle, then 4 GPUs.
7. Cleanup. Delete the old HyperBlocker code, rewrite the README and examples,
   final style pass and verification.