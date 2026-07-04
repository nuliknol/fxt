# Primitive Waves Algorithm

## Purpose

This document describes the current low-level implementation of the `primitive_waves` model used by the trainer daemon when the GUI submits `Run Wave Inference` with `wave_model=primitive_waves`.

The goal is to make future work safe in two areas:

- adding new per-wave metrics
- extending or replacing the current dynamic programming and primitive reduction rules

This is implementation documentation, not a high-level product spec. Everything below is based on the current code in:

- `trainer/src/wave_dp.c`
- `trainer/src/hip_wave_kernels.hip.cpp`
- `trainer/src/gpu_dispatch.c`
- `trainer/src/json_codec.c`
- `trainer/src/http_api.c`
- `trainer/tests/test_core.c`

## End-To-End Runtime Path

### Entry Points

The GUI eventually calls:

- `POST /jobs/wave-inference`

The HTTP layer builds a `WaveParams` and submits a job:

- `trainer/src/http_api.c`
- `trainer/src/job_manager.c`
- `core_run_wave_inference(...)`
- `wave_dp_run(...)`

The final result is stored in memory on the dataset record:

- `DatasetRecord.waves`
- `WaveResult.tokens`
- `WaveResult.token_count`
- `WaveResult.total_fit_error`
- `WaveResult.params`
- `WaveResult.kpi`

The result is then exposed through:

- `GET /datasets/<dataset_id>/waves`
- `core_get_wave_summary(...)`
- `core_get_wave_tokens(...)`

## Primitive Model Output Space

`primitive_waves` does not use the legacy 7-class output space. It collapses the model into 3 primitive token classes:

- `PRIMITIVE_CLASS_CONSECUTIVE`
- `PRIMITIVE_CLASS_ALTERNATING`
- `PRIMITIVE_CLASS_SEPARATOR`

Serialized names:

- `consecutive_string`
- `alternating_string`
- `separator_candle`

Direction names:

- `bull`
- `bear`
- `neutral`

`separator_candle` is usually a one-candle token, but separator runs can exist temporarily inside the pipeline and are later collapsed or merged.

## Main Data Structures

### `WaveParams`

`WaveParams` controls:

- token length bounds: `lmin_candles`, `lmax_candles`
- context extraction: `regime_context_window`, `regime_context_mode`
- normalization and penalty constants
- primitive-vs-legacy mode: `wave_model`
- DP segment penalty: `segment_penalty_lambda`

Even in primitive mode, many legacy-named parameters still matter because primitive scoring reuses pieces of the same segment evaluator.

### `WaveToken`

Each final token stores:

- span: `start_candle`, `end_candle`, `duration_candles`
- class: `class_id`
- fit quality: `fit_error`, `second_best_error`, `ambiguity_margin`, `weak_fit`
- movement and structure: `net_move_raw`, `dominant_direction`, `boundary_confidence`
- primitive metrics:
	- `primitive_consecutive_ratio`
	- `primitive_alternating_ratio`
	- `primitive_max_consecutive_run`
	- `primitive_max_alternating_run`
	- `primitive_counter_body_ratio`
	- `primitive_minority_body_ratio`
- shared metrics also reused by primitive logic:
	- `retracement_ratio`
	- `sign_change_density`
	- `directional_purity_body`
	- `range_growth_ratio`
	- `context_range`
- debug lineage:
	- `debug_flags`
	- `debug_reduction_flags`
	- `debug_reduction_count`

### `WaveKPI`

Primitive mode still computes global KPIs on the final token stream:

- ambiguity and confidence aggregates
- short-token percentages
- near-`lmax` token counts
- weak-fit counts

Legacy-only class coverage KPIs are not populated in primitive mode.

## High-Level Execution Order

`wave_dp_run(...)` executes the primitive pipeline in this exact order:

1. resolve defaults and merge user params
2. ensure candle features exist
3. validate params
4. build initial token sequence
5. recompute token metrics for that initial sequence
6. primitive-only reduction passes
7. recompute final token metrics
8. compute boundary confidence
9. compute KPIs
10. store final tokens and totals on `DatasetRecord.waves`

The primitive-only reduction passes run in this order:

1. `split_primitive_mixed_tokens`
2. `split_primitive_internal_trends` until stable
3. `split_primitive_edge_trends` until stable
4. `coalesce_maximal_primitive_spans`
5. `expand_consecutive_boundaries` until stable
6. `collapse_separator_runs` until stable
7. `merge_separator_into_consecutive` until stable
8. `merge_separator_into_alternating` until stable
9. `bridge_neutral_primitive_tokens` until stable
10. `extend_consecutive_with_adjacent_edges`

This ordering is important. Several later rules assume earlier rules already converted obvious separators and trend fragments.

## Parameter Resolution

Defaults are created in `default_wave_params()`.

Important current defaults:

- `wave_model = WAVE_MODEL_LEGACY`
- `lmin_candles = 1`
- `lmax_candles = 30`
- `regime_context_window = 64`
- `segment_penalty_lambda = 220`
- `trend_priority_mode = 1`
- `retrace_break_threshold = 200`
- `weak_fit_threshold = 140`
- `ambiguity_low_margin_threshold = 40`
- `boundary_low_conf_threshold = 30`

Incoming user values are merged in `merge_wave_params(...)`.

Validation in `validate_params(...)` clamps and normalizes:

- `lmin_candles >= 1`
- `lmax_candles >= lmin_candles`
- `lmax_candles <= candle_count`
- `regime_context_window >= lmax_candles`
- valid context mode only
- valid wave model only

## Candle Features Used By Primitive Waves

Primitive scoring depends on `CandleFeat` and raw candle OHLC values.

The key feature fields used here are:

- `features[c].b`
	- signed body movement used as the main directional signal
- `features[c].r`
	- range magnitude used for volatility/range growth scoring

Important derived concepts:

- doji: `features[c].b == 0`
- inside candle: current candle high/low fully inside previous candle high/low
- effective candle:
	- non-doji
	- not an inside candle

Primitive sequence logic intentionally ignores:

- doji candles for run construction
- inside candles for run construction

But they still remain inside the token span and still influence some broader range-based metrics.

## Core Segment Evaluator

Everything starts from `compute_segment_eval(...)`.

That function evaluates one candidate span `[start, end]` and produces:

- per-class errors
- best class
- second-best class
- ambiguity margin
- all token metrics later copied into `WaveToken`

This evaluator is used by:

- CPU recomputation
- primitive split and merge rules
- boundary confidence
- GPU DP candidate scoring

### Context Window

The evaluator first computes a context window around the segment with `compute_context_bounds(...)`.

Modes:

- centered: segment centered inside `regime_context_window`
- trailing: context window ends at the segment end

`context_range = max(context_high - context_low, context_range_floor)`

This context range is the main denominator for normalized movement and volatility metrics.

### Dominant Direction

Raw net direction:

- `raw_net = candles[end].close_pips - candles[start].open_pips`

Canonical sign:

- `-1` if `raw_net < 0`
- `+1` otherwise

Important consequence:

- zero-net spans are treated as canonical positive internally, but `dominant_direction` is stored as:
	- `1` if raw net > 0
	- `-1` if raw net < 0
	- `0` if raw net == 0

### Body Accounting

For each candle in the span:

- `canonical_body = features[c].b * canonical_sign`

So the span is evaluated relative to its own dominant move direction.

Positive canonical body contributes to:

- `trend_body_count`
- `trend_body`

Negative canonical body contributes to:

- `counter_body_count`
- `retrace_body`

### Tolerated Single Counter Candle

The evaluator contains a special rule for one temporary counter candle inside a consecutive trend:

- `is_tolerated_counter_trend_candle(...)`

The counter candle is tolerated only when:

- it is not at the segment edge
- a confirming next non-doji candle exists
- the counter candle does not break the prior candle's low/high in the wrong way
- the confirming candle closes beyond the prior close in the original direction

If tolerated:

- the counter candle is neutralized for trend-body counting
- it does not count as a sign flip for primitive sequential metrics

This rule is directly covered by tests:

- `test_primitive_wave_tolerates_single_counter_candle`
- `test_primitive_wave_rejects_counter_candle_level_break`

