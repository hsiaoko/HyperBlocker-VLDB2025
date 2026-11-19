# Hyperdeduce architecture

Hyperdeduce is a deductive reasoning engine over CSV relations. You give it
relations and Datalog rules. It compiles a data-aware physical execution plan
and evaluates the rules to a fixpoint on multiple CPUs or multiple GPUs.

## Data flow

```
CSV --io/csv--> Catalog (columnar Relation + Dictionary)
                     |
rules .dl/.yaml --input frontend--> Rule AST --logic/program--> strata
                     |                    |
                     |              plan/optimizer + stats
                     |                    v
                     |              PhysicalOp tree (per rule)
                     |                    |
                     +-> engine (semi-naive fixpoint driver)
                             |  each round: snapshot -> backend.Execute(plan) -> new facts
                             v
                       backend (CPU/TBB or GPU/CUDA)
                             v
                       derived Relation -> Catalog --io/csv--> output
```

## Layers

### Interface

`engine/Engine` is the library facade. `LoadCsv`, `AddRules`, `BuildPlan`,
`Execute`, `GetRelation`. `examples/hyperdeduce.cc` is the CLI. It selects a
rule frontend by file extension through the registry, so it never branches on
the format itself.

### Input

`input/rule_frontend` defines the `RuleFrontend` interface and the
`FrontendRegistry`. `DatalogFrontend` and `YamlFrontend` derive from it and both
produce the same `Rule` AST. Adding a format means deriving a frontend and
registering it once. `parser/` holds the Datalog lexer, recursive-descent
parser, and `ast.h` (Term, Atom, Rule). `io/csv` loads and writes CSV with
rapidcsv and infers column types.

### Representation

`base/` holds `Status`/`StatusOr` (no exceptions) and the 64-bit `Cell` with
`DataType`. `catalog/` holds the `Dictionary` (string to integer id), the
columnar `Relation`, the `Catalog` that owns every relation and the dictionary,
and `row_key` (row hashing for dedup).

### Analysis

`logic/program` validates safety and arity, builds the predicate dependency
graph, and groups derived predicates into strata with Tarjan SCC. It marks
EDB versus IDB and recursive predicates.

### Planning

`plan/stats` computes row counts and per-column distinct counts.
`plan/optimizer` compiles each rule into a physical operator tree, pushes
constants into selections, and orders joins by cost (equality before similarity
before cross product). `plan/physical_plan` is the operator IR (Scan, Filter,
HashJoin, SimilarityJoin, Project, Dedup) plus `Explain()`. `builtin/` holds the
`BuiltinRegistry` (comparisons and similarity measures register at one point)
and the similarity measures (Jaro, Jaro-Winkler, Jaccard, Levenshtein).

### Execution

`backend/backend` defines the `Backend` interface and the read-only
`ExecutionContext`. `backend/cpu` uses TBB. `backend/gpu` uses CUDA and thrust.
`engine` owns all catalog mutation and runs the semi-naive fixpoint.

## Invariants

1. Every cell is 64 bits and strings are dictionary-encoded, so every join is
   an integer join and one plan runs on both CPU and GPU.
2. The plan is data and a backend is a pure function `Execute(plan, snapshots)
   -> Relation`. It never mutates the catalog. The engine owns mutation. This
   is what makes backends pluggable.
3. Similarity is a binary join operator, a sibling of HashJoin whose theta is
   `sim >= t`. It can also sit in filter position after an equality join.
4. Evaluation is semi-naive per stratum, with delta relations and row-hash
   dedup, stopping when a round derives nothing new.
5. Multi-GPU shards the driving scan. In a left-deep plan each output row
   derives from exactly one driving row, so the union of shards is exact and
   the engine dedups across shards.
6. Variants converge on an interface plus a registry: input frontends, builtin
   predicates, and backends all follow this shape.

## GPU similarity join

The similarity join parallelizes over pairs, not left rows.

```
SimCountKernel<Code>   one thread per pair, computes the measure, atomicOr into a match bitmap
SimRowCountKernel      one thread per left row, popcounts its bitmap slice -> counts
thrust exclusive_scan  counts -> offsets, total match count
SimFillKernel          one thread per pair, reads the bitmap, atomicAdd a counter for a slot, writes the row
```

The measure is templated on `Code` (0 Jaro, 1 Jaro-Winkler, 2 Jaccard,
3 Levenshtein) so the compiler drops the other branches. Strings are capped at
512 characters; the host reports an error past the limit rather than truncating.
The measure is computed once: the count pass writes the bitmap and the fill pass
reads it.

The dominant limiter was parallelism. A left-row-parallel kernel launched only
as many threads as left rows, leaving most of the GPU idle. Pair-parallel
kernels fill the device. Local-memory spill was ruled out as the limiter by
measurement. On the full 168M-pair DBLP-Google dataset the GPU runs about 4.0s
on one card and 2.3s on four, versus 22.8s on CPU, all producing 3260 matches.
The GPU operator time is bimodal (~40ms or ~88ms on a 4M-pair subset) from device
state, so report the median and the range, not a single run.

## Extension seams

| To add | Where |
| --- | --- |
| A rule format | derive `RuleFrontend` and register |
| A predicate (incl. learned/ML) | `BuiltinRegistry::Register` |
| An execution backend | implement `Backend` |
| A join algorithm | a new `PhysicalOp` plus an optimizer rule |
| A data format (not yet built) | a `RelationLoader` interface |

## Current limits

Positive Datalog only; negation and aggregation are not implemented. Similarity
score columns and fuzzy/weighted fixpoint are future work. 0-ary relations are
not evaluated. Data loading is CSV-only and not yet behind an interface.