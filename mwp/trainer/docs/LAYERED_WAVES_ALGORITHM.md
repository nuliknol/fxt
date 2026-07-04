# Layered Waves Algorithm

## Purpose

This document describes the current implementation of the `layered_waves` model used by the trainer daemon when the GUI submits `Run Wave Inference` with:

- `wave_model=layered_waves`

This is implementation documentation based on the current code, not a conceptual spec. The formulas and rules below are taken from:

- `trainer/src/wave_dp.c`
- `trainer/src/json_codec.c`
- `trainer/src/http_api.c`
- `trainer/src/job_manager.c`

The goal is to explain:

- how layer 1 strings are built
- how layer 2 waves are classified by dynamic programming
- how token properties are calculated
- how penalties and `fit_error` are calculated
- how the post-DP structural rewrite passes modify the initial DP output

## End-To-End Runtime Path

The GUI calls:

- `POST /jobs/wave-inference`

The HTTP layer builds `WaveParams` in:

- `trainer/src/http_api.c`

The job system dispatches to:

- `core_run_wave_inference(...)`
- `wave_dp_run(...)`

For `wave_model=layered_waves`, the main construction path is:

- `build_trend_priority_tokens(...)`
- `build_layered_tokens(...)`

The final result is stored on the dataset record and exposed through:

- `GET /datasets/<dataset_id>/waves`

## Model Structure

`layered_waves` is explicitly two-layered.

### Layer 1

Primitive strings:

- `sequential_string`
- `alternating_string`

Serialized names come from primitive class ids:

- `PRIMITIVE_CLASS_CONSECUTIVE` -> `sequential_string`
- `PRIMITIVE_CLASS_ALTERNATING` -> `alternating_string`

### Layer 2

Final waves:

- `trend`
- `range`

Layer 2 is built on top of layer 1. A final wave always references a contiguous child interval:

- `layered_child_start_index`
- `layered_child_end_index`

and the JSON exposes the corresponding `child_strings` list.

## Main Functions

The current layered pipeline is centered on these functions in `trainer/src/wave_dp.c`:

- `build_layered_layer1_tokens(...)`
- `compute_layered_child_summary(...)`
- `compute_layered_span_errors(...)`
- `layered_dp_class_bias(...)`
- `build_layered_tokens(...)`

Important helper/rewrite functions:

- `optimize_layered_token_boundaries(...)`
- `split_layered_edge_outliers(...)`
- `split_layered_internal_outliers(...)`
- `split_layered_internal_range_islands(...)`
- `split_layered_cross_token_range_islands(...)`
- `merge_same_direction_layered_trends(...)`
- `merge_adjacent_layered_ranges(...)`
- `extract_leading_layered_trend_into_following_range(...)`
- `extract_layered_dominant_trend_blocks(...)`
- `trim_layered_unbridged_alternating_edges_from_trends(...)`
- `extract_layered_suffix_trend_from_mixed_trend(...)`
- `reclassify_layered_tokens(...)`

## Shared Candle-Level Evaluator

Even layered waves still reuse the common segment evaluator:

- `compute_segment_eval(...)`

This function computes the candle-span metrics used later by the layered scorer, including:

- `net_move_raw`
- `relative_net_move_raw`
- `relative_abs_body_sum_raw`
- `relative_abs_step_sum_raw`
- `relative_directional_efficiency_body`
- `relative_directional_efficiency_step`
- `relative_avg_body_efficiency`
- `relative_body_direction_share`
- `relative_overlap_ratio`
- `sign_change_density`
- `relative_retracement_ratio`
- `relative_trend_quality_raw`

For the exact shared formulas, see:

- `compute_segment_eval(...)`

in `trainer/src/wave_dp.c`.

## Penalty Helpers

The layered scorer uses the common penalty helpers:

```c
static int32_t penalty_low(int32_t x, int32_t a)
{
	return (x < a) ? (a - x) : 0;
}

static int32_t penalty_high(int32_t x, int32_t b)
{
	return (x > b) ? (x - b) : 0;
}
```

Interpretation:

- `penalty_low(x, a)`
	- penalize when `x` is too low
- `penalty_high(x, b)`
	- penalize when `x` is too high

These are linear penalties. The scorer then multiplies them by weights.

## Layer 1 Construction

Layer 1 is built by:

- `build_layered_layer1_tokens(...)`

### Sequential Candidate Discovery

The algorithm scans candles from left to right and forms same-sign blocks using raw body sign:

- bullish candle:
	- `close_pips > open_pips`
- bearish candle:
	- `close_pips < open_pips`
- doji:
	- `close_pips == open_pips`

For each raw same-sign block:

1. find the first non-doji sign
2. extend until the next opposite real body sign
3. call:
	- `compute_relative_trend_price_amplitude(...)`

to choose the best subspan inside that block

The chosen subspan is kept as a candidate only if:

- it contains at least `2` effective candles of the same sign
- `layered_span_is_pure_sequential(...)` is true

### Pure Sequential Rule

`layered_span_is_pure_sequential(...)` uses raw body sign only:

- any real opposite candle breaks the span
- dojis are allowed

So `sequential_string` means:

- all non-doji candles must point the same way

### Alternating Strings

`alternating_string` is now an explicit primitive, not a residual gap class.

Grammar:

- ignore dojis for alternation state
- the remaining non-doji candle bodies must alternate sign strictly
- any repeated non-doji sign ends the primitive

So valid examples are:

- `bull, bear`
- `bear, bull, bear`
- `bull, doji, bear, doji, bull`

Invalid examples are:

- `bull, bull, bear`
- `bear, bear, bull`
- `bull, bear, bear`

### Alternating Constraint

Current rules enforced in layer 1:

- `sequential_string` must contain at least `2` non-doji candles of the same sign
- `alternating_string` must contain at least `2` non-doji candles
- `alternating_string` must contain at least one bull and one bear candle
- `alternating_string` records `primitive_alternating_pair_count`
	- this is the number of strict bull/bear flips inside the primitive
- one-candle primitive leftovers are collapsed by:
	- `collapse_layered_layer1_singletons(...)`

so final layer 1 should not leave one-candle primitives.

## Layer 1 Token Properties

Each layer-1 token is still evaluated through `compute_segment_eval(...)`, then emitted as a `WaveToken`.

So primitive child strings already carry:

- span
- class
- dominant direction
- net move
- shared relative metrics
- primitive ratios

These child tokens are later summarized into layer-2 features.

## Layered Child Summary

For every candidate layer-2 span, child-string statistics are computed by:

- `compute_layered_child_summary(...)`

This builds:

- `count`
- `sequential_count`
- `alternating_count`
- `sequential_candles`
- `alternating_candles`
- `sequential_bull_candles`
- `sequential_bear_candles`
- `alternating_bull_candles`
- `alternating_bear_candles`
- `sequential_net_move_sum_raw`
- `alternating_net_move_sum_raw`
- `max_sequential_net_move_raw`
- `max_alternating_net_move_raw`
- `max_child_net_move_raw`
- `sequential_bull_net_move_sum_raw`
- `sequential_bear_net_move_sum_raw`
- `alternating_bull_net_move_sum_raw`
- `alternating_bear_net_move_sum_raw`

Derived ratios:

### `directional_agreement`

This looks only at sequential-string candles:

```text
directional_agreement =
	max(sequential_bull_candles, sequential_bear_candles) * norm_scale
	/ max(sequential_candles, 1)
```

Range:

- `0 .. norm_scale`

Typical with `norm_scale=1000`:

- `1000` means all sequential child candles agree in one direction

### `sequential_ratio`

```text
sequential_ratio =
	sequential_candles * norm_scale / total_child_candles
```