### Sequential Step Metrics

Primitive mode relies heavily on close-to-close step analysis.

For each pair of adjacent candles in the span:

- `step_dir = sign(candles[next].close_pips - candles[prev].close_pips)`

The step is skipped if:

- it lands on the tolerated counter candle
- target candle is a doji
- target candle is an inside candle
- close-to-close delta is zero

From the remaining steps the evaluator computes:

- `primitive_consecutive_ratio`
	- fraction of nonzero steps aligned with canonical direction
- `primitive_alternating_ratio`
	- fraction of step transitions that flip direction
- `primitive_max_consecutive_run`
	- longest same-direction run length, converted from steps to candles by adding 1
- `primitive_max_alternating_run`
	- longest alternating flip-flop run length, also converted to candle length by adding 1

### Other Derived Metrics

The evaluator also computes:

- `net_move_raw = abs(raw_net)`
- `net_move_ratio = net_move_raw / gross_range_raw`
- `retracement_ratio = retrace_body / trend_body`
- `sign_change_density = sign_changes / (duration - 1)`
- `directional_purity_body = trend_body / gross_move_raw`
- `range_growth_ratio`
	- average second-half range over first-half range
- `active_body_ratio`
	- fraction of candles that are not fully inert after tolerance handling
- `primitive_counter_body_ratio`
	- counter-body candle share
- `primitive_minority_body_ratio`
	- minority-side body count share

All normalized ratios are scaled by `norm_scale`, typically `1000`.

### Primitive Class Error Functions

In primitive mode, the evaluator maps the span into 3 scores.

#### Consecutive Score

`PRIMITIVE_CLASS_CONSECUTIVE` uses:

- strong-trend-like and weak-trend-like penalties
- low alternating ratio
- long same-direction run
- zero counter-body preference
- good net move and purity
- low sign-change density

The actual consecutive score is:

- `min(strong_trend_error, weak_trend_error) + move_penalty`

Hard rejection:

- if the first or last real body points opposite to canonical direction, consecutive score becomes effectively infinite

This edge-direction rejection is why some lead/trail fragments stay split instead of being absorbed.

Covered by tests:

- `test_primitive_wave_rejects_opposite_leading_edge`
- `test_primitive_wave_rejects_bearish_lead_for_bull_run`

#### Alternating Score

`PRIMITIVE_CLASS_ALTERNATING` is the minimum of several legacy-style shape scores:

- retracement-like
- alternating chop
- mixed compression
- volatile expansion
- v-shaped reversal

Then `move_penalty` is added.

This means primitive alternating is a meta-bucket, not a single geometric form.

#### Separator Score

`PRIMITIVE_CLASS_SEPARATOR` only becomes viable for:

- `duration == 1`

Longer spans cannot be selected as separators directly by the evaluator.

Separator scoring prefers:

- low active-body ratio
- small gross range

### Best Class And Ambiguity

After class errors are computed, the evaluator chooses:

- `best_class`
- `second_best_class`
- `ambiguity_margin = second_best_error - best_fit`

These values are copied into the final token and later used by KPIs.

## Initial Primitive Tokenization

Primitive mode does not use the legacy trend-lock path. Instead:

- `build_trend_priority_tokens(...)`
- detects `wave_model == WAVE_MODEL_PRIMITIVE`
- calls `build_primitive_rule_first_tokens(...)`

This is the first major design difference from legacy waves.

### Rule-First Consecutive Discovery

`build_primitive_rule_first_tokens(...)` scans left to right and tries to extract obvious hard consecutive runs before GPU DP fills the gaps.

The helper chain is:

- `primitive_find_next_hard_consecutive_span(...)`
- `primitive_find_rule_run(...)`
- `primitive_span_is_strict_consecutive(...)`

#### `primitive_find_rule_run(...)`

This finds a same-direction effective run beginning at a candidate index.

Behavior:

- skips non-effective candles
- fixes the run sign from the first effective candle
- allows neutral/doji/inside candles to stay inside the span
- allows one pending counter candle only if later validated
- stops before an unconfirmed opposite-direction break

#### `primitive_span_is_strict_consecutive(...)`

