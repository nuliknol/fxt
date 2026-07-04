# Trainer Methods And Extension Guide

This document is the implementation-oriented reference for extending the trainer.

It is written for future work in three areas:

- adding new training or clustering methods
- adding new distance functions or distance modes
- adding new parameters to existing methods

The source of truth is the current C implementation in:

- `trainer/src/http_api.c`
- `trainer/src/job_manager.c`
- `trainer/src/char_block.c`
- `trainer/src/cluster_graph.c`
- `trainer/src/cluster_kmedoids.c`
- `trainer/src/cluster_dbscan.c`
- `trainer/src/word_mining.c`
- `trainer/src/phrase_mining.c`
- `trainer/src/gpu_dispatch.c`
- `trainer/src/hip_distance_kernels.hip.cpp`
- `trainer/include/api_types.h`
- `trainer/include/internal_store.h`

If this document and the code disagree, the code wins.

## 1. System Overview

The trainer pipeline is:

1. load dataset candles
2. build candle features
3. build character blocks of length `q`
4. optionally cluster the blocks
5. derive a symbol stream from the latest clustering
6. optionally derive an RLE stream
7. mine repeated words or phrases from the symbol stream

Important stored representations:

- `Candle`
  - raw OHLCV input
- `CandleFeat`
  - per-candle encoded features: upper wick, lower wick, body, range
- `CharMatrix`
  - all `q`-candle blocks for a dataset
  - `dim = q * 4`
- `BlockSummary`
  - precomputed block-level aggregates used by GPU distance gating
- `ClusterResult`
  - latest clustering result stored on the dataset
- `symbol_stream`
  - sequence of cluster ids, one per character block
- `rle_tokens`
  - run-length encoding of the symbol stream

## 2. Execution Flow

Every training or mining job follows the same high-level path:

1. HTTP endpoint parses query params in `trainer/src/http_api.c`
2. parsed params are copied into one of the structs from `trainer/include/api_types.h`
3. `core_run_*()` in `trainer/src/job_manager.c` validates defaults/basic invariants
4. `push_job()` stores the job in the in-memory queue
5. the single worker thread executes the queued job
6. the algorithm writes results back into the dataset record in `GlobalStore`

Important design facts:

- Jobs are queued in memory only.
- There is one worker thread, so jobs run serially.
- Parameters are copied by value into `JobRecord`.
- Clustering methods overwrite the dataset's previous `ClusterResult`.
- Word and phrase mining read the latest clustering-derived symbol stream.

## 3. Public Job Types

Current job types:

- `JOB_TYPE_GRAPH_CLUSTERING`
- `JOB_TYPE_WORD_MINING`
- `JOB_TYPE_PHRASE_MINING`
- `JOB_TYPE_KMEDOIDS`
- `JOB_TYPE_DBSCAN`

Current HTTP endpoints:

- `POST /jobs/character-clustering`
- `POST /jobs/kmedoids`
- `POST /jobs/dbscan`
- `POST /jobs/word-mining`
- `POST /jobs/phrase-mining`

## 4. Character Blocks And Summaries

Character blocks are built by `core_build_character_blocks()` in `trainer/src/char_block.c`.

For each sliding window of `q` candles:

- the block stores `q * 4` integers
- the coordinate order is repeated per candle:
  - `upper`
  - `lower`
  - `body`
  - `range`

At the same time the builder also computes `BlockSummary`:

- `bull_count`
- `bear_count`
- `sign_changes`
- `max_bull_run`
- `max_bear_run`
- `net_body`
- `gross_body`
- `avg_range`
- `upper_sum`
- `lower_sum`

These summaries are required by the HIP distance kernel. Any new training method that uses `gpu_distance_tile()` must ensure `rec->chars.summaries` is available.

## 5. Distance Subsystem

### 5.1 Where Distance Is Really Implemented

The effective clustering distance logic is in the HIP kernel:

- `trainer/src/hip_distance_kernels.hip.cpp`

Dispatch is through:

- `gpu_distance_tile()` in `trainer/src/gpu_dispatch.c`