### `alternating_ratio`

```text
alternating_ratio =
	alternating_candles * norm_scale / total_child_candles
```

## Layered Scoring Features

The main layered scorer is:

- `compute_layered_span_errors(...)`

It evaluates one candidate child span as both:

- `trend`
- `range`

and returns:

- `trend_error`
- `range_error`

The smaller error wins.

### Common Intermediate Values

Let:

```text
total_child_move_sum =
	sequential_net_move_sum_raw + alternating_net_move_sum_raw
```

If final wave direction is bullish:

```text
aligned_seq_move_sum   = sequential_bull_net_move_sum_raw
counter_seq_move_sum   = sequential_bear_net_move_sum_raw
aligned_alt_move_sum   = alternating_bull_net_move_sum_raw
counter_alt_move_sum   = alternating_bear_net_move_sum_raw
```

If final wave direction is bearish, bull/bear are swapped.

Then:

### `aligned_seq_share`

```text
aligned_seq_share =
	aligned_seq_move_sum * norm_scale / max(total_child_move_sum, 1)
```

### `counter_seq_share`

```text
counter_seq_share =
	counter_seq_move_sum * norm_scale / max(total_child_move_sum, 1)
```

### `aligned_alt_share`

```text
aligned_alt_share =
	aligned_alt_move_sum * norm_scale / max(total_child_move_sum, 1)
```

### `counter_alt_share`

```text
counter_alt_share =
	counter_alt_move_sum * norm_scale / max(total_child_move_sum, 1)
```

### `alternating_share`

```text
alternating_share =
	alternating_net_move_sum_raw * norm_scale / max(total_child_move_sum, 1)
```

### `seq_count_ratio`

```text
seq_count_ratio =
	sequential_count * norm_scale / max(child_count, 1)
```

### `alt_count_ratio`

```text
alt_count_ratio =
	alternating_count * norm_scale / max(child_count, 1)
```

### `range_variance_ratio`

First:

```text
avg_child_move =
	(sequential_net_move_sum_raw + alternating_net_move_sum_raw)
	/ max(child_count, 1)
```

Then:

```text
range_variance_ratio =
	max_child_net_move_raw * norm_scale / max(avg_child_move, 1)
```

This is a child-move concentration metric:

- larger values mean one child dominates the wave

## Trend Error Formula

Current trend error is:

```text
trend_error =
	10 * penalty_low(aligned_seq_share, 520) +
	 5 * penalty_low(aligned_alt_share, 90) +
	 6 * penalty_high(alternating_share, 520) +
	10 * penalty_high(counter_seq_share, 220) +
	 8 * penalty_high(counter_alt_share, 260) +
	 7 * penalty_low(seq_count_ratio, 560) +
	 7 * penalty_low(sequential_ratio, 580) +
	 6 * penalty_high(alt_count_ratio, 440) +
	 6 * penalty_high(alternating_ratio, 420) +
	 6 * penalty_low(directional_agreement, 760) +
	 5 * penalty_low(relative_directional_efficiency_step,
			relative_trend_der_step_min - 60) +
	 5 * penalty_low(relative_directional_efficiency_body,
			relative_trend_der_body_min - 60) +
	 3 * penalty_low(relative_avg_body_efficiency,
			relative_trend_body_eff_min - 40) +
	 4 * penalty_high(relative_overlap_ratio, trend_overlap_ceiling) +
	 4 * penalty_high(sign_change_density, trend_sign_ceiling) +
	 4 * penalty_high(relative_retracement_ratio, trend_retrace_ceiling) +
	 3 * penalty_low(abs(net_move_raw), drift_floor)
```

Where:

```text
drift_floor          = max(minimum_pips_for_trend * 5, 28)
trend_sign_ceiling   = max(relative_trend_sign_flip_max - 40, 220)
trend_overlap_ceiling= max(relative_trend_overlap_max + 80, 180)
trend_retrace_ceiling= max(relative_trend_retracement_max + 120, 220)
```

