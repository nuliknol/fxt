# Jobs Tab Parameter Reference

This document explains every parameter visible in the `Jobs` tab of the Qt GUI client.

The tab now presents clustering in an algorithm-centric way.

Current clustering selections:

- `Threshold graph clustering`
- `Sparse graph clustering`
- `K-medoids`
- `DBSCAN`

Current mining actions:

- `Run Word Mining`
- `Run Phrase Mining`

Not every field is used by every algorithm, and several visible labels are intentionally more descriptive than the underlying server query parameter names.

Examples:

- GUI label `Number of clusters (k)` maps to server parameter `k`
- GUI label `Neighbors per block` maps to server parameter `k_neighbors`
- GUI label `Maximum block mismatch count (theta)` maps to server parameter `theta`

## First Rule

When you click `Run Clustering`, only the parameters relevant to the selected clustering algorithm are sent.

Examples:

- `Threshold graph clustering` uses the threshold formula and `theta`.
- `Sparse graph clustering` uses graph backend controls like `Neighbors per block` and `Final graph backend`.
- `K-medoids` uses `Number of clusters (k)`, `Refinement iterations`, and `Initialization restarts`.
- `DBSCAN` uses `Neighborhood radius (eps)` and `Minimum neighbors (min_pts)`.
- `Run Word Mining` uses only `dataset_id`, `word_min`, `word_max`.
- `Run Phrase Mining` uses only `dataset_id`, `phrase_min`, `phrase_max`.

## Practical Starting Points

If you want safe starting values for graph clustering, use:

- `q = 4`
- `distance_mode = feature_aware`
- `delta = 4`
- `body_gate = 8`
- `range_gate = 8`
- `bull_gate = 2`
- `signchg_gate = 2`
- `graph_k = 8`
- `mutual_knn = 1`
- `graph_max_dist = 0`
- `graph_mode = label_propagation`
- `body_mode = signed_body`
- `sign_penalty = 2.0`
- `same_majority = 0`
- `use_composition = 1`
- all weights = `1.0`

If graph results fragment too much:

- increase `delta`
- increase gates slightly
- increase `graph_k`
- disable `mutual_knn` temporarily

If graph results collapse into very large clusters:

- decrease `graph_k`
- lower `graph_max_dist` from `0` to a real cutoff
- decrease gates
- increase composition weights
- keep `mutual_knn = 1`

## Common Distance Parameters

These appear in the top parameter area because they are used by all clustering methods, but their algorithm meaning changes with the selected method.

### `dataset_id`

The dataset to train on.

Use the dataset ID shown in the `Connection` tab after import.

### `q`

Character length in candles.

Meaning:

- `q = 1`: each character is one candle
- `q = 4`: each character is a 4-candle block
- `q = 5`: each character is a 5-candle block

Effect:

- smaller `q` finds simpler local candle shapes
- larger `q` finds more structured micro-patterns
- larger `q` also makes matching stricter and reduces the number of blocks

Suggested values:

- `3` to `5` for most experiments

## Threshold Graph Controls

These matter only for `Threshold graph clustering`.

### `Coordinate mismatch tolerance`

Server parameter: `delta`

This is the per-coordinate tolerance used by the original mismatch-count distance.

Rule:

- if `abs(X[i] - Y[i]) <= delta`, that coordinate contributes `0`
- if `abs(X[i] - Y[i]) > delta`, that coordinate contributes `1`

Then the whole-block distance is:

- `D(X,Y) = sum_i [ abs(X[i] - Y[i]) > delta ]`

### `Maximum block mismatch count (theta)`

Server parameter: `theta`

This is the threshold applied after the whole-block mismatch count is computed.

Rule:

- create an edge if `D(X,Y) <= theta`
- do not create an edge if `D(X,Y) > theta`

Interpretation:

- lower `theta`: fewer edges, more fragmented connected components
- higher `theta`: more edges, more merging

## Sparse Graph Controls

These matter only for `Sparse graph clustering`.

### `Distance family`

Server parameter: `distance_mode`

Controls which distance family is used.

Values:

- `feature_aware`: new distance system from the revised spec/addendum
- `legacy`: old mismatch-count style distance

Use:

- choose `feature_aware` almost always
- use `legacy` only to compare old vs new behavior

### `Feature tolerance`

Server parameter: `delta`

Global tolerance in pip units before differences start counting.