There is also a CPU helper:

- `core_distance_block_pair()` in `trainer/src/char_distance.c`

Important limitation:

- `core_distance_block_pair()` only implements the old mismatch-count distance
- it is not the full source of truth for `feature_aware`, `strict_composition`, or `wick_aware`
- current graph clustering uses the GPU path, not this CPU helper

### 5.2 Shared DistanceConfig Fields

The shared config struct is `DistanceConfig` in `trainer/include/api_types.h`.

Key fields:

- `distance_mode`
- `mirrored_mode`
- `weighted`
- `delta`
- per-feature weights:
  - `w_upper`
  - `w_lower`
  - `w_body`
  - `w_range`
- body options:
  - `body_mode`
  - `body_sign_penalty`
- composition weights:
  - `w_bullcount`
  - `w_signchanges`
  - `w_runbull`
  - `w_runbear`
- hard gates:
  - `gate_body_diff`
  - `gate_range_diff`
  - `gate_bullcount_diff`
  - `gate_signchange_diff`
  - `gate_upper_wick_diff`
  - `gate_lower_wick_diff`
  - `gate_lower_ratio_diff`
  - `gate_upper_ratio_diff`
  - `gate_total_wick_ratio_diff`
- feature toggles:
  - `use_upper`
  - `use_lower`
  - `use_range`
  - `use_body`
  - `use_composition`
- `require_same_majority_sign`
- `block_q`

### 5.3 Mirroring

Mirroring means:

- upper wick and lower wick are swapped
- body sign is inverted
- range is unchanged

For summary values, mirroring also swaps:

- `bull_count` and `bear_count`
- `max_bull_run` and `max_bear_run`
- `upper_sum` and `lower_sum`
- `net_body` sign

When `mirrored_mode=1`, the kernel evaluates direct and mirrored comparisons and keeps the smaller valid distance.

### 5.4 Distance Modes

Current enum values:

- `DISTANCE_MODE_FEATURE_AWARE = 0`
- `DISTANCE_MODE_LEGACY = 1`
- `DISTANCE_MODE_STRICT_COMPOSITION = 2`
- `DISTANCE_MODE_WICK_AWARE = 3`

Current HTTP string mappings:

- `feature_aware`
- `legacy`
- `strict_composition`
- `wick_aware`

#### `legacy`

Per coordinate:

- mismatch if `abs(a[k] - b[k]) > delta`

Distance:

- unweighted: count mismatches
- weighted: sum feature weights over mismatches

This is the original threshold-style distance.

#### `feature_aware`

This is the modern sparse-graph distance.

For each enabled coordinate type:

- compute `phi_excess(diff, delta) = max(abs(diff) - delta, 0)`
- multiply by the relevant feature weight

For body coordinates, the actual body difference depends on `body_mode`.

If `use_composition=1`, the kernel adds summary penalties for:

- `abs(bull_count diff) * w_bullcount`
- `abs(sign_changes diff) * w_signchanges`
- `abs(max_bull_run diff) * w_runbull`
- `abs(max_bear_run diff) * w_runbear`

Before any of that, the pair must pass the hard gates in `passes_gate()`.

#### `strict_composition`

Distance value is still mismatch-count based, but the pair is discarded unless:

- the common hard gates pass
- the threshold-composition regime check passes

#### `wick_aware`

Distance value is also mismatch-count based, but gating is stricter than `strict_composition`.

Extra wick gates are checked on:

- `upper_sum`
- `lower_sum`
- lower-wick/body ratio
- upper-wick/body ratio
- total-wick/body ratio

### 5.5 Body Modes

Current values:

- `0`
  - signed difference: `abs(body_a - body_b)`
- `1`
  - magnitude-only difference: `abs(abs(body_a) - abs(body_b))`
- `2`
  - magnitude-only difference plus sign penalty when signs disagree

### 5.6 Hard Gates

The gate function `passes_gate()` is central to `feature_aware`, `strict_composition`, and `wick_aware`.

A pair can be rejected before distance accumulation if any enabled rule fails.