Interpretation:

- reward strong aligned sequential displacement
- allow some aligned alternating displacement
- penalize too much alternating structure overall
- penalize counter-direction displacement
- require enough sequential children
- require directional agreement
- require enough DER and body efficiency
- penalize overlap, sign changes, retracement
- require enough total drift

## Range Error Formula

Current range error is:

```text
range_error =
	10 * penalty_low(alternating_share, 420) +
	 8 * penalty_high(aligned_seq_share, 480) +
	 6 * penalty_high(aligned_alt_share, 420) +
	 7 * penalty_high(counter_seq_share, 280) +
	 4 * penalty_high(counter_alt_share, 420) +
	 6 * penalty_low(alt_count_ratio, 520) +
	 7 * penalty_low(alternating_ratio, 520) +
	 5 * penalty_high(seq_count_ratio, 560) +
	 5 * penalty_high(sequential_ratio, 560) +
	10 * penalty_high(range_variance_ratio, 1800) +
	 5 * penalty_high(directional_agreement, 660) +
	 4 * penalty_high(relative_directional_efficiency_step,
			relative_range_der_step_max + 80) +
	 4 * penalty_high(relative_directional_efficiency_body,
			relative_range_der_step_max + 60) +
	 4 * penalty_low(sign_change_density, range_sign_floor) +
	 4 * penalty_low(relative_overlap_ratio, range_overlap_floor) +
	 4 * penalty_high(abs(net_move_raw), drift_floor * 3)
```

Where:

```text
range_sign_floor  = max(relative_range_sign_flip_min - 120, 180)
range_overlap_floor = max(relative_range_overlap_min - 140, 120)
```

Interpretation:

- reward alternating dominance
- penalize too much aligned directional structure
- penalize too much sequential content
- penalize one-child displacement outliers
- penalize too much directional agreement
- penalize strong DER
- require enough sign changes and overlap
- penalize too much total drift

## Short-Wave Penalty

Both class scores get extra penalty for very small child counts:

```text
if child_count == 1:
	trend_error += segment_penalty_lambda * 4
	range_error += segment_penalty_lambda * 4

if child_count == 2:
	trend_error += segment_penalty_lambda * 2
	range_error += segment_penalty_lambda * 2
```

This is a soft preference for waves built from at least `3` strings.

## Hard Range Invalidity

There is one remaining hard invalidation:

```text
if alternating_count == 0:
	range_error += 1000000
```

So a `range` must contain at least one `alternating_string`.

## DP Bias

After `trend_error` vs `range_error`, the DP also adds a class bias:

- `layered_dp_class_bias(...)`

Let:

```text
move_raw = abs(net_move_raw)
candle_span = end_candle - start_candle + 1
unit = max(minimum_pips_for_trend, 1)
```

Then:

### Trend Bias

```text
trend_bias = -(move_raw * 4) + candle_span * unit * 3 - segment_penalty_lambda
```

Interpretation:

- reward larger trend displacement
- penalize longer trend length in candles

### Range Bias

```text
range_bias = move_raw * 3 - candle_span * unit * 10
```

Interpretation:

- penalize range displacement
- reward longer range duration in candles

This is the current implementation of:

- trends prefer larger pip distance and fewer candles
- ranges prefer more candles and shallower displacement

## Layer 2 Dynamic Programming

The main DP is in:

- `build_layered_tokens(...)`

### DP State

Arrays over layer-1 index:

- `dp_cost[end_index]`
- `parent[end_index]`
- `parent_class[end_index]`
- `best_fit[end_index]`
- `best_second[end_index]`

Meaning:

- best total cost to segment layer-1 children `[0 .. end_index-1]`

### Candidate Span

For every end index, the algorithm tries all starts within:

```text
child_limit = max(48, lmax_candles / 2)
```