For feature-aware distance, each feature difference contributes only above this tolerance.

Interpretation:

- low `delta`: strict matching
- high `delta`: tolerant matching

Effect:

- increasing it creates more candidate neighbors
- too high can blur distinct patterns together

Typical start:

- `2` to `8`, depending on symbol pip scale and volatility

### `Body comparison mode`

Server parameter: `body_mode`

Controls how candle bodies are compared.

Values:

- `signed_body`: bullish and bearish bodies are treated as directionally different
- `magnitude_only`: compares only body size, ignores sign
- `magnitude_sign_penalty`: compares size first, then adds a penalty if direction differs

When to use:

- `signed_body`: best default if direction matters
- `magnitude_only`: useful if you want structural similarity independent of bullish/bearish direction
- `magnitude_sign_penalty`: good compromise when direction matters, but not absolutely

### `Body sign penalty`

Server parameter: `body_sign_penalty`

Used only when `body_mode = magnitude_sign_penalty`.

It adds extra distance when two bodies have different signs.

Interpretation:

- `0`: direction almost ignored
- larger values: bullish vs bearish mismatch becomes more important

Practical range:

- `1.0` to `5.0`

### `Use mirrored comparison`

Server parameter: `mirrored`

Experimental symmetry mode.

When enabled, the server also compares blocks after:

- swapping upper/lower wick
- negating body
- keeping range unchanged

Use this if you want to treat bullish and bearish mirror-image structures as related.

## Hard Gates

These reject pairs before full graph distance is used.

They are the main protection against nonsensical matches.

### `body_gate`

Maximum allowed difference in block `net_body`.

Low value:

- blocks with different overall directional force are rejected quickly

High value:

- more permissive

### `range_gate`

Maximum allowed difference in block `avg_range`.

This controls scale compatibility.

Very important when you want to stop tiny candles from matching huge candles.

### `bull_gate`

Maximum allowed difference in `bull_count`.

Example:

- a `4-bull` block and a `1-bull` block differ by `3`
- with `bull_gate = 2`, that pair is rejected

### `signchg_gate`

Maximum allowed difference in number of sign changes across the block.

This separates patterns like:

- `++++`
- `+++-`
- `++--`

Lower values force stronger regime-shape consistency.

### `same_majority`

Binary flag:

- `0`: do not require same majority sign
- `1`: require both blocks to have the same bullish/bearish majority

Useful when you want to stop bullish-dominant and bearish-dominant sequences from mixing.

## Graph Construction Controls

### `graph_k`

How many nearest neighbors to keep per node before graph clustering.

Low `graph_k`:

- sparser graph
- less collapse
- more fragmentation risk

High `graph_k`:

- denser graph
- more stable communities
- more risk of over-connecting everything

Typical start:

- `6` to `12`

If dataset is small:

- `3` to `6` may be enough

### `mutual_knn`

Binary flag:

- `1`: keep an edge only if each node is in the other node's top-k list
- `0`: one-sided k-NN is enough

Recommended default:

- `1`

Why:

- reduces bridge-node collapse
- removes weak one-way neighbor links

### `graph_max_dist`

Optional hard cutoff after k-NN ranking.

Meaning:

- `0`: disabled
- `> 0`: discard neighbors with distance above this value

Use this when:

- graph still gets too dense even with mutual k-NN
- you want a hard compactness cap

### `graph_mode`

Final clustering method applied to the sparse graph.

Values:

- `label_propagation`: current best default
- `connected_components`: plain components on sparse graph
- `legacy_connected_components`: old threshold graph diagnostic mode
- `louvain`: currently accepted, but server falls back to label propagation

Recommended:

- `label_propagation`

Use diagnostics:

- `connected_components` or `legacy_connected_components` only for comparison/debugging

## Feature Weights

These scale how much each feature contributes to graph distance.

### `w_upper`

Importance of upper wick differences.

Increase it if wick shape matters more in your research.

### `w_lower`

Importance of lower wick differences.

### `w_body`

Importance of body comparison.

Often one of the most important weights.

Increase it if directional conviction and body magnitude are central to the pattern definition.

### `w_range`

Importance of total candle range differences.

Increase it if volatility scale is critical.

## Composition Weights

These matter only when `use_composition = 1`.

They use block summaries rather than per-candle raw coordinates.

### `use_composition`

Binary flag:

- `1`: include block summary penalties
- `0`: use only candle-level geometry/body/range

Recommended:

- `1`

### `w_bullcount`

Penalty for different counts of bullish candles inside the block.

Higher value separates:

- `++++`
- `+++-`
- `++--`

more strongly.

### `w_signchg`

Penalty for different numbers of bullish/bearish sign transitions.

This is one of the best controls for separating trend-like blocks from choppy alternating ones.

### `w_runbull`

Penalty for different longest bullish run length.

### `w_runbear`

Penalty for different longest bearish run length.

These run-based weights help distinguish:

- steady one-sided moves
- interrupted moves
- alternation-heavy sequences

## K-Medoids Controls

These matter only for `K-medoids`.

### `kmed_k`

Target number of clusters.

This is not inferred automatically.

You must choose it.

Effect:

- too small: different patterns get forced together
- too large: one pattern may split into many clusters

Typical start:

- `8` to `32`, depending on dataset size and expected variety

### `max_iter`

Maximum number of refinement iterations per run.

Higher value:

- more chance of convergence
- more runtime

Typical start:

- `20` to `50`

### `restarts`

How many times k-medoids is rerun with different initialization.

Higher value:

- better chance of avoiding poor local minima
- more runtime

Typical start:

- `3` to `10`

For quick checks:

- `1`

## DBSCAN Controls

These matter only for `DBSCAN`.

### `eps`

Neighborhood radius.

Smaller `eps`:

- stricter local neighborhoods
- more noise points

Larger `eps`:

- more connected neighborhoods
- more chance of merging distinct structures

### `min_pts`

Minimum neighborhood size required for a core point.

Smaller value:

- easier to form clusters

Larger value:

- more conservative clustering
- more noise

Typical start:

- `eps = 4` to `12`
- `min_pts = 4` to `10`

## Word Mining Controls

These matter only for `Run Word Mining`.

### `word_min`

Minimum word length in symbols.

### `word_max`

Maximum word length in symbols.

Suggested start:

- `2` and `12`

Shorter lengths:

- more frequent
- less specific

Longer lengths:

- rarer
- more specific

## Phrase Mining Controls

These matter only for `Run Phrase Mining`.

### `phrase_min`

Minimum phrase length.

### `phrase_max`

Maximum phrase length.

Suggested start:

- `2` and `8`

## Recommended Presets

### Preset A: General Graph Baseline

- `q = 4`
- `distance_mode = feature_aware`
- `delta = 4`
- `body_mode = signed_body`
- `body_gate = 8`
- `range_gate = 8`
- `bull_gate = 2`
- `signchg_gate = 2`
- `graph_k = 8`
- `mutual_knn = 1`
- `graph_max_dist = 0`
- `graph_mode = label_propagation`
- `use_composition = 1`
- all weights = `1.0`

### Preset B: Strong Direction Sensitivity

- `body_mode = signed_body`
- `same_majority = 1`
- `w_body = 2.0`
- `w_bullcount = 2.0`
- `w_signchg = 2.0`

Use this when bullish vs bearish structure must remain separate.

### Preset C: Mirror-Structure Search

- `mirrored = 1`
- `body_mode = magnitude_sign_penalty`
- `sign_penalty = 1.0`

Use this when you want to find structurally similar up/down variants.

### Preset D: Anti-Collapse Graph

- `mutual_knn = 1`
- `graph_k = 4` to `8`
- `graph_max_dist =` small positive cutoff
- moderate gates
- nonzero composition weights

Use this if the graph starts forming one giant cluster.

## Tuning Guide

If almost everything lands in one cluster:

- reduce `graph_k`
- set `mutual_knn = 1`
- lower `delta`
- lower gates
- increase `w_body`, `w_bullcount`, `w_signchg`
- set a positive `graph_max_dist`

If you get too many tiny clusters:

- increase `delta`
- increase `graph_k`
- loosen gates
- reduce composition weights

If direction should matter more:

- use `signed_body`
- increase `w_body`
- enable `same_majority`

If direction should matter less:

- use `magnitude_only`
- or enable `mirrored`

## Important Limitations

- `louvain` is not fully implemented yet; server currently falls back to label propagation.
- The GUI exposes one shared parameter panel, so many controls are visible even when the selected button will ignore them.
- `kmedoids` and `dbscan` still use the older parameter model internally; the new graph distance controls do not automatically change those algorithms.
