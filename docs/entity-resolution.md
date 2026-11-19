# Entity resolution with hyperdeduce

This guide walks through entity resolution: finding records in two tables that
refer to the same real-world entity. It is the running example because it uses
every part of the engine, equality blocking, similarity joins, and a fixpoint
free evaluation, and it shows both rule formats, Datalog and the original
HyperBlocker YAML.

The complete example lives in `examples/entity_resolution/`. Commands below run
from the repository root after building (`cmake --build build -j`).

## The problem

You have two publication tables, DBLP and ACM, and you want the pairs of records
that describe the same paper. Titles and author lists are noisy strings, so exact
equality is too strict. The classic recipe is:

1. Block on a cheap equality (here, publication year) to avoid comparing every
   pair.
2. Within each block, require string similarity above a threshold on title and
   authors.

hyperdeduce expresses this as a Datalog rule and compiles it to a plan that does
the equality join first, then the similarity joins.

## The data

`examples/entity_resolution/dblp.csv` and `acm.csv` share the schema
`id,title,authors,venue,year`:

```
id,title,authors,venue,year
d1,adaptive query processing,m smith jones,vldb,2020
d2,graph indexing techniques,a b c,dasfaa,2019
d3,entity resolution survey,p q r,sigmod,2020
```

```
id,title,authors,venue,year
a1,adaptive query procesing,m smith jones,vldb,2020
a2,graph index techniques,a b c,dasfaa,2019
a3,entity resolution survey,p q r,sigmod,2021
```

`d1`/`a1` and `d2`/`a2` are near-duplicates with typos. `d3`/`a3` have identical
titles but different years, so the year block should reject them.

## The rule in Datalog

`examples/entity_resolution/rule.dl`:

```
match(dblp_id, acm_id) :-
  dblp(dblp_id, t1, a1, v1, year),
  acm(acm_id, t2, a2, v2, year),
  jaro_winkler(t1, t2, 0.81),
  jaro_winkler(a1, a2, 0.79).
```

The shared variable `year` in both atoms is the equality block: the optimizer
joins `dblp` and `acm` on equal year first. `jaro_winkler(x, y, t)` is a binary
similarity predicate, true when the Jaro-Winkler similarity of `x` and `y` is at
least `t`. The measures available are `jaro`, `jaro_winkler`, `jaccard`, and
`levenshtein` (normalized ratio).

Run it:

```
build/examples/hyperdeduce \
  --data_dir examples/entity_resolution \
  --rules examples/entity_resolution/rule.dl \
  --output_dir out --backend cpu --has_header --explain
```

`--has_header` tells the loader the first CSV row is column names. The result is
`out/match.csv`:

```
d1,a1
d2,a2
```

`d3`/`a3` is absent because the years differ (2020 vs 2021), so the equality
block rejects it before any similarity is computed.

## The same rule in YAML

hyperdeduce also reads the original HyperBlocker YAML rule format directly.
`examples/entity_resolution/rule.yaml`:

```
Preconditions:
  Relations:
    - [0, 0, 1, 2, 3, 4]
    - [1, 0, 1, 2, 3, 4]
  Equalities:
    - [0, 1, 4, 4]
  Sim:
    - [0, 1, 1, 1, 2, 2]
  Threshold:
    - [0, 1, 0.81, 0.79]
Conseq:
  Option: Equality
  Equality: [0, 1, 0, 0]
```

Tables are referenced by index, 0 and 1, and columns by position
(`0=id, 1=title, 2=authors, 3=venue, 4=year`). `Equalities [0,1,4,4]` means
table 0 column 4 equals table 1 column 4 (year). `Sim [0,1,1,1,2,2]` pairs
title with title and authors with authors; `Threshold` gives 0.81 and 0.79.
`Conseq.Equality [0,1,0,0]` outputs (table 0 id, table 1 id). The measure is
Jaro-Winkler.

Map the table indices to relation names with `--tables` (the names come from the
CSV file stems, `dblp` and `acm`):

```
build/examples/hyperdeduce \
  --data_dir examples/entity_resolution \
  --rules examples/entity_resolution/rule.yaml \
  --tables dblp,acm \
  --output_dir out --backend cpu --has_header
```

This produces the same `d1,a1` / `d2,a2`. The CLI dispatches by file extension,
so `.dl` and `.yaml` both work through `--rules`; no manual conversion is needed.
`tools/yaml2datalog.py` is available if you want the Datalog text explicitly.

## Reading the plan and the report

`--explain` prints the physical plan. For this rule it is, bottom up: scan
`dblp`, scan `acm`, `HashJoin` on the shared year (the block), then two
`jaro_winkler` similarity filters, then project to the ids and dedup. The
equality join runs before the expensive similarity, which is the point of
blocking.

The run report (on by default, `--report=false` to hide) shows per-relation load
stats, the strata, per-stratum rounds and facts derived, GPU kernel time and
sampled utilization when on GPU, and load/plan/eval/total timing.

## On the GPU

Swap the backend and pin a free device (the machine may be shared):

```
CUDA_VISIBLE_DEVICES=2 build/examples/hyperdeduce \
  --data_dir examples/entity_resolution \
  --rules examples/entity_resolution/rule.dl \
  --output_dir out --backend gpu --gpus 1 --has_header
```

`--gpus 4` shards the work across four devices. Results are identical to CPU. On
the full DBLP-ACM benchmark (about 168M candidate pairs) the GPU similarity join
runs in a few seconds versus tens of seconds on CPU; see
`docs/refactor/` and the decision trail for the measured numbers.

## Multiple queries share work

If you run several similarity measures over the same tables, put each rule in its
own file in a folder and pass the folder to `--rules`:

```
match_jw(a,b)  :- dblp(a,t1,x1,v1,y), acm(b,t2,x2,v2,y), jaro_winkler(t1,t2,0.81), jaro_winkler(x1,x2,0.79).
match_jac(a,b) :- dblp(a,t1,x1,v1,y), acm(b,t2,x2,v2,y), jaccard(t1,t2,0.5), jaccard(x1,x2,0.5).
```

```
build/examples/hyperdeduce --data_dir examples/entity_resolution \
  --rules my_rules_folder --tables dblp,acm --backend cpu --has_header --explain
```

Multiple-query optimization detects the shared relational body, materializes the
blocked candidate join once as a `__shared_` base, and feeds each query from it.
Variables are normalized, so rules that differ only in variable names still
share. `--mqo=false` disables it.

## From the library

The same flow in C++:

```cpp
#include "hyperdeduce/engine/engine.h"
#include "hyperdeduce/backend/cpu/cpu_backend.h"

hyperdeduce::Engine engine;
engine.LoadCsv("dblp.csv", "dblp", {.has_header = true});
engine.LoadCsv("acm.csv", "acm", {.has_header = true});
engine.AddRules("match(a,b) :- dblp(a,t1,x1,v1,y), acm(b,t2,x2,v2,y),"
                " jaro_winkler(t1,t2,0.81), jaro_winkler(x1,x2,0.79).");
auto plan = engine.BuildPlan();          // inspectable execution plan
hyperdeduce::CpuBackend backend;         // or GpuBackend(4)
engine.Execute(&backend);
const auto* match = engine.GetRelation("match");
```

`LoadRelation` dispatches by extension through the loader registry, and
`AddRules(source, frontend, context)` accepts any registered frontend, so YAML
loads the same way.