For each candidate child span `[child_start .. child_end]`:

1. compute shared candle metrics with `compute_segment_eval(...)`
2. compute child summary with `compute_layered_child_summary(...)`
3. compute:
	- `trend_error`
	- `range_error`
4. choose the lower one
5. add:
	- previous DP cost
	- chosen error
	- `segment_penalty_lambda`
	- class bias

### DP Recurrence

```text
total_cost =
	dp_cost[child_start] +
	chosen_error +
	segment_penalty_lambda +
	layered_dp_class_bias(...)
```

The minimal `total_cost` wins.

### Final DP Class

For each chosen segment:

```text
if trend_error <= range_error:
	class = trend
else:
	class = range
```

## Token Materialization

After DP parent reconstruction, `fill_layered_token(...)` writes the final `WaveToken`.

Important fields copied into final layer-2 tokens:

- span
- `class_id`
- `fit_error`
- `second_best_error`
- `seed_start_candle`
- `seed_end_candle`
- `layered_child_start_index`
- `layered_child_end_index`
- `layered_sequential_count`
- `layered_alternating_count`
- `layered_sequential_candles`
- `layered_alternating_candles`
- `layered_sequential_net_move_sum_raw`
- `layered_alternating_net_move_sum_raw`
- `layered_directional_agreement`

## Post-DP Structural Passes

The initial DP result is not final. `build_layered_tokens(...)` then applies multiple structural rewrite passes.

Current order:

1. `optimize_layered_token_boundaries(...)`
2. `split_layered_edge_outliers(...)`
3. `split_layered_internal_outliers(...)`
4. `split_layered_internal_range_islands(...)`
5. `split_layered_cross_token_range_islands(...)`
6. `merge_same_direction_layered_trends(...)`
7. `merge_adjacent_layered_ranges(...)`
8. `extract_leading_layered_trend_into_following_range(...)`
9. `extract_layered_dominant_trend_blocks(...)`
10. `trim_layered_unbridged_alternating_edges_from_trends(...)`
11. `extract_layered_suffix_trend_from_mixed_trend(...)`
12. `reclassify_layered_tokens(...)`

These passes are now critical. In many practical cases, the final wave structure is determined more by these structural rewrites than by the raw DP split alone.

### Boundary Optimization

`optimize_layered_token_boundaries(...)` only moves the boundary between two existing tokens.

Priority:

1. maximize total trend price amplitude
2. if equal, minimize total trend candle count
3. if equal, minimize total fit error

### Edge Outlier Split

`split_layered_edge_outliers(...)`

Splits off a first or last child when that child’s displacement is at least `3x` the average of the remaining children.

### Internal Opposite Trend Outlier Split

`split_layered_internal_outliers(...)`

Finds two adjacent large opposite sequential children inside one token and splits them out.

### Internal Range Island Split

`split_layered_internal_range_islands(...)`

Rewrites:

- `trend`

into:

- `trend | range | trend`

when a shallow middle block behaves like range and the left/right sides are same-direction trends.

### Cross-Token Range Island Split

`split_layered_cross_token_range_islands(...)`

Same idea, but allowed across two adjacent same-direction trend tokens.

### Same-Direction Trend Merge

`merge_same_direction_layered_trends(...)`

Merges adjacent same-direction trends when the boundary child is a small opposite-direction connector and the merged whole remains a valid trend.

### Adjacent Range Merge

`merge_adjacent_layered_ranges(...)`

Merges adjacent ranges when:

- the merged span is still a good range
- or opposite-direction sequential children inside the merged range cancel enough

The current cancellation metric is:

```text
merged_seq_cancel_ratio =
	min(sequential_bull_net_move_sum_raw, sequential_bear_net_move_sum_raw)
	* norm_scale
	/ max(sequential_net_move_sum_raw, 1)
```

### Leading Trend Extraction Into Following Range

`extract_leading_layered_trend_into_following_range(...)`