The base gates compare:

- `net_body`
- `avg_range`
- `bull_count`
- `sign_changes`

Optional extra gate:

- `require_same_majority_sign`

Extra wick-aware gates compare:

- `upper_sum`
- `lower_sum`
- wick/body ratios

## 6. Graph Clustering

Graph clustering is implemented in `trainer/src/cluster_graph.c` and exposed through:

- `POST /jobs/character-clustering`

This endpoint actually serves two different algorithms:

- legacy threshold clustering
- sparse k-NN graph clustering

The selector is `cluster_mode`.

### 6.1 GraphParams

`GraphParams` contains all graph-clustering options.

Important fields:

- block construction:
  - `q`
- distance:
  - `distance_mode`
  - `delta`
  - `mirrored_mode`
  - `weighted`
  - all weights
  - all gates
  - body options
  - feature toggles
- legacy threshold:
  - `theta`
- sparse graph:
  - `k_neighbors`
  - `use_mutual_knn`
  - `max_neighbor_distance`
- graph backend:
  - `cluster_mode`

### 6.2 Graph Cluster Modes

Current enum values:

- `GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS = 0`
- `GRAPH_CLUSTER_MODE_CONNECTED_COMPONENTS = 1`
- `GRAPH_CLUSTER_MODE_LABEL_PROPAGATION = 2`
- `GRAPH_CLUSTER_MODE_LOUVAIN = 3`

Current HTTP string mappings:

- `legacy_connected_components`
- `connected_components`
- `label_propagation`
- `louvain`

Important reality:

- `louvain` is accepted by the API
- the implementation currently logs a fallback and uses weighted label propagation

### 6.3 Legacy Threshold Clustering

This path is selected by:

- `cluster_mode=legacy_connected_components`

Implementation entry:

- `build_legacy_threshold_graph()`

Important behavior:

- blocks are first reordered into a canonical sorted order
- block-to-block distances are computed on the GPU
- only pairs passing `threshold_composition_match()` are considered
- only pairs with distance `<= theta` are considered linked
- assignment is not plain connected components
- a new block joins an existing cluster only if it matched every member already in that cluster
- otherwise a new cluster is created

This is stricter than transitive graph connectivity.

The function also computes:

- node strength
- cluster score from internal edge weights
- graph diagnostics

### 6.4 Sparse k-NN Graph Clustering

This path is selected by any `cluster_mode` other than `legacy_connected_components`.

Implementation entry:

- `build_sparse_knn_graph()`

Important behavior:

- pairwise distances are computed tile-by-tile on the GPU
- each node keeps its `k_neighbors` closest candidates
- `max_neighbor_distance` can reject long links
- final edges are:
  - mutual only if `use_mutual_knn=1`
  - union of one-sided neighbors otherwise

Edge weight is always:

- `1 / (1 + distance)`

After edge construction, the graph is clustered by:

- connected components, or
- weighted label propagation

### 6.5 Graph Diagnostics

Stored graph diagnostics include:

- `n_nodes`
- `n_edges`
- `average_degree`
- degree histogram
- `raw_component_count`
- `largest_component_size`
- `largest_component_fraction`
- `percolated`
- cluster-size histogram

Percolation threshold is hardcoded:

- `GRAPH_PERCOLATION_FRACTION = 0.80`

### 6.6 Graph Endpoint Defaults

Defaults applied by HTTP and by `core_run_graph_clustering(NULL)`:

- `q = 4`
- `distance_mode = feature_aware`
- `delta = 4`
- `theta = 4`
- `mirrored = 0`
- `weighted = 1`
- `delta_body_gate = 8`
- `delta_range_gate = 8`
- `delta_bullcount_gate = 2`
- `delta_signchange_gate = 2`
- `delta_upper_wick_gate = 8`
- `delta_lower_wick_gate = 8`
- `delta_lower_ratio_gate = 25`
- `delta_upper_ratio_gate = 25`
- `delta_total_wick_ratio_gate = 30`
- `k_neighbors = 8`
- `use_mutual_knn = 1`
- `max_neighbor_distance = 0.0`
- `cluster_mode = label_propagation`
- `body_mode = 0`
- `require_same_majority_sign = 0`
- `use_upper = 1`
- `use_lower = 1`
- `use_range = 1`
- `use_body = 1`
- `use_composition = 1`
- `w_upper = 1.0`
- `w_lower = 1.0`
- `w_body = 1.0`
- `w_range = 1.0`
- `body_sign_penalty = 2.0`
- `w_bullcount = 1.0`
- `w_signchanges = 1.0`
- `w_runbull = 1.0`
- `w_runbear = 1.0`

## 7. K-Medoids

Implementation:

- `trainer/src/cluster_kmedoids.c`

Endpoint:

- `POST /jobs/kmedoids`

Behavior:

- ensures character blocks for the requested `q`
- requires GPU availability
- randomly seeds `k` medoids per restart
- assigns each block to its nearest medoid
- recomputes each medoid by brute-force search over cluster members
- keeps the best restart by total assignment score

Current parameters:

- `q`
- `delta`
- `k`
- `max_iter`
- `restarts`
- `mirrored`
- `weighted`
- `w_upper`
- `w_lower`
- `w_body`
- `w_range`

Current defaults:

- `q = 4`
- `delta = 4`
- `k = 8`
- `max_iter = 20`
- `restarts = 1`
- `mirrored = 0`
- `weighted = 0`
- `w_upper = 1.0`
- `w_lower = 1.0`
- `w_body = 1.0`
- `w_range = 1.0`

Important scope limitation:

- K-medoids does not currently expose `distance_mode`, gate parameters, composition weights, or body-mode options.

## 8. DBSCAN

Implementation:

- `trainer/src/cluster_dbscan.c`

Endpoint:

- `POST /jobs/dbscan`

Behavior:

- ensures character blocks for the requested `q`
- requires GPU availability
- computes neighbors by scanning all blocks against the current point
- treats distance `<= eps` as a neighbor
- grows clusters with a queue-based expansion
- marks unmatched points as noise

Current parameters:

- `q`
- `delta`
- `eps`
- `min_pts`
- `mirrored`
- `weighted`
- `w_upper`
- `w_lower`
- `w_body`
- `w_range`

Current defaults:

- `q = 4`
- `delta = 4`
- `eps = 4`
- `min_pts = 4`
- `mirrored = 0`
- `weighted = 0`
- `w_upper = 1.0`
- `w_lower = 1.0`
- `w_body = 1.0`
- `w_range = 1.0`

Important scope limitation:

- DBSCAN also does not currently expose `distance_mode`, gates, composition weights, or body-mode options.

## 9. Word Mining

Implementation:

- `trainer/src/word_mining.c`

Endpoint:

- `POST /jobs/word-mining`

Behavior:

- builds the symbol stream from the latest clustering
- optionally switches to an RLE-derived stream
- enumerates every exact subsequence from `min_len` to `max_len`
- counts duplicates by exact token equality
- only stores patterns with frequency `>= 2`

Parameters:

- `min_len`
- `max_len`
- `use_rle`

Defaults:

- `min_len = 2`
- `max_len = 12`
- `use_rle = 0`

This is exact repeated-subsequence counting, not approximate mining.

## 10. Phrase Mining

Implementation:

- `trainer/src/phrase_mining.c`

Endpoint:

- `POST /jobs/phrase-mining`

Behavior:

- builds symbol stream and RLE stream
- mines exact repeated token subsequences
- tracks average duration in chars and candles
- if `use_rle=1`, duration in chars is the sum of run lengths
- candle duration is `dur_chars + (q - 1)`

Parameters:

- `min_len`
- `max_len`
- `use_rle`

Defaults:

- `min_len = 2`
- `max_len = 8`
- `use_rle = 1`

## 11. Wave Classification

Implementation:

- `trainer/src/wave_dp.c`
- `trainer/src/hip_wave_kernels.hip.cpp`
- `trainer/src/json_codec.c`

Endpoint:

- `POST /jobs/wave-inference`

Overview:

- the wave classifier partitions the full candle sequence into non-overlapping tokens
- each token gets exactly one class
- the current classes are:
  - `strong_trend`
  - `weak_trend`
  - `retracement`
  - `alternating_chop`
  - `mixed_compression`
  - `volatile_expansion`
  - `v_shaped_reversal`
- the execution priority is:
  1. identify `strong_trend`
  2. identify `weak_trend`
  3. classify the remaining gaps with the non-trend classes
  4. merge adjacent opposite-direction trends into `v_shaped_reversal` when that reduces error

This priority is intentional. Trends are higher priority than choppy patterns.

### 11.1 Inputs

The method consumes:

- candle OHLCV data
- candle features built by `core_build_candle_features()`

The most important public parameters are:

- `lmin_candles`
- `lmax_candles`
- `regime_context_window`
- `regime_context_mode`
- `segment_penalty_lambda`
- `trend_priority_mode`
- `retrace_break_threshold`
- `trend_split_gain_min`
- `compression_min_duration`
- `expansion_min_duration`
- `v_reversal_min_trend_strength`
- `v_reversal_max_bridge_chop`
- `v_reversal_max_duration`

Important default values:

- `lmin_candles = 1`
- `lmax_candles = 30`
- `regime_context_window = 64`
- `trend_priority_mode = 1`
- `segment_penalty_lambda = 220`

### 11.2 Per-Segment Measurements

For any candidate span `[start, end]`, the implementation computes a shared set of measurements.

Important measurements:

- `net_move_raw`
  - absolute open-to-close move for the span
- `net_move_ratio`
  - net move normalized by total candle range inside the span
- `retracement_ratio`
  - counter-trend body divided by trend body
- `sign_change_density`
  - how often candle body direction flips inside the span
- `directional_purity_body`
  - how much of the body flow is aligned with the dominant direction
- `active_body_ratio`
  - fraction of candles with nonzero body
- `advance_per_candle_norm`
  - normalized net advance per candle
- `range_growth_ratio`
  - second-half average range divided by first-half average range
- `v_strength`
  - strongest two-sided reversal split found inside the segment
- `dominant_direction`
  - `+1`, `-1`, or `0`

The context window is computed first.

- if `regime_context_mode = centered`, the segment is centered inside the context window as much as possible
- if `regime_context_mode = trailing`, the context ends at the segment end

The context window provides `context_range`, which is used to normalize several measurements.

### 11.3 Class Error Model

Each class has an error function. Lower error means a better fit.

Current class intent:

- `strong_trend`
  - strict trend
  - wants high net progress, high directional purity, low retracement, low internal chop
- `weak_trend`
  - looser trend
  - still wants directional continuation, but allows more retracement and more internal chop than `strong_trend`
- `retracement`
  - meaningful pullback against the surrounding trend
- `alternating_chop`
  - frequent back-and-forth direction changes with low net progress
- `mixed_compression`
  - contained range behavior with low or moderate progress and no expansion signature
- `volatile_expansion`
  - larger second-half range growth or generally expanding range behavior
- `v_shaped_reversal`
  - two-sided reversal with strong left and right legs and limited bridge chop

For each span:

1. compute all class errors
2. choose the minimum-error class as `best_class`
3. choose the second minimum as `second_best_class`
4. store the difference as `ambiguity_margin`

### 11.4 Step-By-Step Execution

This is the current execution order.

#### Step 1: Resolve Parameters

- start from built-in defaults in `default_wave_params()`
- merge user-provided values on top
- validate ranges such as `lmin_candles <= lmax_candles`

#### Step 2: Build Priority Trend Locks

This is the most important part of the algorithm.

The code scans candidate lengths from longest to shortest:

- start at `lmax_candles`
- go down to a minimum lockable length derived from `trend_priority_mode`

For each length, it scans left to right across the dataset:

1. skip spans that overlap already locked candles
2. compute the full segment evaluation
3. test `strong_trend` lock conditions first
4. if not strong, test `weak_trend` lock conditions
5. if either passes, lock that entire span immediately
6. mark all candles in that span as occupied

