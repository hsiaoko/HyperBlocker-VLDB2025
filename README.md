# Hyperdeduce

Hyperdeduce is a deductive reasoning library over CSV relations. You give it one
or more relations and one or more Datalog rules. It builds a data-aware execution
plan and evaluates the rules to a fixpoint on multiple CPUs or multiple GPUs. You
read the derived relations back or write them to CSV.

```
parent("alice", "bob").
parent("bob", "carol").
ancestor(x, y) :- parent(x, y).
ancestor(x, z) :- parent(x, y), ancestor(y, z).
```

Loading `parent.csv` and these rules derives `ancestor` as the transitive closure
of `parent`.

## Build

You need a C++17 compiler, CMake 3.18 or newer, TBB, gflags, and GoogleTest. The
GPU backend also needs the CUDA toolkit. rapidcsv (CSV parsing) and yaml-cpp
(legacy YAML rules) are submodules.

```
git submodule update --init third_party/rapidcsv third_party/yaml-cpp
cmake -S . -B build
cmake --build build -j
(cd build && ctest)
```

Set `-DHYPERDEDUCE_WITH_CUDA=OFF` for a CPU-only build. The default CUDA
architecture is `86`; override `CMAKE_CUDA_ARCHITECTURES` for other GPUs.

## Run

The CLI loads every `<predicate>.csv` in a directory as a base relation, reads a
Datalog rules file, and writes the derived relations.

```
build/examples/hyperdeduce \
  --data_dir tests/data/family \
  --rules tests/data/family/rules.dl \
  --output_dir out \
  --backend cpu --explain
```

Use `--backend gpu --gpus 4` to run on four GPUs. `--explain` prints the chosen
execution plan with estimated row counts. `--has_header` treats the first CSV row
as column names.

## Rule syntax

Hyperdeduce parses a Datalog subset. A rule is `head :- body.` and a fact is
`head.`. A bare identifier in a term position is a variable. Constants are numbers
or double-quoted strings. The body mixes relational atoms and infix comparisons
`=`, `!=`, `<`, `<=`, `>`, `>=`. Line comments start with `//` or `%`.

```
adult(x) :- person(x, age), age >= 18.
sibling(x, y) :- parent(p, x), parent(p, y), x != y.
```

Recursion is evaluated with semi-naive iteration. Mutually recursive predicates
are grouped into strata and evaluated in dependency order.

### Similarity joins

String similarity is a binary join predicate, not a post-filter. The measures
ported from the HyperBlocker paper artifact are `jaro`, `jaro_winkler`,
`jaccard`, and `levenshtein` (normalized ratio). The trailing number is a
threshold parameter.

```
match(a, b) :- docA(a, t1), docB(b, t2), jaro_winkler(t1, t2, 0.89).
```

A similarity atom is a theta-join with a similarity condition. The optimizer
schedules it by cost like other joins, preferring cheap equality joins first,
and runs it on both the CPU and GPU backends.

### Legacy YAML rules

`--rules` accepts Datalog text (`.dl`) or an original HyperBlocker YAML rule
(`.yaml`) directly. The input module dispatches by file extension: each format
is a `RuleFrontend` that parses into the shared rule AST, so the engine and CLI
never branch on the format. The YAML frontend decodes the paper format
(`Preconditions` equalities, similarity pairs, thresholds, and the consequence
pair); equality columns become a shared variable so the optimizer blocks on them
first. Load the referenced tables before adding the rule.

```
build/examples/hyperdeduce --data_dir . --rules rule.yaml --backend gpu --has_header
```

`--tables tableA,tableB` maps YAML table indices 0 and 1 to relation names.
`tools/yaml2datalog.py` remains as a standalone YAML-to-Datalog translator if you
want the Datalog text explicitly.

## Library API

```cpp
#include "hyperdeduce/engine/engine.h"
#include "hyperdeduce/backend/cpu/cpu_backend.h"

hyperdeduce::Engine engine;
engine.LoadCsv("parent.csv", "parent");
engine.AddRules("ancestor(x, y) :- parent(x, y).\n"
                "ancestor(x, z) :- parent(x, y), ancestor(y, z).");
auto plan = engine.BuildPlan();   // inspectable execution plan
hyperdeduce::CpuBackend backend;  // or hyperdeduce::GpuBackend(4)
engine.Execute(&backend);
const hyperdeduce::Relation* ancestor = engine.GetRelation("ancestor");
```

## Architecture

The domain is encoded in types and graphs. Relations are columnar with 64-bit
cells; strings are interned in a global dictionary so every join is an integer
join. The parser produces a typed AST, the program model builds the predicate
dependency graph and strata, and the optimizer compiles each rule into a physical
operator tree (Scan, Filter, HashJoin, Project, Dedup) with a data-aware join
order. A backend interprets that plan; the CPU backend uses TBB and the GPU
backend uses CUDA with thrust. The engine owns the catalog and runs the fixpoint.

See [docs/refactor/plan.md](docs/refactor/plan.md) for the full design and the
extension points (builtin predicates, join algorithms, backends, loaders).

## Testing

`ctest` runs the unit tests, including GPU-to-CPU equivalence on one and four
devices. `tools/oracle.py` is an independent naive-fixpoint evaluator, and
`tools/crosscheck.py` compares the engine against it:

```
python3 tools/crosscheck.py build/examples/hyperdeduce \
  tests/data/social tests/data/social/rules.dl /tmp/xcheck gpu 4
```

## Paper artifact

This repository began as HyperBlocker, a GPU entity-blocking system published at
VLDB 2025. That code is preserved at tag `vldb2025-paper`. The `main` branch is
the hyperdeduce rewrite.