If a trend starts with a dominant first child and the remainder with the next range token is shallow and mixed, rewrite as:

- `trend | range`

### Dominant Trend Block Extraction

`extract_layered_dominant_trend_blocks(...)`

Extracts a strong internal 3-child block of the form:

- aligned sequential
- small opposite connector
- aligned sequential

### Unbridged Alternating Edge Trim

`trim_layered_unbridged_alternating_edges_from_trends(...)`

Removes an edge `alternating_string` from a trend when the rest of the wave remains the same-direction trend.

This enforces:

- alternating edges should not stay inside a trend unless they bridge internal trend legs

### Suffix Trend Extraction

`extract_layered_suffix_trend_from_mixed_trend(...)`

Extracts a strong trailing sequential trend child from a mixed trend when the prefix is mostly counter-direction and not a real bridge.

## Final Reclassification

After all structural rewrites:

- `reclassify_layered_tokens(...)`

recomputes each token from its final child span and assigns:

- final `class_id`
- final `fit_error`
- final `second_best_error`

This is important because the final token may no longer match the original DP class after boundary movement or splitting.

## Meaning of `fit_error`

For `layered_waves`, `fit_error` is:

- the chosen class error after the final layered scorer runs

Important limitation of the current implementation:

- `fit_error = 0` does **not** mean mathematically perfect trend or range
- it only means none of the current weighted threshold penalties fired

So the current scorer is:

- threshold-based

not:

- distance-to-ideal based

This explains why a strong but imperfect trend can still appear with:

- `fit_error = 0`

if it passes all the current thresholds.

## Penalty Trace in JSON / GUI

Each final wave JSON includes:

- `penalty_trace`

This is serialized in:

- `trainer/src/json_codec.c`

The trace shows:

- `chosen_class`
- `chosen_error`
- `second_best_error`
- `dp_bias`
- `metrics`
- `trend_terms`
- `range_terms`

Each term includes:

- `name`
- `mode`
- `value`
- `threshold`
- `weight`
- `penalty`

Meaning of `mode`:

- `low`
	- penalize when `value < threshold`
- `high`
	- penalize when `value > threshold`

This is the most precise runtime explanation of why a specific wave became:

- `trend`
- or `range`

## Practical Reading Guide

### Strong Trend Usually Has

- high `aligned_seq_share`
- nonzero or moderate `aligned_alt_share`
- low `counter_seq_share`
- low `counter_alt_share`
- low `alternating_share`
- high `directional_agreement`
- high `relative_directional_efficiency_step`
- high `relative_directional_efficiency_body`
- low `relative_retracement_ratio`

### Strong Range Usually Has

- high `alternating_share`
- low total drift
- lower DER
- low or mixed directional agreement
- enough sign changes
- enough overlap
- low child-displacement concentration

## Files To Update When Changing Classification

If you change layered classification logic, the main files to review are:

- `trainer/src/wave_dp.c`
	- all formulas and post-passes
- `trainer/src/json_codec.c`
	- penalty trace serialization
- `gui/src/main_window.cpp`
	- penalty dialog / property views if you expose new fields

If you add a new layered metric, the safest path is:

1. compute it in `compute_layered_child_summary(...)` or `compute_segment_eval(...)`
2. use it inside `compute_layered_span_errors(...)`
3. serialize it into penalty trace in `json_codec.c`
4. expose it in GUI if needed

## Summary

The current `layered_waves` classifier is:

1. build strict layer-1 strings
2. run DP over contiguous child-string spans
3. classify each candidate span by weighted `trend_error` vs `range_error`
4. add class bias favoring:
	- trend = larger move, fewer candles
	- range = smaller move, more candles
5. apply multiple structural rewrite passes
6. reclassify final tokens

So the final wave class is the result of:

- raw candle metrics
- child-string composition
- DP cost
- structural rewrite passes

not just a single threshold or a single DP decision.