Consequences:

- longer trend spans win over shorter trend spans
- `strong_trend` wins over `weak_trend` for the same span
- non-trend classes do not participate in locking
- once a trend span is locked, later scans cannot take candles away from it

This is how the priority rule "trends first" is enforced.

#### Step 3: Fill Unlocked Gaps With GPU Dynamic Programming

After priority trend locks are chosen, the remaining gaps are filled with GPU dynamic programming.

For each unlocked gap:

1. enumerate all segment ends inside the gap
2. for each end, enumerate candidate start points between `lmin_candles` and `lmax_candles`
3. compute the best class error for each candidate span
4. add `segment_penalty_lambda`
5. choose the minimum total DP score

Important behavior:

- when filling unlocked gaps after trend locking, any DP result that is itself a trend or `v_shaped_reversal` is demoted to a non-priority class
- this means leftover gaps are used only for lower-priority pattern classes

So the final result is not "all classes compete everywhere equally".

It is:

1. trends claim candles first
2. remaining candles are classified as non-trend patterns

#### Step 4: Recompute Final Token Metrics

After the initial token sequence is assembled:

- each token is re-evaluated on the CPU
- the stored token metrics are refreshed from the final candle boundaries

This keeps token metadata consistent even if the initial segmentation came from the GPU path.

#### Step 5: Merge Same-Direction Adjacent Trends

The code then checks adjacent trend tokens.

If both neighbors are trend classes and share the same direction:

1. evaluate the combined span
2. measure whether keeping the split is materially better than merging
3. if the split gain is smaller than `trend_split_gain_min`, merge them
4. assign the merged token as:
   - `strong_trend` if the merged span satisfies strong lock conditions
   - otherwise `weak_trend` if it satisfies weak lock conditions

Purpose:

- maximize trend wave length
- avoid unnecessary fragmentation of a directional move

#### Step 6: Merge Opposite-Direction Adjacent Trends Into `v_shaped_reversal`

If two adjacent trend tokens have opposite directions:

1. evaluate the combined span as a possible `v_shaped_reversal`
2. compare that error against keeping the split plus the configured transition penalty
3. merge only if the reversal interpretation is better

This is the only place where `v_shaped_reversal` is formed.

#### Step 7: Apply Retracement Structural Rules

Raw class score alone is not enough for retracements.

A token labeled `retracement` is rejected if any of these conditions fail:

- it sits at the dataset edge
- it is adjacent to another retracement
- both neighbors are not trend classes
- its direction does not oppose the surrounding trend direction

There is also a strength check:

- if the retracement is too weak relative to the previous trend, the three-token pattern is collapsed back into a single trend token when the merged span still qualifies as a trend

If a retracement is rejected but cannot be merged into a valid trend span:

- it is reassigned to the best fallback non-retracement class

#### Step 8: Re-run Trend/Reversal Cleanup After Retracement Changes

Whenever retracement rules change the token sequence:

- same-direction trend merging is run again
- opposite-direction `v_shaped_reversal` merging is run again

This continues until retracement rules stop changing the sequence.

#### Step 9: Compute Boundary Confidence And KPIs

After the token sequence is final:

- boundary confidence is computed by comparing each split against the merged alternative
- KPI counters are populated
- class counts are populated
- final total fit error is computed

### 11.5 What "Trend Priority" Means In Practice

The current implementation does not let all classes compete symmetrically.

Instead:

- trend detection is greedy by descending candidate length
- longer valid trends are preferred
- `strong_trend` has higher priority than `weak_trend`
- non-trend classes only explain candles left over after priority trend locking

This means a span that still qualifies as a trend should usually remain trend, even if a choppy class could also score well on a shorter subspan.

### 11.6 Current Limitations

Important current limitations:

- priority locking is greedy
  - once a long trend span is locked, later passes do not globally reconsider that earlier decision
- `v_shaped_reversal` is not a first-pass lock class
  - it is created only by merging two adjacent trend tokens later