This is the hard validator used before a span is promoted to consecutive.

It requires:

- effective run covers the trimmed effective body region
- at least 3 effective same-direction bodies
- no opposite-direction real bodies in trimmed prefix/suffix
- first meaningful run is not immediately contradicted before 3 same-sign bodies accumulate

This function is reused later by merge and extension passes. It is one of the most important invariants in the primitive system.

### GPU Gap Fill

Whenever the rule-first pass leaves a gap, that gap is filled by:

- `append_gpu_gap_tokens(...)`

For primitive mode:

- `demote_priority_classes = 0`
- class space is the primitive 3-class space

The output tokens from GPU reconstruction are marked with:

- `WAVE_TOKEN_REDUCTION_GPU_DP`

The rule-first consecutive spans are marked with:

- `WAVE_TOKEN_REDUCTION_RULE_FIRST`

After assembly, the token stream is sorted by start index and validated to be a complete non-overlapping partition of the candle range.

## GPU Dynamic Programming

### Dispatch Layer

CPU code calls:

- `gpu_wave_dp(...)`

which dispatches to:

- `hip_wave_dp_impl(...)`

if HIP is enabled.

If HIP is unavailable:

- primitive wave inference fails immediately in `wave_dp_run(...)`

### DP State

For a gap of length `N`, the GPU computes arrays indexed by local end position `end`:

- `dp[end]`
	- best total score for partitioning `[0, end]`
- `prev_start[end]`
	- start index of the last chosen token
- `class_id[end]`
	- chosen class for the last token
- metric side arrays for that last chosen token

### Per-End Evaluation

For each `end`, the kernel enumerates candidate token lengths:

- `len in [lmin_candles, min(lmax_candles, end + 1)]`

Candidate start:

- `start = end + 1 - len`

Each candidate is scored by `build_candidate(...)`, which mirrors the CPU segment evaluator logic closely.

Candidate total score:

- `dp[start - 1] + best_fit + move_penalty + segment_penalty_lambda`

Important implementation detail:

- in the HIP code, `best_fit` already includes `move_penalty`
- then `move_penalty` is added again to `out.score`
- and `out.fit_error` is stored as `best_fit + move_penalty`

So the GPU path currently double-applies `move_penalty` relative to the conceptual score formula.

The CPU later recomputes every token with `recompute_token(...)`, so stored token metrics and fit errors become CPU-consistent afterward. But the segmentation choice itself is still made using the GPU score.

This matters if future work changes:

- `min_wave_move_pips`
- DP transition cost design
- any comparison between GPU-selected segmentation and CPU recomputation

### Parallelism Model

For each `end`, HIP launches one block:

- `WAVE_DP_THREADS = 64`

Threads split the candidate lengths, compute local bests, then reduce inside shared memory.

This is not a wavefront DP over many `end` positions at once. The host launches one kernel per `end` sequentially.

So the current GPU acceleration is:

- parallel over candidate lengths for a fixed end
- sequential over ends

That is relevant if you want to redesign the DP algorithm.

### Backtracking

After GPU arrays return to CPU, `append_gpu_gap_tokens(...)` reconstructs the partition by walking backward:

1. start at `cursor = range_len - 1`
2. read `start = prev_start[cursor]`
3. emit token `[start, cursor]`
4. jump to `cursor = start - 1`
5. reverse the collected tokens into chronological order

## CPU Recompute Pass

Immediately after the initial segmentation is built, every token is recomputed on CPU using:

- `recompute_token(...)`

This preserves:

- `debug_flags`
- `debug_reduction_flags`
- `debug_reduction_count`

and refreshes all metrics from the final current span boundaries.

This recompute is why newly added metrics should usually be derived inside `compute_segment_eval(...)` and copied by `fill_token_from_eval(...)`, not hand-maintained in each primitive reduction function.

## Primitive Reduction Pipeline

The primitive system does not stop at DP. The initial tokenization is treated as a rough partition that is then rewritten by rule passes.

### 1. `split_primitive_mixed_tokens`

This pass finds alternating tokens that look too mixed and splits them at every sign flip in effective-body space.

Trigger heuristic `should_split_primitive_token(...)` uses combinations of:

- `weak_fit`
- `primitive_max_consecutive_run`
- `primitive_consecutive_ratio`
- `primitive_alternating_ratio`
- `primitive_counter_body_ratio`
- `primitive_minority_body_ratio`

Each produced subspan is re-evaluated and classified by `classify_split_run(...)`.

Reduction flag:

- `WAVE_TOKEN_REDUCTION_SPLIT_MIXED`

Intent:

- take obviously mixed DP spans and expose latent sub-runs before deeper refinement

### 2. `split_primitive_internal_trends`

This repeatedly searches alternating tokens for the strongest interior same-sign run that:

- is fully internal, not touching token edges
- contains at least 3 effective same-sign bodies

It extracts the best such run as a new consecutive token and reclassifies left/right remainders.

Selection priority:

- larger effective-count run wins
- tie breaks by lower consecutive fit error

Reduction flag:

- `WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL`

Intent:

- peel a clear consecutive wave out of the middle of a broader alternating token

Covered by:

- `test_primitive_wave_splits_internal_trend_with_neutral_break`

### 3. `split_primitive_edge_trends`

This looks for a strong consecutive edge run at the front or back of an alternating token.

Requirements:

- token is alternating
- token duration at least 4
- edge run has at least 3 effective same-sign bodies
- edge run alone evaluates as consecutive
- edge run fit is not weak

If both leading and trailing candidates exist, trailing wins on tie.

Reduction flag:

- `WAVE_TOKEN_REDUCTION_SPLIT_EDGE`

Intent:

- extract breakout tails or leading trend fragments from an alternating token

Covered by:

- `test_primitive_wave_splits_breakout_tail`
- `test_primitive_wave_extends_across_leading_inside_edge`
- `test_primitive_wave_rejects_opposite_leading_edge`

### 4. `coalesce_maximal_primitive_spans`

This is a local adjacent merge pass.

Two neighboring tokens are merged only if:

- merged duration does not exceed `lmax_candles`
- merged span evaluates to a valid primitive kind
- if merged kind is consecutive, strict consecutive validation passes
- if merged kind is alternating, `primitive_alternating_ratio >= 420`
- both original tokens already have the same primitive class as the merged result
- merged fit is not worse than split fit

Reduction flag:

- `WAVE_TOKEN_REDUCTION_COALESCE`

Intent:

- remove needless fragmentation after split passes, but only if the merged interpretation is at least as good

### 5. `expand_consecutive_boundaries`

This moves one boundary candle from alternating into adjacent consecutive when that boundary candle points in the same direction as the consecutive token and the expanded span still evaluates as strong consecutive.

It handles both shapes:

- alternating | consecutive
- consecutive | alternating

Intent:

- absorb one same-direction pivot candle that the earlier segmentation left on the wrong side

This pass rewrites token boundaries without changing token count.

Covered by:

- `test_primitive_wave_expands_consecutive_boundary`

### 6. `collapse_separator_runs`

If multiple consecutive separator tokens appear in a row, this pass merges the whole separator run into one fitted primitive token.

If merged eval prefers consecutive:

- strict consecutive validation must also pass

Otherwise it becomes alternating.

Reduction flag:

- `WAVE_TOKEN_REDUCTION_COLLAPSE_SEPARATOR`

Intent:

- separator is meant to be a local bridge state, not a long-lived multi-token island

Covered by:

- `test_primitive_wave_collapses_separator_run`

### 7. `merge_separator_into_consecutive`

This tries to absorb a separator into a neighboring consecutive token when:

- separator sign matches neighbor dominant direction
- merged span passes `primitive_span_is_strict_consecutive(...)`

It tries left then right.

Reduction flag:

- `WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_CONSEC`

Intent:

- preserve maximal clean consecutive runs even if DP emitted a one-candle bridge

### 8. `merge_separator_into_alternating`

If the separator cannot join consecutive, this pass tries to merge it into a neighboring alternating token.

Acceptance uses bounded degradation:

- absolute tolerance `merge_abs_tol = 180`
- or relative tolerance `5/4`

If merged left/right both work, lower merged alternating error wins.

It also sets human-readable separator origin flags:

- `WAVE_TOKEN_FLAG_SEPARATOR_LEFT_ADDED`
- `WAVE_TOKEN_FLAG_SEPARATOR_RIGHT_ADDED`

Reduction flag:

- `WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_ALT`

Intent:

- keep visible separators only when they are really needed after neighboring cleanup

Covered by:

- `test_primitive_wave_merges_separator_debug_flag`

### 9. `bridge_neutral_primitive_tokens`

This merges:

- consecutive
- one or more neutral tokens
- consecutive

when both consecutive endpoints have the same direction and the whole merged span is still strict consecutive.

Neutral bridge definition:

- token dominant direction is `0`
- every candle inside it is either doji or inside-candle

Merge is accepted only if:

- merged total fit + one segment penalty is not worse than the split total of all parts

Reduction flag:

- `WAVE_TOKEN_REDUCTION_BRIDGE_NEUTRAL`

Intent:

- let a trend continue across inert candles that should not force a wave boundary

Covered by:

- `test_primitive_wave_treats_doji_as_neutral`
- `test_primitive_wave_ignores_neutral_bridge_in_primary_dp`
- `test_primitive_wave_live_prefix_neutral_bridge`

### 10. `extend_consecutive_with_adjacent_edges`

This is the final boundary-stretch pass.

For each consecutive token, it repeatedly tries to steal one adjacent candle from the left or right as long as:

- the expanded span remains strict consecutive

If the neighbor shrinks to zero width, the neighbor token is removed.
Otherwise the neighbor is reclassified on its reduced span.

Reduction flag:

- `WAVE_TOKEN_REDUCTION_EXTEND_CONSEC_EDGE`

Intent:

- make consecutive tokens maximal after all other cleanup has settled

Covered by:

- `test_primitive_wave_extends_across_terminal_pullback_confirmation`

## Primitive Invariants

After the primitive pipeline finishes, the intended invariants are:

- tokens form a complete non-overlapping partition of all candles
- consecutive tokens are as large as possible under `primitive_span_is_strict_consecutive(...)`
- alternating tokens hold all non-consecutive mixed behavior
- separator tokens, if any remain, are truly local boundary artifacts
- neutral/doji/inside candles do not force fake breaks when a surrounding consecutive move can absorb them legally

These invariants are not enforced in one place. They emerge from the pass ordering above.

## Boundary Confidence

After all primitive passes complete, `compute_boundary_confidence(...)` runs on adjacent token pairs.

For each split between `tokens[i]` and `tokens[i+1]`:

- evaluate merged span
- compute:
	- `boundary_confidence = merged_eval.best_fit - (left.fit_error + right.fit_error)`

Interpretation:

- positive and large: split is justified
- near zero or negative: merged interpretation is competitive or better

The confidence is stored on the left token of the pair.

KPI:

- `low_boundary_confidence_count` counts splits below `boundary_low_conf_threshold`

## KPI Computation

`compute_kpis(...)` runs on the final stored token array.

Primitive-relevant KPIs include:

- `avg_ambiguity_margin`
- `low_margin_token_count`
- `avg_token_length`
- `tokens_per_1000_candles`
- `pct_one_candle_tokens`
- `pct_two_candle_tokens`
- `pct_near_lmax_tokens`
- `avg_long_token_error`
- `avg_boundary_confidence`
- `low_boundary_confidence_count`
- `weak_fit_count`

In primitive mode, legacy class coverage arrays are not populated meaningfully because class IDs are in the primitive 3-class space.

## Final Total Fit Error

Stored `WaveResult.total_fit_error` is computed as:

- `segment_penalty_lambda * token_count`
- plus sum of `token.fit_error`
- plus some legacy transition penalties

In primitive mode:

- retracement and legacy trend transition penalty counters remain zero

So primitive total fit error is effectively:

- segment penalty per token
- plus sum of final token fit errors

## Debug Flags And Reduction Lineage

Primitive debugging depends heavily on two fields:

- `debug_flags`
- `debug_reduction_flags`

Current reduction lineage bits:

- `WAVE_TOKEN_REDUCTION_RULE_FIRST`
- `WAVE_TOKEN_REDUCTION_GPU_DP`
- `WAVE_TOKEN_REDUCTION_SPLIT_MIXED`
- `WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL`
- `WAVE_TOKEN_REDUCTION_SPLIT_EDGE`
- `WAVE_TOKEN_REDUCTION_COALESCE`
- `WAVE_TOKEN_REDUCTION_COLLAPSE_SEPARATOR`
- `WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_CONSEC`
- `WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_ALT`
- `WAVE_TOKEN_REDUCTION_BRIDGE_NEUTRAL`
- `WAVE_TOKEN_REDUCTION_EXTEND_CONSEC_EDGE`

Current separator-origin bits:

- `WAVE_TOKEN_FLAG_SEPARATOR_LEFT_ADDED`
- `WAVE_TOKEN_FLAG_SEPARATOR_RIGHT_ADDED`

These are serialized to the API as:

- `debug_flags`
- `debug_reduction_flags`
- `debug_reduction_count`
- `separator_debug`

If new metrics need provenance, this is the existing pattern to follow.

## JSON Output Fields Exposed To The GUI

Primitive tokens are serialized with:

- `token_index`
- `start_candle`
- `end_candle`
- `class_id`
- `class_name`
- `primitive_type`
- `primitive_direction`
- `fit_error`
- `second_best_error`
- `ambiguity_margin`
- `duration_candles`
- `net_move_raw`
- `retracement_ratio`
- `sign_change_density`
- `directional_purity_body`
- `range_growth_ratio`
- `context_range`
- `primitive_consecutive_ratio`
- `primitive_alternating_ratio`
- `primitive_max_consecutive_run`
- `primitive_max_alternating_run`
- `primitive_counter_body_ratio`
- `primitive_minority_body_ratio`
- `dominant_direction`
- `boundary_confidence`
- `weak_fit`
- `debug_flags`
- `debug_reduction_flags`
- `debug_reduction_count`
- `separator_debug`

Primitive summary also exposes:

- `primitive_counts`
- `params`
- `kpi`

## Where To Add New Per-Wave Metrics

If the new metric is a property of a single span, the safest path is:

1. add fields to `WaveSegmentEval`
2. compute the metric inside `compute_segment_eval(...)`
3. add fields to `WaveToken`
4. copy values in `fill_token_from_eval(...)`
5. expose them in `json_encode_wave_list(...)`
6. optionally aggregate them in `compute_kpis(...)`

Why this is the right place:

- every stage already trusts `compute_segment_eval(...)` as the source of truth
- CPU recomputation after boundary changes refreshes the metric automatically
- split/merge passes do not need custom bookkeeping

Do not attach span metrics only inside the GPU path unless you also add the CPU-side equivalent. The CPU recompute pass will overwrite token fields.

## Where To Change DP Behavior

If you want to extend the segmentation algorithm itself, there are 3 main layers.

### 1. Candidate Span Scoring

Change:

- `compute_segment_eval(...)`
- `build_candidate(...)`

Use this when:

- class score formulas change
- a new primitive class is added
- new metrics affect per-span fit

Important rule:

- CPU and HIP scoring logic must stay aligned

### 2. DP State / Transition Design

Change:

- `hip_wave_kernels.hip.cpp`
- `append_gpu_gap_tokens(...)`

Use this when:

- adding transition-aware costs
- adding extra DP state dimensions
- replacing one-block-per-end scheduling
- introducing multi-token context into DP

Current limitation:

- DP state only remembers best previous cut and class for each end
- no higher-order transition memory exists in primitive mode

### 3. Post-DP Structural Rewriting

Change:

- primitive reduction passes in `wave_dp.c`

Use this when:

- the desired behavior is local and structural
- a wave should be split or merged using deterministic rules after coarse segmentation
- the designer wants cleaner token shapes without redesigning the entire DP

In practice, many current primitive features are implemented here instead of inside the DP.

## Recommended Hook Points For New Primitive Metrics

### Span-only metrics

Examples:

- wick imbalance
- breakout gap size
- neutral candle density
- inside-candle density
- momentum decay

Hook point:

- `compute_segment_eval(...)`

### Metrics about how the token was formed