- retracement requires surrounding trend context
  - an isolated span cannot remain retracement at the edge
- the final behavior depends both on raw class errors and on the structural cleanup rules

## 12. Result Storage

The latest clustering result is stored in `DatasetRecord.clustering`.

Important stored fields:

- `algorithm_type`
- `cluster_count`
- `noise_count`
- `graph_cluster_mode`
- `cluster_ids`
- `summaries`
- `graph_diag`

Interpretation:

- `algorithm_type = JOB_TYPE_GRAPH_CLUSTERING`
  - `graph_cluster_mode` tells you whether it was legacy threshold or sparse graph
- `algorithm_type = JOB_TYPE_KMEDOIDS`
  - clustering came from k-medoids
- `algorithm_type = JOB_TYPE_DBSCAN`
  - clustering came from DBSCAN

Word and phrase outputs are stored separately on the dataset:

- `words`
- `phrases`

## 13. Extension Playbook

### 13.1 Adding A New Training Method

You must update all of these places:

1. `trainer/include/api_types.h`
   - add a new params struct if needed
   - add a new `JOB_TYPE_*` enum if this is a new job family
2. `trainer/include/core_api.h`
   - declare `core_run_<method>()`
3. `trainer/src/http_api.c`
   - add endpoint parsing
   - add HTTP defaults
   - map string params to enum values if needed
4. `trainer/src/job_manager.c`
   - extend `JobRecord`
   - copy params in `push_job()`
   - add logging in `log_job_params()`
   - dispatch in `run_job_record()`
   - add `core_run_<method>()`
5. `trainer/src/<your_method>.c`
   - implement the algorithm
6. result storage
   - write the result back to `DatasetRecord`
   - if this is clustering, fill `ClusterResult` consistently
7. JSON/export paths if new outputs are exposed
   - usually `trainer/src/json_codec.c` and dataset getters
8. this document

If the new method trains on pairwise block distances, it should use the GPU path unless there is a deliberate architectural reason not to.

### 13.2 Adding A New Distance Mode

You must update all of these places:

1. `trainer/include/api_types.h`
   - add the new `DistanceMode` enum value
   - add any new config fields to `DistanceConfig`
   - add matching graph params if the mode is public through graph clustering
2. `trainer/src/http_api.c`
   - extend `query_get_distance_mode()`
   - parse any new query parameters
3. `trainer/src/cluster_graph.c`
   - copy new graph params into `DistanceConfig` in `fill_distance_config_from_graph()`
4. `trainer/src/hip_distance_kernels.hip.cpp`
   - implement the new mode in `distance_kernel()`
   - extend gating or body logic if required
5. any non-graph methods that should also use the new mode
   - their params structs currently do not carry `distance_mode`
6. `trainer/src/job_manager.c`
   - log the new params
7. this document

If you want CPU and GPU behavior to stay consistent, `trainer/src/char_distance.c` must also be updated. Today it only implements the legacy mismatch-count distance.

### 13.3 Adding A New Parameter To An Existing Method

For every new parameter:

1. add the field to the relevant params struct in `trainer/include/api_types.h`
2. add the HTTP parser/default in `trainer/src/http_api.c`
3. add the `core_run_*()` default when `params == NULL`
4. copy it into `DistanceConfig` or the algorithm-local config if needed
5. log it in `trainer/src/job_manager.c`
6. document it here

Do not add a field in only one layer. The common failure mode in this codebase is a parameter existing in the API but not being initialized all the way down to the executing kernel.

## 14. Known Constraints And Refactor Hazards

These are important for future agent work.

### 14.1 GPU Is Mandatory For Current Clustering

All current clustering algorithms require `gpu_dispatch_available()`.

If HIP is disabled or unavailable:

- graph clustering fails
- k-medoids fails
- DBSCAN fails

Word and phrase mining do not require GPU.

### 14.2 `theta` Is Only Used By Legacy Threshold Clustering

`theta` is parsed for all graph-clustering requests, but it only affects:

- `cluster_mode=legacy_connected_components`

It is ignored by sparse k-NN graph clustering.

### 14.3 `k_neighbors` Is Still Required Even For Legacy Threshold Mode

`core_run_graph_clustering()` and `cluster_graph_run()` reject `k_neighbors == 0`.

That validation runs even when:

- `cluster_mode=legacy_connected_components`

So the current threshold path still depends on a nonzero `k_neighbors`, even though it does not use k-NN construction internally.

### 14.4 `louvain` Is Not Implemented

The API accepts `cluster_mode=louvain`, but the runtime falls back to weighted label propagation.

### 14.5 K-Medoids And DBSCAN Do Not Share Full Graph Distance Features

They currently expose only:

- `delta`
- `mirrored`
- `weighted`
- `w_upper`
- `w_lower`
- `w_body`
- `w_range`

They do not currently expose:

- `distance_mode`
- gates
- composition weights
- body-mode options

If you want all clustering methods to share one distance family, this is one of the main refactor targets.

### 13.6 K-Medoids And DBSCAN Currently Partially Initialize DistanceConfig

In `cluster_kmedoids.c` and `cluster_dbscan.c`, the local `DistanceConfig cfg` is not fully zero-initialized before being passed to the GPU kernel.

That means fields such as:

- `distance_mode`
- gate values
- toggle flags
- `block_q`

are not explicitly defined there.

The intended behavior appears to be legacy-style distance, but the current implementation does not guarantee that cleanly. This should be treated as a correctness hazard during refactoring.

### 13.7 CPU And GPU Distance Logic Diverge

`trainer/src/char_distance.c` only supports the old mismatch-count logic.

`trainer/src/hip_distance_kernels.hip.cpp` implements the real modern distance family.

Any refactor that introduces a shared distance abstraction should unify these two paths or make their separation explicit.

## 14. Parameter Reference By Endpoint

### `POST /jobs/character-clustering`

Current query parameters:

- `dataset_id`
- `q`
- `distance_mode`
- `delta`
- `theta`
- `mirrored`
- `weighted`
- `delta_body_gate`
- `delta_range_gate`
- `delta_bullcount_gate`
- `delta_signchange_gate`
- `delta_upper_wick_gate`
- `delta_lower_wick_gate`
- `delta_lower_ratio_gate`
- `delta_upper_ratio_gate`
- `delta_total_wick_ratio_gate`
- `k_neighbors`
- `use_mutual_knn`
- `max_neighbor_distance`
- `cluster_mode`
- `body_mode`
- `require_same_majority_sign`
- `use_upper`
- `use_lower`
- `use_range`
- `use_body`
- `use_composition`
- `w_upper`
- `w_lower`
- `w_body`
- `w_range`
- `body_sign_penalty`
- `w_bullcount`
- `w_signchanges`
- `w_runbull`
- `w_runbear`

### `POST /jobs/kmedoids`

Current query parameters:

- `dataset_id`
- `q`
- `delta`
- `k`
- `max_iter`
- `restarts`
- `mirrored`
- `weighted`
- `w_upper`
- `w_lower`
- `w_body`
- `w_range`

### `POST /jobs/dbscan`

Current query parameters:

- `dataset_id`
- `q`
- `delta`
- `eps`
- `min_pts`
- `mirrored`
- `weighted`
- `w_upper`
- `w_lower`
- `w_body`
- `w_range`

### `POST /jobs/word-mining`

Current query parameters:

- `dataset_id`
- `min_len`
- `max_len`
- `use_rle`

### `POST /jobs/phrase-mining`

Current query parameters:

- `dataset_id`
- `min_len`
- `max_len`
- `use_rle`

## 15. Recommended Refactor Direction

If the next task is to evolve the trainer, the cleanest direction is:

1. define one explicit shared distance API
2. make all clustering methods use fully initialized distance config
3. separate legacy threshold clustering from sparse graph clustering more clearly
4. make parameter defaults live in one place per method
5. keep GPU distance as the canonical execution path for pairwise training distances

That would remove most of the current ambiguity when adding new methods or parameters.