Examples:

- whether token originated from rule-first or GPU
- which reduction stages touched it
- how many times boundaries changed

Hook points:

- `mark_token_reduction(...)`
- extra token fields updated in split/merge functions

### Metrics depending on neighboring tokens

Examples:

- left/right context compatibility
- split stability
- continuation confidence

Hook points:

- after final tokenization, near `compute_boundary_confidence(...)`
- or in a new final pass after all primitive reductions

## Current Known Design Constraints

### Primitive mode is hybrid, not pure DP

The real algorithm is:

- rule-first obvious consecutive spans
- GPU DP for leftover gaps
- repeated CPU reduction passes

Any redesign should treat all three layers together.

### Consecutive is protected by hard validation

`primitive_span_is_strict_consecutive(...)` is the strongest gate in the system.

Many passes may prefer a merged consecutive interpretation, but they still reject it unless this validator passes.

### Neutral handling is intentionally asymmetric

Doji and inside candles are:

- ignored for primitive run counting
- still present in spans
- later bridgeable if they sit between two same-direction consecutive tokens

### GPU and CPU scoring must stay synchronized

The HIP file duplicates most of the evaluator logic.

Any change to:

- thresholds
- formulas
- primitive class definitions
- normalization

must be mirrored in both places unless the GPU path is being retired.

### There is a current GPU move-penalty duplication

As noted above, HIP candidate scoring currently adds `move_penalty` twice into the DP objective.

CPU recomputation hides that in stored token metrics, but not in segmentation choice.

If future work changes move-based penalties, this should be revisited first.

## Test Cases That Define Primitive Behavior

These tests are the best executable examples of intended behavior:

- `test_primitive_wave_ignores_inside_candle_break`
- `test_primitive_wave_splits_breakout_tail`
- `test_primitive_wave_merges_separator_debug_flag`
- `test_primitive_wave_collapses_separator_run`
- `test_primitive_wave_tolerates_single_counter_candle`
- `test_primitive_wave_rejects_counter_candle_level_break`
- `test_primitive_wave_treats_doji_as_neutral`
- `test_primitive_wave_ignores_neutral_bridge_in_primary_dp`
- `test_primitive_wave_live_prefix_neutral_bridge`
- `test_primitive_wave_splits_internal_trend_with_neutral_break`
- `test_primitive_wave_extends_across_terminal_pullback_confirmation`
- `test_primitive_wave_expands_consecutive_boundary`
- `test_primitive_wave_extends_across_leading_inside_edge`
- `test_primitive_wave_rejects_opposite_leading_edge`
- `test_primitive_wave_rejects_bearish_lead_for_bull_run`

If you change primitive behavior, update or extend this set first.

## Practical Guidance For Designers And Implementers

If the planned change is "add more columns per wave":

- implement the metric in `compute_segment_eval(...)`
- copy it into `WaveToken`
- serialize it in `json_codec.c`

If the planned change is "change how a wave is split or merged":

- first decide whether it is:
	- a span scoring problem
	- a DP optimization problem
	- a post-DP structural rule

Use this rule of thumb:

- if the change depends only on one span, put it in the evaluator
- if it depends on choosing boundaries globally, change DP
- if it depends on local token neighborhoods after coarse segmentation, add a primitive reduction pass

If the planned change is "add a new primitive class":

- update enum definitions
- update `wave_output_class_count(...)`
- update CPU evaluator class-error logic
- update HIP evaluator class-error logic
- update reconstruction and serialization
- update primitive filters in `json_codec.c`
- update GUI table and filter options

## Summary

`primitive_waves` is currently a 3-class hybrid segmentation system:

- hard rule-first extraction of obvious consecutive runs
- GPU dynamic programming for the remaining partition problem
- a long sequence of deterministic primitive cleanup passes

The most important technical anchors for future work are:

- `compute_segment_eval(...)`
- `primitive_span_is_strict_consecutive(...)`
- `append_gpu_gap_tokens(...)`
- `hip_wave_kernels.hip.cpp`
- the ordered primitive reduction pipeline in `wave_dp_run(...)`

Any meaningful extension should start by deciding which of those layers actually owns the new behavior.
