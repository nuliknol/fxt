#include "wave_predictor.h"

#include "api_types.h"
#include "internal_store.h"

#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WP_MAX_INPUT_LEN 8
#define WP_MAX_HORIZON 8
#define WP_FEATURE_COUNT 11
#define WP_DIRECTION_COUNT 3
#define WP_MOVE_BUCKET_COUNT 7
#define WP_MAX_CLASS_COUNT 7
#define WP_MAX_SIGNALS 6
#define WP_MAX_ACTIVE_PREDICATES 1024
#define WP_SCORE_TEMPERATURE 3.0
#define WP_DEFAULT_MAX_SCORING_FAMILIES 8
#define WP_CLASS_MAX_SCORING_FAMILIES 4
#define WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH 512.0
#define WP_CLASS_CONDITIONAL_PRIOR_STRENGTH 4096.0

typedef enum {
	WP_PRED_POSITION_EQ = 0,
	WP_PRED_CONTAINS = 1,
	WP_PRED_COUNT_GE = 2,
	WP_PRED_MAJORITY = 3,
	WP_PRED_PAIR_SAME_TOKEN = 4
} WavePredictorPredicateKind;

typedef struct {
	const char *label;
	double contribution;
	uint32_t support;
	double lift;
} WavePredictorSignal;

typedef struct {
	double probs[WP_MAX_CLASS_COUNT];
	double scores[WP_MAX_CLASS_COUNT];
	uint32_t counts[WP_MAX_CLASS_COUNT];
	uint32_t total;
	int best_index;
	WavePredictorSignal signals[WP_MAX_SIGNALS];
	uint32_t signal_count;
} WavePredictorTargetResult;

typedef struct {
	uint32_t support;
	uint32_t counts[WP_MAX_CLASS_COUNT];
	char label[96];
	char family[64];
} WavePredictorFeatureStat;

typedef struct {
	const WavePredictorFeatureStat *feature;
	double contribution;
	double cond_prob;
	double lift;
} WavePredictorFamilyTrace;

typedef struct {
	uint8_t kind;
	int8_t rel_pos;
	int8_t feature_a;
	int8_t value_a;
	int8_t feature_b;
	int8_t value_b;
	int8_t threshold;
	char label[96];
	char family[64];
} WavePredictorPredicate;

typedef struct {
	int32_t pip_scale;
	int32_t abs_thresholds[WP_FEATURE_COUNT][5];
	int32_t signed_move_thresholds[3];
	int32_t signed_move_centers[WP_MOVE_BUCKET_COUNT];
} WavePredictorBucketConfig;

static int append(char *dst, size_t cap, size_t *used, const char *fmt, ...);
static int predictor_class_count(int32_t wave_model);
static int predictor_direction_index(int dominant_direction);
static int predictor_signed_move_bucket(int32_t net_move_raw, const WavePredictorBucketConfig *cfg);
static int predictor_move_bucket_center(int bucket, const WavePredictorBucketConfig *cfg);
static int predictor_feature_value(const WaveToken *tok, int feature_id, const WavePredictorBucketConfig *cfg);
static const char *predictor_feature_name(int feature_id);
static const char *predictor_feature_value_name(int32_t wave_model, int feature_id, int value);
static int predictor_feature_value_count(int32_t wave_model, int feature_id);
static int predictor_feature_uses_abs_buckets(int feature_id);
static int predictor_feature_uses_ratio_buckets(int feature_id);
static int predictor_feature_uses_duration_buckets(int feature_id);
static int predictor_compare_i32(const void *a, const void *b);
static int32_t predictor_select_quantile_i32(const int32_t *values, uint32_t count, double q);
static void predictor_build_bucket_config(const DatasetRecord *rec, WavePredictorBucketConfig *cfg);
static int32_t predictor_abs_i32(int32_t value);
static int32_t predictor_max_i32(int32_t a, int32_t b);
static int predictor_family_index(char families[][64], uint32_t *family_count, const char *family);
static const char *predictor_class_name(int32_t wave_model, int class_id);
static const char *predictor_direction_name(int dir_index);
static const char *predictor_move_bucket_name(int bucket);
static const char *predictor_target_value_name(int target_kind, int32_t wave_model, int value);
static uint32_t predictor_collect_family_traces(const WavePredictorTargetResult *result,
	const WavePredictorFeatureStat *feature_stats,
	uint32_t feature_count,
	int value_count,
	WavePredictorFamilyTrace *out_items);
static int predictor_encode_target_trace_json(char *out,
	size_t out_cap,
	size_t *used,
	const char *key,
	int target_kind,
	int32_t wave_model,
	const WavePredictorTargetResult *result,
	const WavePredictorFeatureStat *feature_stats,
	uint32_t feature_count,
	int value_count);
static int predictor_predicate_eq(const WavePredictorPredicate *a, const WavePredictorPredicate *b);
static int predictor_add_predicate(WavePredictorPredicate *items, uint32_t *count, const WavePredictorPredicate *item);
static int predictor_build_window_predicates(const DatasetRecord *rec,
	uint32_t window_start,
	uint32_t input_len,
	const WavePredictorBucketConfig *cfg,
	WavePredictorPredicate *out_items,
	uint32_t *out_count);
static int predictor_predicate_matches_window(const DatasetRecord *rec,
	uint32_t window_start,
	uint32_t input_len,
	const WavePredictorBucketConfig *cfg,
	const WavePredictorPredicate *pred);
static void predictor_init_result(WavePredictorTargetResult *result, int value_count);
static void predictor_finalize_target(WavePredictorTargetResult *result,
	const WavePredictorFeatureStat *feature_stats,
	uint32_t feature_count,
	int value_count,
	uint32_t min_class_support,
	double prior_strength,
	uint32_t max_scoring_families);

static double predictor_feature_weight(const WavePredictorFeatureStat *feature_stat)
{
	double weight;

	if (feature_stat == NULL) {
		return 0.0;
	}
	weight = log(1.0 + (double)feature_stat->support);
	if (strncmp(feature_stat->label, "contains_pair(", strlen("contains_pair(")) == 0) {
		weight *= 0.35;
	}
	return weight;
}

static double predictor_smoothed_cond_prob(uint32_t target_count,
	uint32_t support,
	double base_prob,
	double prior_strength)
{
	double denom;

	if (base_prob < 0.0) {
		base_prob = 0.0;
	}
	if (base_prob > 1.0) {
		base_prob = 1.0;
	}
	denom = (double)support + prior_strength;
	if (denom <= 0.0) {
		return base_prob;
	}
	return ((double)target_count + (prior_strength * base_prob)) / denom;
}

static uint32_t predictor_select_top_family_contributions(const double *family_best,
	uint32_t family_count,
	double *out_values,
	uint32_t out_cap)
{
	uint32_t selected = 0u;
	uint32_t i;

	if (family_best == NULL || out_values == NULL || out_cap == 0u) {
		return 0u;
	}
	for (i = 0u; i < family_count; ++i) {
		double value = family_best[i];
		uint32_t pos;

		if (value == 0.0) {
			continue;
		}
		pos = selected;
		while (pos > 0u && fabs(out_values[pos - 1u]) < fabs(value)) {
			if (pos < out_cap) {
				out_values[pos] = out_values[pos - 1u];
			}
			pos -= 1u;
		}
		if (pos < out_cap) {
			out_values[pos] = value;
			if (selected < out_cap) {
				selected += 1u;
			}
		}
	}
	if (selected > out_cap) {
		selected = out_cap;
	}
	return selected;
}

static void predictor_fill_base_probs(const WavePredictorTargetResult *result,
	int value_count,
	double *out_probs)
{
	int v;
	double denom;

	if (out_probs == NULL) {
		return;
	}
	denom = (double)result->total + (double)value_count;
	if (denom <= 0.0) {
		denom = 1.0;
	}
	for (v = 0; v < value_count; ++v) {
		out_probs[v] = ((double)result->counts[v] + 1.0) / denom;
	}
}

static int32_t predictor_abs_i32(int32_t value)
{
	return (value < 0) ? -value : value;
}

static int32_t predictor_max_i32(int32_t a, int32_t b)
{
	return (a > b) ? a : b;
}

static const char *predictor_target_value_name(int target_kind, int32_t wave_model, int value)
{
	switch (target_kind) {
	case 0:
		return predictor_class_name(wave_model, value);
	case 1:
		return predictor_direction_name(value);
	default:
		return predictor_move_bucket_name(value);
	}
}

static uint32_t predictor_collect_family_traces(const WavePredictorTargetResult *result,
	const WavePredictorFeatureStat *feature_stats,
	uint32_t feature_count,
	int value_count,
	WavePredictorFamilyTrace *out_items)
{
	char families[WP_MAX_ACTIVE_PREDICATES][64];
	double family_best[WP_MAX_ACTIVE_PREDICATES];
	uint32_t family_count = 0u;
	uint32_t i;
	int target_index;

	if (result == NULL || feature_stats == NULL || out_items == NULL) {
		return 0u;
	}
	memset(family_best, 0, sizeof(family_best));
	memset(out_items, 0, sizeof(WavePredictorFamilyTrace) * WP_MAX_ACTIVE_PREDICATES);
	target_index = result->best_index;
	for (i = 0u; i < feature_count; ++i) {
		double base_prob;
		double cond_prob;
		double lift;
		double weight;
		double contribution;
		int family_index;

		if (feature_stats[i].support < 4u) {
			continue;
		}
		base_prob = ((double)result->counts[target_index] + 1.0) /
			((double)result->total + (double)value_count);
		cond_prob = predictor_smoothed_cond_prob(feature_stats[i].counts[target_index],
			feature_stats[i].support,
			base_prob,
			value_count);
		lift = cond_prob / base_prob;
		weight = predictor_feature_weight(&feature_stats[i]);
		contribution = weight * log(lift);
		if (contribution <= 0.0) {
			continue;
		}
		family_index = predictor_family_index(families, &family_count, feature_stats[i].family);
		if (family_index < 0) {
			continue;
		}
		if (out_items[family_index].feature == NULL || family_best[family_index] < contribution) {
			family_best[family_index] = contribution;
			out_items[family_index].feature = &feature_stats[i];
			out_items[family_index].contribution = contribution;
			out_items[family_index].cond_prob = cond_prob;
			out_items[family_index].lift = lift;
		}
	}
	return family_count;
}

static int predictor_encode_target_trace_json(char *out,
	size_t out_cap,
	size_t *used,
	const char *key,
	int target_kind,
	int32_t wave_model,
	const WavePredictorTargetResult *result,
	const WavePredictorFeatureStat *feature_stats,
	uint32_t feature_count,
	int value_count)
{
	WavePredictorFamilyTrace families[WP_MAX_ACTIVE_PREDICATES];
	uint32_t family_count;
	int first_family = 1;
	int v;

	if (out == NULL || used == NULL || key == NULL || result == NULL || feature_stats == NULL) {
		return -1;
	}
	family_count = predictor_collect_family_traces(result, feature_stats, feature_count, value_count, families);
	if (append(out,
		out_cap,
		used,
		"\"%s\":{\"selected_index\":%d,\"selected_label\":\"%s\",\"base_count\":%u,\"total\":%u,\"value_count\":%d,\"temperature\":%.2f,\"family_count\":%u,\"score_values\":[",
		key,
		result->best_index,
		predictor_target_value_name(target_kind, wave_model, result->best_index),
		result->counts[result->best_index],
		result->total,
		value_count,
		WP_SCORE_TEMPERATURE,
		family_count) != 0) {
		return -1;
	}
	for (v = 0; v < value_count; ++v) {
		if (v > 0 && append(out, out_cap, used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			used,
			"{\"index\":%d,\"label\":\"%s\",\"count\":%u,\"score\":%.6f,\"probability\":%.6f}",
			v,
			predictor_target_value_name(target_kind, wave_model, v),
			result->counts[v],
			result->scores[v],
			result->probs[v]) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, used, "],\"families\":[") != 0) {
		return -1;
	}
	for (v = 0; v < (int)family_count; ++v) {
		if (families[v].feature == NULL || families[v].contribution <= 0.0) {
			continue;
		}
		if (!first_family && append(out, out_cap, used, ",") != 0) {
			return -1;
		}
		first_family = 0;
		if (append(out,
			out_cap,
			used,
			"{\"label\":\"%s\",\"support\":%u,\"target_count\":%u,\"cond_prob\":%.6f,\"lift\":%.6f,\"contribution\":%.6f}",
			families[v].feature->label,
			families[v].feature->support,
			families[v].feature->counts[result->best_index],
			families[v].cond_prob,
			families[v].lift,
			families[v].contribution) != 0) {
			return -1;
		}
	}
	return append(out, out_cap, used, "]}");
}

static int predictor_predicate_eq(const WavePredictorPredicate *a, const WavePredictorPredicate *b)
{
	return a != NULL && b != NULL &&
		a->kind == b->kind &&
		a->rel_pos == b->rel_pos &&
		a->feature_a == b->feature_a &&
		a->value_a == b->value_a &&
		a->feature_b == b->feature_b &&
		a->value_b == b->value_b &&
		a->threshold == b->threshold;
}

static int predictor_add_predicate(WavePredictorPredicate *items, uint32_t *count, const WavePredictorPredicate *item)
{
	uint32_t i;

	if (items == NULL || count == NULL || item == NULL) {
		return -1;
	}
	for (i = 0u; i < *count; ++i) {
		if (predictor_predicate_eq(&items[i], item)) {
			return 0;
		}
	}
	if (*count >= WP_MAX_ACTIVE_PREDICATES) {
		return -1;
	}
	items[*count] = *item;
	*count += 1u;
	return 0;
}

static int predictor_family_index(char families[][64], uint32_t *family_count, const char *family)
{
	uint32_t i;

	if (families == NULL || family_count == NULL || family == NULL) {
		return -1;
	}
	for (i = 0u; i < *family_count; ++i) {
		if (strcmp(families[i], family) == 0) {
			return (int)i;
		}
	}
	if (*family_count >= WP_MAX_ACTIVE_PREDICATES) {
		return -1;
	}
	snprintf(families[*family_count], sizeof(families[*family_count]), "%s", family);
	*family_count += 1u;
	return (int)(*family_count - 1u);
}

static int predictor_build_window_predicates(const DatasetRecord *rec,
	uint32_t window_start,
	uint32_t input_len,
	const WavePredictorBucketConfig *cfg,
	WavePredictorPredicate *out_items,
	uint32_t *out_count)
{
	uint32_t counts[WP_FEATURE_COUNT][WP_MAX_CLASS_COUNT];
	uint32_t rel_pos;
	uint32_t i;
	int feature_id;
	int value;
	WavePredictorPredicate pred;

	if (rec == NULL || cfg == NULL || out_items == NULL || out_count == NULL) {
		return -1;
	}
	*out_count = 0u;
	memset(counts, 0, sizeof(counts));
	for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
		const WaveToken *tok = &rec->waves.tokens[window_start + rel_pos];

		for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
			value = predictor_feature_value(tok, feature_id, cfg);
			if (value >= 0 && value < WP_MAX_CLASS_COUNT) {
				counts[feature_id][value] += 1u;
			}

			memset(&pred, 0, sizeof(pred));
			pred.kind = WP_PRED_POSITION_EQ;
			pred.rel_pos = (int8_t)((int)rel_pos - (int)input_len + 1);
			pred.feature_a = (int8_t)feature_id;
			pred.value_a = (int8_t)value;
			snprintf(pred.label,
				sizeof(pred.label),
				"w[%d].%s=%s",
				(int)pred.rel_pos,
				predictor_feature_name(feature_id),
				predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value));
			snprintf(pred.family,
				sizeof(pred.family),
				"fv:%d:%d",
				feature_id,
				value);
			(void)predictor_add_predicate(out_items, out_count, &pred);
		}
	}
	for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
		int value_count = predictor_feature_value_count(rec->waves.params.wave_model, feature_id);
		uint32_t best_count = 0u;
		int best_value = -1;

		for (value = 0; value < value_count; ++value) {
			uint32_t seen = counts[feature_id][value];

			if (seen == 0u) {
				continue;
			}
			memset(&pred, 0, sizeof(pred));
			pred.kind = WP_PRED_CONTAINS;
			pred.feature_a = (int8_t)feature_id;
			pred.value_a = (int8_t)value;
			snprintf(pred.label,
				sizeof(pred.label),
				"contains(%s=%s)",
				predictor_feature_name(feature_id),
				predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value));
			snprintf(pred.family,
				sizeof(pred.family),
				"fv:%d:%d",
				feature_id,
				value);
			(void)predictor_add_predicate(out_items, out_count, &pred);

			memset(&pred, 0, sizeof(pred));
			pred.kind = WP_PRED_COUNT_GE;
			pred.feature_a = (int8_t)feature_id;
			pred.value_a = (int8_t)value;
			pred.threshold = (int8_t)((seen >= 3u) ? 3 : (int8_t)seen);
			snprintf(pred.label,
				sizeof(pred.label),
				"count(%s=%s)>=%d",
				predictor_feature_name(feature_id),
				predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
				(int)pred.threshold);
			snprintf(pred.family,
				sizeof(pred.family),
				"fv:%d:%d",
				feature_id,
				value);
			(void)predictor_add_predicate(out_items, out_count, &pred);

			if (seen > best_count) {
				best_count = seen;
				best_value = value;
			}
		}
		if ((feature_id == 0 || feature_id == 1) && best_value >= 0 && best_count > input_len / 2u) {
			memset(&pred, 0, sizeof(pred));
			pred.kind = WP_PRED_MAJORITY;
			pred.feature_a = (int8_t)feature_id;
			pred.value_a = (int8_t)best_value;
			snprintf(pred.label,
				sizeof(pred.label),
				"majority(%s=%s)",
				predictor_feature_name(feature_id),
				predictor_feature_value_name(rec->waves.params.wave_model, feature_id, best_value));
			snprintf(pred.family,
				sizeof(pred.family),
				"fv:%d:%d",
				feature_id,
				best_value);
			(void)predictor_add_predicate(out_items, out_count, &pred);
		}
	}
	for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
		const WaveToken *tok = &rec->waves.tokens[window_start + rel_pos];
		int values[WP_FEATURE_COUNT];
		int feature_b;

		for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
			values[feature_id] = predictor_feature_value(tok, feature_id, cfg);
		}
		for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
			for (feature_b = feature_id + 1; feature_b < WP_FEATURE_COUNT; ++feature_b) {
				memset(&pred, 0, sizeof(pred));
				pred.kind = WP_PRED_PAIR_SAME_TOKEN;
				pred.feature_a = (int8_t)feature_id;
				pred.value_a = (int8_t)values[feature_id];
				pred.feature_b = (int8_t)feature_b;
				pred.value_b = (int8_t)values[feature_b];
				snprintf(pred.label,
					sizeof(pred.label),
					"contains_pair(%s=%s & %s=%s)",
					predictor_feature_name(feature_id),
					predictor_feature_value_name(rec->waves.params.wave_model, feature_id, values[feature_id]),
					predictor_feature_name(feature_b),
					predictor_feature_value_name(rec->waves.params.wave_model, feature_b, values[feature_b]));
				snprintf(pred.family,
					sizeof(pred.family),
					"pair:%d:%d:%d:%d",
					feature_id,
					values[feature_id],
					feature_b,
					values[feature_b]);
				(void)predictor_add_predicate(out_items, out_count, &pred);
			}
		}
	}
	for (i = 0u; i < *out_count; ++i) {
		out_items[i].label[sizeof(out_items[i].label) - 1u] = '\0';
		out_items[i].family[sizeof(out_items[i].family) - 1u] = '\0';
	}
	return 0;
}

static int predictor_predicate_matches_window(const DatasetRecord *rec,
	uint32_t window_start,
	uint32_t input_len,
	const WavePredictorBucketConfig *cfg,
	const WavePredictorPredicate *pred)
{
	uint32_t rel_pos;

	if (rec == NULL || cfg == NULL || pred == NULL) {
		return 0;
	}
	switch (pred->kind) {
	case WP_PRED_POSITION_EQ: {
		int index = (int)input_len + (int)pred->rel_pos - 1;
		if (index < 0 || (uint32_t)index >= input_len) {
			return 0;
		}
		return predictor_feature_value(&rec->waves.tokens[window_start + (uint32_t)index], pred->feature_a, cfg) == pred->value_a;
	}
	case WP_PRED_CONTAINS:
		for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
			if (predictor_feature_value(&rec->waves.tokens[window_start + rel_pos], pred->feature_a, cfg) == pred->value_a) {
				return 1;
			}
		}
		return 0;
	case WP_PRED_COUNT_GE: {
		uint32_t count = 0u;
		for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
			if (predictor_feature_value(&rec->waves.tokens[window_start + rel_pos], pred->feature_a, cfg) == pred->value_a) {
				count += 1u;
			}
		}
		return count >= (uint32_t)pred->threshold;
	}
	case WP_PRED_MAJORITY: {
		uint32_t count = 0u;
		for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
			if (predictor_feature_value(&rec->waves.tokens[window_start + rel_pos], pred->feature_a, cfg) == pred->value_a) {
				count += 1u;
			}
		}
		return count > input_len / 2u;
	}
	case WP_PRED_PAIR_SAME_TOKEN:
		for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
			const WaveToken *tok = &rec->waves.tokens[window_start + rel_pos];
			if (predictor_feature_value(tok, pred->feature_a, cfg) == pred->value_a &&
				predictor_feature_value(tok, pred->feature_b, cfg) == pred->value_b) {
				return 1;
			}
		}
		return 0;
	default:
		return 0;
	}
}

static double predictor_safe_lift(double conditional_prob, double base_prob)
{
	if (base_prob <= 0.0) {
		return 0.0;
	}
	return conditional_prob / base_prob;
}

static double predictor_log2_safe(double value)
{
	if (value <= 0.0) {
		return 0.0;
	}
	return log(value) / log(2.0);
}

static double predictor_distribution_kl_bits(const double *probs,
	const double *base_probs,
	int value_count)
{
	double kl = 0.0;
	int v;

	if (probs == NULL || base_probs == NULL) {
		return 0.0;
	}
	for (v = 0; v < value_count; ++v) {
		if (probs[v] <= 0.0 || base_probs[v] <= 0.0) {
			continue;
		}
		kl += probs[v] * predictor_log2_safe(probs[v] / base_probs[v]);
	}
	return kl;
}

typedef struct {
	uint32_t sample_count;
	int class_count;
	int actual_class_index;
	int actual_dir_index;
	int actual_move_index;
	WavePredictorTargetResult class_result;
	WavePredictorTargetResult direction_result;
	WavePredictorTargetResult move_result;
	double class_base_probs[WP_MAX_CLASS_COUNT];
	double direction_base_probs[WP_MAX_CLASS_COUNT];
	double move_base_probs[WP_MAX_CLASS_COUNT];
	double expected_move;
	double baseline_expected_move;
} WavePredictorEvalPoint;

static int predictor_probability_bucket(double prob)
{
	if (prob < 0.5) {
		return 0;
	}
	if (prob < 0.6) {
		return 1;
	}
	if (prob < 0.7) {
		return 2;
	}
	if (prob < 0.8) {
		return 3;
	}
	if (prob < 0.9) {
		return 4;
	}
	return 5;
}

static const char *predictor_probability_bucket_name(int bucket)
{
	switch (bucket) {
	case 0:
		return "0_50";
	case 1:
		return "50_60";
	case 2:
		return "60_70";
	case 3:
		return "70_80";
	case 4:
		return "80_90";
	default:
		return "90_100";
	}
}

static int predictor_sign_i32(int32_t value)
{
	if (value < 0) {
		return -1;
	}
	if (value > 0) {
		return 1;
	}
	return 0;
}

static int predictor_sign_f64(double value)
{
	if (value < 0.0) {
		return -1;
	}
	if (value > 0.0) {
		return 1;
	}
	return 0;
}

static int predictor_feature_uses_abs_buckets(int feature_id)
{
	return (feature_id == 2 || feature_id == 7 || feature_id == 8) ? 1 : 0;
}

static int predictor_feature_uses_ratio_buckets(int feature_id)
{
	return (feature_id == 4 || feature_id == 5 || feature_id == 6 || feature_id == 9) ? 1 : 0;
}

static int predictor_feature_uses_duration_buckets(int feature_id)
{
	return (feature_id == 3) ? 1 : 0;
}

static int predictor_compare_i32(const void *a, const void *b)
{
	const int32_t va = *(const int32_t *)a;
	const int32_t vb = *(const int32_t *)b;

	if (va < vb) {
		return -1;
	}
	if (va > vb) {
		return 1;
	}
	return 0;
}

static int32_t predictor_select_quantile_i32(const int32_t *values, uint32_t count, double q)
{
	uint32_t index;

	if (values == NULL || count == 0u) {
		return 1;
	}
	if (q <= 0.0) {
		return values[0];
	}
	if (q >= 1.0) {
		return values[count - 1u];
	}
	index = (uint32_t)(q * (double)(count - 1u));
	if (index >= count) {
		index = count - 1u;
	}
	return values[index];
}

static void predictor_build_bucket_config(const DatasetRecord *rec, WavePredictorBucketConfig *cfg)
{
	int32_t feature_samples[WP_FEATURE_COUNT][16384];
	uint32_t feature_counts[WP_FEATURE_COUNT];
	int32_t move_samples[16384];
	uint32_t move_count = 0u;
	uint32_t token_index;
	uint32_t pip_scale;
	int feature_id;

	if (cfg == NULL) {
		return;
	}
	memset(cfg, 0, sizeof(*cfg));
	cfg->pip_scale = 1;
	for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
		cfg->abs_thresholds[feature_id][0] = 1;
		cfg->abs_thresholds[feature_id][1] = 2;
		cfg->abs_thresholds[feature_id][2] = 5;
		cfg->abs_thresholds[feature_id][3] = 10;
		cfg->abs_thresholds[feature_id][4] = 20;
	}
	cfg->signed_move_thresholds[0] = 1;
	cfg->signed_move_thresholds[1] = 3;
	cfg->signed_move_thresholds[2] = 8;
	cfg->signed_move_centers[0] = -8;
	cfg->signed_move_centers[1] = -3;
	cfg->signed_move_centers[2] = -1;
	cfg->signed_move_centers[3] = 0;
	cfg->signed_move_centers[4] = 1;
	cfg->signed_move_centers[5] = 3;
	cfg->signed_move_centers[6] = 8;
	if (rec == NULL || rec->waves.tokens == NULL || rec->waves.token_count == 0u) {
		return;
	}
	memset(feature_counts, 0, sizeof(feature_counts));
	pip_scale = rec->meta.pip_scale;
	if (pip_scale == 0u) {
		pip_scale = 1u;
	}
	cfg->pip_scale = (int32_t)pip_scale;
	for (token_index = 0u; token_index < rec->waves.token_count; ++token_index) {
		const WaveToken *tok = &rec->waves.tokens[token_index];
		int32_t normalized_move;

		normalized_move = predictor_abs_i32(tok->net_move_raw) / (int32_t)pip_scale;
		if (move_count < (sizeof(move_samples) / sizeof(move_samples[0]))) {
			move_samples[move_count] = normalized_move;
			move_count += 1u;
		}
		for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
			int32_t sample_value;
			uint32_t count;

			if (!predictor_feature_uses_abs_buckets(feature_id)) {
				continue;
			}
			switch (feature_id) {
			case 2:
				sample_value = predictor_abs_i32(tok->net_move_raw) / (int32_t)pip_scale;
				break;
			case 7:
				sample_value = predictor_abs_i32(tok->relative_upper_wick_sum_raw) / (int32_t)pip_scale;
				break;
			case 8:
				sample_value = predictor_abs_i32(tok->relative_lower_wick_sum_raw) / (int32_t)pip_scale;
				break;
			default:
				sample_value = 0;
				break;
			}
			count = feature_counts[feature_id];
			if (count < (sizeof(feature_samples[feature_id]) / sizeof(feature_samples[feature_id][0]))) {
				feature_samples[feature_id][count] = sample_value;
				feature_counts[feature_id] = count + 1u;
			}
		}
	}
	for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
		uint32_t count = feature_counts[feature_id];
		int32_t *samples = feature_samples[feature_id];
		int32_t q0;
		int32_t q1;
		int32_t q2;
		int32_t q3;
		int32_t q4;

		if (!predictor_feature_uses_abs_buckets(feature_id) || count == 0u) {
			continue;
		}
		qsort(samples, count, sizeof(samples[0]), predictor_compare_i32);
		q0 = predictor_select_quantile_i32(samples, count, 0.10);
		q1 = predictor_select_quantile_i32(samples, count, 0.25);
		q2 = predictor_select_quantile_i32(samples, count, 0.50);
		q3 = predictor_select_quantile_i32(samples, count, 0.75);
		q4 = predictor_select_quantile_i32(samples, count, 0.90);
		cfg->abs_thresholds[feature_id][0] = predictor_max_i32(q0, 1);
		cfg->abs_thresholds[feature_id][1] = predictor_max_i32(q1, cfg->abs_thresholds[feature_id][0] + 1);
		cfg->abs_thresholds[feature_id][2] = predictor_max_i32(q2, cfg->abs_thresholds[feature_id][1] + 1);
		cfg->abs_thresholds[feature_id][3] = predictor_max_i32(q3, cfg->abs_thresholds[feature_id][2] + 1);
		cfg->abs_thresholds[feature_id][4] = predictor_max_i32(q4, cfg->abs_thresholds[feature_id][3] + 1);
	}
	if (move_count > 0u) {
		int32_t q0;
		int32_t q1;
		int32_t q2;

		qsort(move_samples, move_count, sizeof(move_samples[0]), predictor_compare_i32);
		q0 = predictor_select_quantile_i32(move_samples, move_count, 0.15);
		q1 = predictor_select_quantile_i32(move_samples, move_count, 0.45);
		q2 = predictor_select_quantile_i32(move_samples, move_count, 0.80);
		cfg->signed_move_thresholds[0] = predictor_max_i32(q0, 1);
		cfg->signed_move_thresholds[1] = predictor_max_i32(q1, cfg->signed_move_thresholds[0] + 1);
		cfg->signed_move_thresholds[2] = predictor_max_i32(q2, cfg->signed_move_thresholds[1] + 1);
		cfg->signed_move_centers[0] = -cfg->signed_move_thresholds[2];
		cfg->signed_move_centers[1] = -cfg->signed_move_thresholds[1];
		cfg->signed_move_centers[2] = -cfg->signed_move_thresholds[0];
		cfg->signed_move_centers[3] = 0;
		cfg->signed_move_centers[4] = cfg->signed_move_thresholds[0];
		cfg->signed_move_centers[5] = cfg->signed_move_thresholds[1];
		cfg->signed_move_centers[6] = cfg->signed_move_thresholds[2];
	}
}

static int predictor_compute_anchor_horizon(const DatasetRecord *rec,
	uint32_t anchor_token,
	uint32_t input_len,
	uint32_t h,
	WavePredictorEvalPoint *out)
{
	WavePredictorBucketConfig bucket_cfg;
	WavePredictorPredicate predicates[WP_MAX_ACTIVE_PREDICATES];
	uint32_t input_start;
	uint32_t sample_end;
	uint32_t feat_index;
	uint32_t predicate_count = 0u;
	WavePredictorFeatureStat class_features[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat direction_features[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat move_features[WP_MAX_ACTIVE_PREDICATES];
	int i;

	if (rec == NULL || out == NULL || rec->waves.tokens == NULL || rec->waves.token_count == 0u) {
		return -1;
	}
	if (anchor_token >= rec->waves.token_count || h == 0u || anchor_token + h >= rec->waves.token_count) {
		return -1;
	}
	if (input_len == 0u) {
		input_len = 3u;
	}
	if (input_len > WP_MAX_INPUT_LEN || input_len > anchor_token + 1u) {
		input_len = (anchor_token + 1u < WP_MAX_INPUT_LEN) ? (anchor_token + 1u) : WP_MAX_INPUT_LEN;
	}
	predictor_build_bucket_config(rec, &bucket_cfg);
	memset(out, 0, sizeof(*out));
	out->class_count = predictor_class_count(rec->waves.params.wave_model);
	predictor_init_result(&out->class_result, out->class_count);
	predictor_init_result(&out->direction_result, WP_DIRECTION_COUNT);
	predictor_init_result(&out->move_result, WP_MOVE_BUCKET_COUNT);
	memset(class_features, 0, sizeof(class_features));
	memset(direction_features, 0, sizeof(direction_features));
	memset(move_features, 0, sizeof(move_features));
	input_start = anchor_token + 1u - input_len;
	if (predictor_build_window_predicates(rec, input_start, input_len, &bucket_cfg, predicates, &predicate_count) != 0) {
		return -1;
	}
	for (feat_index = 0u; feat_index < predicate_count; ++feat_index) {
		strcpy(class_features[feat_index].label, predicates[feat_index].label);
		strcpy(direction_features[feat_index].label, predicates[feat_index].label);
		strcpy(move_features[feat_index].label, predicates[feat_index].label);
		strcpy(class_features[feat_index].family, predicates[feat_index].family);
		strcpy(direction_features[feat_index].family, predicates[feat_index].family);
		strcpy(move_features[feat_index].family, predicates[feat_index].family);
	}
	for (sample_end = input_len - 1u; sample_end + h < rec->waves.token_count; ++sample_end) {
		const WaveToken *future_tok;
		int class_index;
		int dir_index;
		int move_index;

		future_tok = &rec->waves.tokens[sample_end + h];
		class_index = (int)future_tok->class_id;
		dir_index = predictor_direction_index(future_tok->dominant_direction);
		move_index = predictor_signed_move_bucket(future_tok->net_move_raw, &bucket_cfg);
		if (class_index >= 0 && class_index < out->class_count) {
			out->class_result.counts[class_index] += 1u;
		}
		out->class_result.total += 1u;
		out->direction_result.counts[dir_index] += 1u;
		out->direction_result.total += 1u;
		out->move_result.counts[move_index] += 1u;
		out->move_result.total += 1u;
		for (feat_index = 0u; feat_index < predicate_count; ++feat_index) {
			if (predictor_predicate_matches_window(rec,
				sample_end + 1u - input_len,
				input_len,
				&bucket_cfg,
				&predicates[feat_index])) {
				class_features[feat_index].support += 1u;
				direction_features[feat_index].support += 1u;
				move_features[feat_index].support += 1u;
				if (class_index >= 0 && class_index < out->class_count) {
					class_features[feat_index].counts[class_index] += 1u;
				}
				direction_features[feat_index].counts[dir_index] += 1u;
				move_features[feat_index].counts[move_index] += 1u;
			}
		}
		out->sample_count += 1u;
	}
	if (out->sample_count == 0u) {
		return -1;
	}
	predictor_finalize_target(&out->class_result,
		class_features,
		predicate_count,
		out->class_count,
		1u,
		WP_CLASS_CONDITIONAL_PRIOR_STRENGTH,
		WP_CLASS_MAX_SCORING_FAMILIES);
	predictor_finalize_target(&out->direction_result,
		direction_features,
		predicate_count,
		WP_DIRECTION_COUNT,
		8u,
		WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH,
		WP_DEFAULT_MAX_SCORING_FAMILIES);
	predictor_finalize_target(&out->move_result,
		move_features,
		predicate_count,
		WP_MOVE_BUCKET_COUNT,
		1u,
		WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH,
		WP_DEFAULT_MAX_SCORING_FAMILIES);
	predictor_fill_base_probs(&out->class_result, out->class_count, out->class_base_probs);
	predictor_fill_base_probs(&out->direction_result, WP_DIRECTION_COUNT, out->direction_base_probs);
	predictor_fill_base_probs(&out->move_result, WP_MOVE_BUCKET_COUNT, out->move_base_probs);
	for (i = 0; i < WP_MOVE_BUCKET_COUNT; ++i) {
		out->expected_move += out->move_result.probs[i] * (double)predictor_move_bucket_center(i, &bucket_cfg);
		out->baseline_expected_move += out->move_base_probs[i] * (double)predictor_move_bucket_center(i, &bucket_cfg);
	}
	out->actual_class_index = (int)rec->waves.tokens[anchor_token + h].class_id;
	out->actual_dir_index = predictor_direction_index(rec->waves.tokens[anchor_token + h].dominant_direction);
	out->actual_move_index = predictor_signed_move_bucket(rec->waves.tokens[anchor_token + h].net_move_raw, &bucket_cfg);
	return 0;
}

static int predictor_encode_evaluation_summary_json(const DatasetRecord *rec,
	uint32_t input_len,
	uint32_t max_horizon,
	char *out,
	size_t out_cap,
	size_t *used)
{
	uint32_t h;
	int first_horizon = 1;

	if (rec == NULL || out == NULL || used == NULL) {
		return -1;
	}
	if (append(out, out_cap, used, "],\"evaluation_summary\":{\"horizons\":[") != 0) {
		return -1;
	}
	for (h = 1u; h <= max_horizon; ++h) {
		uint32_t anchor_token;
		uint32_t sample_count = 0u;
		uint32_t class_hits = 0u;
		uint32_t direction_hits = 0u;
		uint32_t move_hits = 0u;
		uint32_t move_sign_hits = 0u;
		uint32_t class_bucket_total[6] = {0u};
		uint32_t class_bucket_hits[6] = {0u};
		uint32_t direction_bucket_total[6] = {0u};
		uint32_t direction_bucket_hits[6] = {0u};
		double sum_class_prob = 0.0;
		double sum_class_lift = 0.0;
		double sum_direction_prob = 0.0;
		double sum_direction_lift = 0.0;
		double sum_expected_edge = 0.0;
		double sum_abs_move_error = 0.0;
		double sum_x = 0.0;
		double sum_y = 0.0;
		double sum_x2 = 0.0;
		double sum_y2 = 0.0;
		double sum_xy = 0.0;
		double move_corr = 0.0;

		for (anchor_token = input_len - 1u; anchor_token + h < rec->waves.token_count; ++anchor_token) {
			WavePredictorEvalPoint point;
			int class_bucket;
			int direction_bucket;
			int actual_move_sign;
			int predicted_move_sign;
			double predicted_class_prob;
			double predicted_direction_prob;
			double realized_move;

			if (predictor_compute_anchor_horizon(rec, anchor_token, input_len, h, &point) != 0) {
				continue;
			}
			sample_count += 1u;
			predicted_class_prob = point.class_result.probs[point.class_result.best_index];
			predicted_direction_prob = point.direction_result.probs[point.direction_result.best_index];
			if (point.class_result.best_index == point.actual_class_index) {
				class_hits += 1u;
			}
			if (point.direction_result.best_index == point.actual_dir_index) {
				direction_hits += 1u;
			}
			if (point.move_result.best_index == point.actual_move_index) {
				move_hits += 1u;
			}
			actual_move_sign = predictor_sign_i32(rec->waves.tokens[anchor_token + h].net_move_raw);
			predicted_move_sign = predictor_sign_f64(point.expected_move);
			if (actual_move_sign == predicted_move_sign) {
				move_sign_hits += 1u;
			}
			class_bucket = predictor_probability_bucket(predicted_class_prob);
			direction_bucket = predictor_probability_bucket(predicted_direction_prob);
			class_bucket_total[class_bucket] += 1u;
			direction_bucket_total[direction_bucket] += 1u;
			if (point.class_result.best_index == point.actual_class_index) {
				class_bucket_hits[class_bucket] += 1u;
			}
			if (point.direction_result.best_index == point.actual_dir_index) {
				direction_bucket_hits[direction_bucket] += 1u;
			}
			sum_class_prob += predicted_class_prob;
			sum_direction_prob += predicted_direction_prob;
			sum_class_lift += predictor_safe_lift(predicted_class_prob,
				point.class_base_probs[point.class_result.best_index]);
			sum_direction_lift += predictor_safe_lift(predicted_direction_prob,
				point.direction_base_probs[point.direction_result.best_index]);
			sum_expected_edge += point.expected_move - point.baseline_expected_move;
			realized_move = (double)rec->waves.tokens[anchor_token + h].net_move_raw;
			sum_abs_move_error += fabs(point.expected_move - realized_move);
			sum_x += point.expected_move;
			sum_y += realized_move;
			sum_x2 += point.expected_move * point.expected_move;
			sum_y2 += realized_move * realized_move;
			sum_xy += point.expected_move * realized_move;
		}
		if (!first_horizon && append(out, out_cap, used, ",") != 0) {
			return -1;
		}
		first_horizon = 0;
		if (sample_count > 1u) {
			double denom_x = (double)sample_count * sum_x2 - sum_x * sum_x;
			double denom_y = (double)sample_count * sum_y2 - sum_y * sum_y;
			double denom = denom_x * denom_y;

			if (denom > 0.0) {
				move_corr = (((double)sample_count * sum_xy) - (sum_x * sum_y)) / sqrt(denom);
			}
		}
		if (append(out,
			out_cap,
			used,
			"{\"horizon\":%u,\"samples\":%u,\"class_accuracy\":%.4f,\"direction_accuracy\":%.4f,\"move_bucket_accuracy\":%.4f,\"move_sign_accuracy\":%.4f,\"avg_predicted_class_probability\":%.4f,\"avg_predicted_class_lift\":%.4f,\"avg_predicted_direction_probability\":%.4f,\"avg_predicted_direction_lift\":%.4f,\"avg_expected_move_edge_raw\":%.4f,\"mean_abs_move_error_raw\":%.4f,\"expected_move_correlation\":%.6f,\"class_calibration\":[",
			h,
			sample_count,
			(sample_count > 0u) ? ((double)class_hits / (double)sample_count) : 0.0,
			(sample_count > 0u) ? ((double)direction_hits / (double)sample_count) : 0.0,
			(sample_count > 0u) ? ((double)move_hits / (double)sample_count) : 0.0,
			(sample_count > 0u) ? ((double)move_sign_hits / (double)sample_count) : 0.0,
			(sample_count > 0u) ? (sum_class_prob / (double)sample_count) : 0.0,
			(sample_count > 0u) ? (sum_class_lift / (double)sample_count) : 0.0,
			(sample_count > 0u) ? (sum_direction_prob / (double)sample_count) : 0.0,
			(sample_count > 0u) ? (sum_direction_lift / (double)sample_count) : 0.0,
			(sample_count > 0u) ? (sum_expected_edge / (double)sample_count) : 0.0,
			(sample_count > 0u) ? (sum_abs_move_error / (double)sample_count) : 0.0,
			move_corr) != 0) {
			return -1;
		}
		for (int b = 0; b < 6; ++b) {
			if (b > 0 && append(out, out_cap, used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				used,
				"{\"bucket\":\"%s\",\"count\":%u,\"accuracy\":%.4f}",
				predictor_probability_bucket_name(b),
				class_bucket_total[b],
				(class_bucket_total[b] > 0u) ? ((double)class_bucket_hits[b] / (double)class_bucket_total[b]) : 0.0) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, used, "],\"direction_calibration\":[") != 0) {
			return -1;
		}
		for (int b = 0; b < 6; ++b) {
			if (b > 0 && append(out, out_cap, used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				used,
				"{\"bucket\":\"%s\",\"count\":%u,\"accuracy\":%.4f}",
				predictor_probability_bucket_name(b),
				direction_bucket_total[b],
				(direction_bucket_total[b] > 0u) ? ((double)direction_bucket_hits[b] / (double)direction_bucket_total[b]) : 0.0) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, used, "]}") != 0) {
			return -1;
		}
	}
	return append(out, out_cap, used, "]}}");
}

static int append(char *dst, size_t cap, size_t *used, const char *fmt, ...)
{
	va_list ap;
	int wrote;

	if (dst == NULL || used == NULL || *used >= cap) {
		return -1;
	}
	va_start(ap, fmt);
	wrote = vsnprintf(dst + *used, cap - *used, fmt, ap);
	va_end(ap);
	if (wrote < 0) {
		return -1;
	}
	*used += (size_t)wrote;
	return (*used < cap) ? 0 : -1;
}

static const char *predictor_class_name(int32_t wave_model, int class_id)
{
	static char buf[32];

	if (wave_model == WAVE_MODEL_PRIMITIVE) {
		switch (class_id) {
		case PRIMITIVE_CLASS_CONSECUTIVE:
			return "sequential_string";
		case PRIMITIVE_CLASS_ALTERNATING:
			return "alternating_string";
		case PRIMITIVE_CLASS_SEPARATOR:
			return "separator_candle";
		default:
			break;
		}
	}
	if (wave_model == WAVE_MODEL_RELATIVE ||
		wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		wave_model == WAVE_MODEL_RELATIVE_ITERATIVE ||
		wave_model == WAVE_MODEL_LAYERED) {
		switch (class_id) {
		case RELATIVE_CLASS_TREND:
			return "trend";
		case RELATIVE_CLASS_RANGE:
			return "range";
		default:
			break;
		}
	}
	switch (class_id) {
	case WAVE_CLASS_STRONG_TREND:
		return "strong_trend";
	case WAVE_CLASS_WEAK_TREND:
		return "weak_trend";
	case WAVE_CLASS_RETRACEMENT:
		return "retracement";
	case WAVE_CLASS_ALTERNATING_CHOP:
		return "alternating_chop";
	case WAVE_CLASS_MIXED_COMPRESSION:
		return "mixed_compression";
	case WAVE_CLASS_VOLATILE_EXPANSION:
		return "volatile_expansion";
	case WAVE_CLASS_V_SHAPED_REVERSAL:
		return "v_shaped_reversal";
	default:
		snprintf(buf, sizeof(buf), "class_%d", class_id);
		return buf;
	}
}

static int predictor_class_count(int32_t wave_model)
{
	if (wave_model == WAVE_MODEL_PRIMITIVE) {
		return PRIMITIVE_CLASS_COUNT;
	}
	if (wave_model == WAVE_MODEL_RELATIVE ||
		wave_model == WAVE_MODEL_RELATIVE_2_STAGE ||
		wave_model == WAVE_MODEL_RELATIVE_SEEDED ||
		wave_model == WAVE_MODEL_RELATIVE_ITERATIVE ||
		wave_model == WAVE_MODEL_LAYERED) {
		return RELATIVE_CLASS_COUNT;
	}
	return WAVE_CLASS_COUNT;
}

static int predictor_direction_index(int dominant_direction)
{
	if (dominant_direction < 0) {
		return 0;
	}
	if (dominant_direction > 0) {
		return 2;
	}
	return 1;
}

static const char *predictor_direction_name(int dir_index)
{
	switch (dir_index) {
	case 0:
		return "bearish";
	case 2:
		return "bullish";
	default:
		return "neutral";
	}
}

static int predictor_abs_bucket(int32_t value, const int32_t *thresholds)
{
	value = predictor_abs_i32(value);
	if (thresholds == NULL) {
		static const int32_t fallback[5] = {1, 2, 5, 10, 20};

		thresholds = fallback;
	}
	if (value <= thresholds[0]) {
		return 0;
	}
	if (value <= thresholds[1]) {
		return 1;
	}
	if (value <= thresholds[2]) {
		return 2;
	}
	if (value <= thresholds[3]) {
		return 3;
	}
	if (value <= thresholds[4]) {
		return 4;
	}
	return 5;
}

static const char *predictor_abs_bucket_name(int bucket)
{
	switch (bucket) {
	case 0:
		return "tiny";
	case 1:
		return "small";
	case 2:
		return "medium";
	case 3:
		return "large";
	case 4:
		return "xlarge";
	default:
		return "impulse";
	}
}

static int predictor_duration_bucket(uint32_t duration)
{
	if (duration <= 1u) {
		return 0;
	}
	if (duration <= 3u) {
		return 1;
	}
	if (duration <= 7u) {
		return 2;
	}
	if (duration <= 15u) {
		return 3;
	}
	return 4;
}

static const char *predictor_duration_bucket_name(int bucket)
{
	switch (bucket) {
	case 0:
		return "1";
	case 1:
		return "2_3";
	case 2:
		return "4_7";
	case 3:
		return "8_15";
	default:
		return "16_plus";
	}
}

static int predictor_ratio_bucket(int32_t value)
{
	if (value < 150) {
		return 0;
	}
	if (value < 300) {
		return 1;
	}
	if (value < 500) {
		return 2;
	}
	if (value < 700) {
		return 3;
	}
	if (value < 900) {
		return 4;
	}
	return 5;
}

static const char *predictor_ratio_bucket_name(int bucket)
{
	switch (bucket) {
	case 0:
		return "very_low";
	case 1:
		return "low";
	case 2:
		return "mid";
	case 3:
		return "high";
	case 4:
		return "very_high";
	default:
		return "extreme";
	}
}

static int predictor_signed_move_bucket(int32_t net_move_raw, const WavePredictorBucketConfig *cfg)
{
	int32_t t0 = 1;
	int32_t t1 = 3;
	int32_t t2 = 8;
	int32_t move_value = net_move_raw;

	if (cfg != NULL) {
		t0 = cfg->signed_move_thresholds[0];
		t1 = cfg->signed_move_thresholds[1];
		t2 = cfg->signed_move_thresholds[2];
		move_value = net_move_raw / predictor_max_i32(cfg->pip_scale, 1);
	}
	if (move_value <= -t2) {
		return 0;
	}
	if (move_value <= -t1) {
		return 1;
	}
	if (move_value <= -t0) {
		return 2;
	}
	if (move_value < t0) {
		return 3;
	}
	if (move_value < t1) {
		return 4;
	}
	if (move_value < t2) {
		return 5;
	}
	return 6;
}

static const char *predictor_move_bucket_name(int bucket)
{
	switch (bucket) {
	case 0:
		return "strong_bearish";
	case 1:
		return "bearish";
	case 2:
		return "light_bearish";
	case 3:
		return "flat";
	case 4:
		return "light_bullish";
	case 5:
		return "bullish";
	default:
		return "strong_bullish";
	}
}

static int predictor_move_bucket_center(int bucket, const WavePredictorBucketConfig *cfg)
{
	static const int32_t fallback[WP_MOVE_BUCKET_COUNT] = {-8, -3, -1, 0, 1, 3, 8};

	if (cfg != NULL) {
		return cfg->signed_move_centers[bucket];
	}
	switch (bucket) {
	case 0:
		return fallback[0];
	case 1:
		return fallback[1];
	case 2:
		return fallback[2];
	case 3:
		return fallback[3];
	case 4:
		return fallback[4];
	case 5:
		return fallback[5];
	default:
		return fallback[6];
	}
}

static int predictor_feature_value(const WaveToken *tok, int feature_id, const WavePredictorBucketConfig *cfg)
{
	int32_t pip_scale = 1;

	if (cfg != NULL) {
		pip_scale = predictor_max_i32(cfg->pip_scale, 1);
	}
	switch (feature_id) {
	case 0:
		return (int)tok->class_id;
	case 1:
		return predictor_direction_index(tok->dominant_direction);
	case 2:
		return predictor_abs_bucket(tok->net_move_raw / pip_scale, (cfg != NULL) ? cfg->abs_thresholds[feature_id] : NULL);
	case 3:
		return predictor_duration_bucket(tok->duration_candles);
	case 4:
		return predictor_ratio_bucket(tok->relative_retracement_ratio);
	case 5:
		return predictor_ratio_bucket(tok->relative_overlap_ratio);
	case 6:
		return predictor_ratio_bucket(tok->relative_directional_efficiency_step);
	case 7:
		return predictor_abs_bucket(tok->relative_upper_wick_sum_raw / pip_scale, (cfg != NULL) ? cfg->abs_thresholds[feature_id] : NULL);
	case 8:
		return predictor_abs_bucket(tok->relative_lower_wick_sum_raw / pip_scale, (cfg != NULL) ? cfg->abs_thresholds[feature_id] : NULL);
	case 9:
		return predictor_ratio_bucket(tok->boundary_confidence);
	default:
		return tok->weak_fit ? 1 : 0;
	}
}

static const char *predictor_feature_name(int feature_id)
{
	switch (feature_id) {
	case 0:
		return "class";
	case 1:
		return "direction";
	case 2:
		return "abs_net_move";
	case 3:
		return "duration";
	case 4:
		return "retracement";
	case 5:
		return "overlap";
	case 6:
		return "dir_eff_step";
	case 7:
		return "upper_wick";
	case 8:
		return "lower_wick";
	case 9:
		return "boundary_conf";
	default:
		return "weak_fit";
	}
}

static const char *predictor_feature_value_name(int32_t wave_model, int feature_id, int value)
{
	switch (feature_id) {
	case 0:
		return predictor_class_name(wave_model, value);
	case 1:
		return predictor_direction_name(value);
	case 2:
	case 7:
	case 8:
		return predictor_abs_bucket_name(value);
	case 3:
		return predictor_duration_bucket_name(value);
	case 4:
	case 5:
	case 6:
	case 9:
		return predictor_ratio_bucket_name(value);
	default:
		return value ? "true" : "false";
	}
}

static int predictor_feature_value_count(int32_t wave_model, int feature_id)
{
	switch (feature_id) {
	case 0:
		return predictor_class_count(wave_model);
	case 1:
		return WP_DIRECTION_COUNT;
	case 2:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
		return 6;
	case 3:
		return 5;
	default:
		return 2;
	}
}

static void predictor_init_result(WavePredictorTargetResult *result, int value_count)
{
	int i;

	memset(result, 0, sizeof(*result));
	result->best_index = 0;
	for (i = 0; i < value_count; ++i) {
		result->scores[i] = 0.0;
		result->probs[i] = 0.0;
	}
}

static void predictor_sort_signals(WavePredictorSignal *signals, uint32_t count)
{
	uint32_t i;
	uint32_t j;

	for (i = 0; i < count; ++i) {
		for (j = i + 1u; j < count; ++j) {
			if (signals[j].contribution > signals[i].contribution) {
				WavePredictorSignal tmp = signals[i];

				signals[i] = signals[j];
				signals[j] = tmp;
			}
		}
	}
}

static void predictor_finalize_target(WavePredictorTargetResult *result,
	const WavePredictorFeatureStat *feature_stats,
	uint32_t feature_count,
	int value_count,
	uint32_t min_class_support,
	double prior_strength,
	uint32_t max_scoring_families)
{
	char families[WP_MAX_ACTIVE_PREDICATES][64];
	double family_best[WP_MAX_ACTIVE_PREDICATES];
	const WavePredictorFeatureStat *family_best_feature[WP_MAX_ACTIVE_PREDICATES];
	double sum_exp = 0.0;
	double max_score = -1e30;
	uint32_t family_count;
	uint32_t i;
	int v;
	int target_index;

	for (v = 0; v < value_count; ++v) {
		double top_family_values[WP_DEFAULT_MAX_SCORING_FAMILIES];
		uint32_t selected_family_count;
		family_count = 0u;
		memset(family_best, 0, sizeof(family_best));
		memset(top_family_values, 0, sizeof(top_family_values));
		if (result->counts[v] < min_class_support) {
			result->scores[v] = -1e30;
			continue;
		}
		double base_prob = ((double)result->counts[v] + 1.0) /
			((double)result->total + (double)value_count);
		double score = log(base_prob);

		for (i = 0; i < feature_count; ++i) {
			double cond_prob;
			double lift;
			double weight;

			if (feature_stats[i].support < 4u) {
				continue;
			}
			cond_prob = predictor_smoothed_cond_prob(feature_stats[i].counts[v],
				feature_stats[i].support,
				base_prob,
				prior_strength);
			lift = cond_prob / base_prob;
			weight = predictor_feature_weight(&feature_stats[i]);
			{
				double contribution = weight * log(lift);
				int family_index = predictor_family_index(families, &family_count, feature_stats[i].family);

				if (family_index >= 0) {
					if (fabs(family_best[family_index]) < fabs(contribution)) {
						family_best[family_index] = contribution;
					}
				}
			}
		}
		selected_family_count = predictor_select_top_family_contributions(family_best,
			family_count,
			top_family_values,
			predictor_max_i32((int32_t)max_scoring_families, 1));
		for (i = 0u; i < selected_family_count; ++i) {
			score += top_family_values[i];
		}
		if (selected_family_count > 0u) {
			score = log(base_prob) + ((score - log(base_prob)) / sqrt((double)selected_family_count));
		}
		result->scores[v] = score;
		if (score > max_score) {
			max_score = score;
			result->best_index = v;
		}
	}
	for (v = 0; v < value_count; ++v) {
		result->probs[v] = exp((result->scores[v] - max_score) / WP_SCORE_TEMPERATURE);
		sum_exp += result->probs[v];
	}
	if (sum_exp <= 0.0) {
		sum_exp = 1.0;
	}
	for (v = 0; v < value_count; ++v) {
		result->probs[v] /= sum_exp;
	}
	target_index = result->best_index;
	result->signal_count = 0u;
	family_count = 0u;
	memset(family_best, 0, sizeof(family_best));
	memset(family_best_feature, 0, sizeof(family_best_feature));
	for (i = 0; i < feature_count; ++i) {
		double base_prob;
		double cond_prob;
		double lift;
		double weight;
		double contribution;

		if (feature_stats[i].support < 4u) {
			continue;
		}
		base_prob = ((double)result->counts[target_index] + 1.0) /
			((double)result->total + (double)value_count);
		cond_prob = predictor_smoothed_cond_prob(feature_stats[i].counts[target_index],
			feature_stats[i].support,
			base_prob,
			prior_strength);
		lift = cond_prob / base_prob;
		weight = predictor_feature_weight(&feature_stats[i]);
		contribution = weight * log(lift);
		if (contribution <= 0.0) {
			continue;
		}
		{
			int family_index = predictor_family_index(families, &family_count, feature_stats[i].family);

			if (family_index < 0) {
				continue;
			}
			if (family_best_feature[family_index] == NULL || family_best[family_index] < contribution) {
				family_best[family_index] = contribution;
				family_best_feature[family_index] = &feature_stats[i];
			}
		}
	}
	for (i = 0u; i < family_count; ++i) {
		const WavePredictorFeatureStat *best_feature = family_best_feature[i];

		if (best_feature == NULL || family_best[i] <= 0.0) {
			continue;
		}
		{
			double base_prob = ((double)result->counts[target_index] + 1.0) /
				((double)result->total + (double)value_count);
			double cond_prob = predictor_smoothed_cond_prob(best_feature->counts[target_index],
				best_feature->support,
				base_prob,
				prior_strength);
			double lift = cond_prob / base_prob;

			if (result->signal_count < WP_MAX_SIGNALS) {
				result->signals[result->signal_count].label = best_feature->label;
				result->signals[result->signal_count].contribution = family_best[i];
				result->signals[result->signal_count].support = best_feature->support;
				result->signals[result->signal_count].lift = lift;
				result->signal_count += 1u;
			} else {
				uint32_t min_index = 0u;
				uint32_t s;

				for (s = 1u; s < result->signal_count; ++s) {
					if (result->signals[s].contribution < result->signals[min_index].contribution) {
						min_index = s;
					}
				}
				if (family_best[i] > result->signals[min_index].contribution) {
					result->signals[min_index].label = best_feature->label;
					result->signals[min_index].contribution = family_best[i];
					result->signals[min_index].support = best_feature->support;
					result->signals[min_index].lift = lift;
				}
			}
		}
	}
	predictor_sort_signals(result->signals, result->signal_count);
}

static int predictor_encode_signals(char *out,
	size_t out_cap,
	size_t *used,
	const WavePredictorTargetResult *result)
{
	uint32_t i;

	if (append(out, out_cap, used, "[") != 0) {
		return -1;
	}
	for (i = 0; i < result->signal_count; ++i) {
		if (i > 0u && append(out, out_cap, used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			used,
			"{\"feature\":\"%s\",\"support\":%u,\"lift\":%.4f,\"contribution\":%.4f}",
			result->signals[i].label,
			result->signals[i].support,
			result->signals[i].lift,
			result->signals[i].contribution) != 0) {
			return -1;
		}
	}
	return append(out, out_cap, used, "]");
}

int wave_predictor_encode_json(uint32_t dataset_id,
	uint32_t anchor_token,
	uint32_t input_len,
	uint32_t horizon,
	int with_eval,
	char *out,
	size_t out_cap)
{
	DatasetRecord *rec;
	WavePredictorBucketConfig bucket_cfg;
	WavePredictorPredicate predicates[WP_MAX_ACTIVE_PREDICATES];
	uint32_t max_horizon;
	uint32_t input_start;
	WavePredictorFeatureStat class_features[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat direction_features[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat move_features[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat class_features_shuffle[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat direction_features_shuffle[WP_MAX_ACTIVE_PREDICATES];
	WavePredictorFeatureStat move_features_shuffle[WP_MAX_ACTIVE_PREDICATES];
	uint32_t valid_future_indices[16384];
	size_t used = 0;
	uint32_t rel_pos;
	uint32_t predicate_count = 0u;
	uint32_t h;
	int class_count;
	int i;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL || rec->waves.tokens == NULL || rec->waves.token_count == 0u) {
		return -1;
	}
	if (anchor_token >= rec->waves.token_count) {
		return -1;
	}
	predictor_build_bucket_config(rec, &bucket_cfg);
	if (input_len == 0u) {
		input_len = 3u;
	}
	if (input_len > WP_MAX_INPUT_LEN || input_len > anchor_token + 1u) {
		input_len = (anchor_token + 1u < WP_MAX_INPUT_LEN) ? (anchor_token + 1u) : WP_MAX_INPUT_LEN;
	}
	if (horizon == 0u) {
		horizon = 4u;
	}
	if (horizon > WP_MAX_HORIZON) {
		horizon = WP_MAX_HORIZON;
	}
	if (anchor_token + 1u >= rec->waves.token_count) {
		max_horizon = 0u;
	} else {
		max_horizon = rec->waves.token_count - anchor_token - 1u;
		if (max_horizon > horizon) {
			max_horizon = horizon;
		}
	}
	input_start = anchor_token + 1u - input_len;
	class_count = predictor_class_count(rec->waves.params.wave_model);
	memset(class_features, 0, sizeof(class_features));
	memset(direction_features, 0, sizeof(direction_features));
	memset(move_features, 0, sizeof(move_features));
	memset(class_features_shuffle, 0, sizeof(class_features_shuffle));
	memset(direction_features_shuffle, 0, sizeof(direction_features_shuffle));
	memset(move_features_shuffle, 0, sizeof(move_features_shuffle));
	if (predictor_build_window_predicates(rec, input_start, input_len, &bucket_cfg, predicates, &predicate_count) != 0) {
		return -1;
	}

	if (append(out,
		out_cap,
		&used,
		"{\"dataset_id\":%u,\"anchor_token\":%u,\"input_len\":%u,\"horizon\":%u,\"wave_model\":\"%s\",\"input_window\":[",
		dataset_id,
		anchor_token,
		input_len,
		max_horizon,
		(rec->waves.params.wave_model == WAVE_MODEL_LAYERED) ? "layered" :
		(rec->waves.params.wave_model == WAVE_MODEL_PRIMITIVE) ? "primitive" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE) ? "relative" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE_2_STAGE) ? "relative_2_stage" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE_SEEDED) ? "relative_seeded" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) ? "relative_iterative" :
		"legacy") != 0) {
		return -1;
	}
	for (rel_pos = 0u; rel_pos < input_len; ++rel_pos) {
		const WaveToken *tok = &rec->waves.tokens[input_start + rel_pos];

		if (rel_pos > 0u && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"window_offset\":%d,\"token_id\":%u,\"start_candle\":%u,\"end_candle\":%u,\"class_name\":\"%s\",\"direction\":\"%s\",\"net_move_raw\":%d,\"duration_candles\":%u}",
			(int)rel_pos - (int)input_len + 1,
			input_start + rel_pos,
			tok->start_candle,
			tok->end_candle,
			predictor_class_name(rec->waves.params.wave_model, (int)tok->class_id),
			predictor_direction_name(predictor_direction_index(tok->dominant_direction)),
			tok->net_move_raw,
			tok->duration_candles) != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "],\"horizons\":[") != 0) {
		return -1;
	}
	for (h = 1u; h <= max_horizon; ++h) {
		WavePredictorTargetResult class_result;
		WavePredictorTargetResult direction_result;
		WavePredictorTargetResult move_result;
		WavePredictorTargetResult class_shuffle_result;
		WavePredictorTargetResult direction_shuffle_result;
		WavePredictorTargetResult move_shuffle_result;
		const WaveToken *actual_future = &rec->waves.tokens[anchor_token + h];
		double expected_move = 0.0;
		double baseline_expected_move = 0.0;
		double class_base_probs[WP_MAX_CLASS_COUNT];
		double direction_base_probs[WP_MAX_CLASS_COUNT];
		double move_base_probs[WP_MAX_CLASS_COUNT];
		double class_shuffle_base_probs[WP_MAX_CLASS_COUNT];
		double direction_shuffle_base_probs[WP_MAX_CLASS_COUNT];
		double move_shuffle_base_probs[WP_MAX_CLASS_COUNT];
		double class_kl_bits;
		double direction_kl_bits;
		double move_kl_bits;
		double class_shuffle_kl_bits;
		double direction_shuffle_kl_bits;
		double move_shuffle_kl_bits;
		double predicted_class_base_prob;
		double predicted_direction_base_prob;
		double predicted_move_base_prob;
		double predicted_class_lift;
		double predicted_direction_lift;
		double predicted_move_lift;
		double shuffled_class_lift;
		double shuffled_direction_lift;
		double shuffled_move_lift;
		uint32_t sample_end;
		uint32_t feat_index = 0u;
		uint32_t sample_count = 0u;
		uint32_t shift;
		uint32_t sample_index;

		predictor_init_result(&class_result, class_count);
		predictor_init_result(&direction_result, WP_DIRECTION_COUNT);
		predictor_init_result(&move_result, WP_MOVE_BUCKET_COUNT);
		predictor_init_result(&class_shuffle_result, class_count);
		predictor_init_result(&direction_shuffle_result, WP_DIRECTION_COUNT);
		predictor_init_result(&move_shuffle_result, WP_MOVE_BUCKET_COUNT);
		memset(class_features, 0, sizeof(class_features));
		memset(direction_features, 0, sizeof(direction_features));
		memset(move_features, 0, sizeof(move_features));
		memset(class_features_shuffle, 0, sizeof(class_features_shuffle));
		memset(direction_features_shuffle, 0, sizeof(direction_features_shuffle));
		memset(move_features_shuffle, 0, sizeof(move_features_shuffle));
		for (feat_index = 0u; feat_index < predicate_count; ++feat_index) {
			strcpy(class_features[feat_index].label, predicates[feat_index].label);
			strcpy(direction_features[feat_index].label, predicates[feat_index].label);
			strcpy(move_features[feat_index].label, predicates[feat_index].label);
			strcpy(class_features_shuffle[feat_index].label, predicates[feat_index].label);
			strcpy(direction_features_shuffle[feat_index].label, predicates[feat_index].label);
			strcpy(move_features_shuffle[feat_index].label, predicates[feat_index].label);
			strcpy(class_features[feat_index].family, predicates[feat_index].family);
			strcpy(direction_features[feat_index].family, predicates[feat_index].family);
			strcpy(move_features[feat_index].family, predicates[feat_index].family);
			strcpy(class_features_shuffle[feat_index].family, predicates[feat_index].family);
			strcpy(direction_features_shuffle[feat_index].family, predicates[feat_index].family);
			strcpy(move_features_shuffle[feat_index].family, predicates[feat_index].family);
		}
		for (sample_end = input_len - 1u; sample_end + h < rec->waves.token_count; ++sample_end) {
			if (sample_count < (sizeof(valid_future_indices) / sizeof(valid_future_indices[0]))) {
				valid_future_indices[sample_count] = sample_end + h;
			}
			sample_count += 1u;
		}
		if (sample_count == 0u) {
			continue;
		}
		shift = sample_count / 3u + h + 1u;
		if (shift >= sample_count) {
			shift %= sample_count;
		}
		if (shift == 0u && sample_count > 1u) {
			shift = 1u;
		}
		sample_index = 0u;
		for (sample_end = input_len - 1u; sample_end + h < rec->waves.token_count; ++sample_end) {
			const WaveToken *future_tok;
			const WaveToken *shuffled_future_tok;
			int class_index;
			int dir_index;
			int move_index;
			int shuffled_class_index;
			int shuffled_dir_index;
			int shuffled_move_index;

			future_tok = &rec->waves.tokens[sample_end + h];
			shuffled_future_tok = &rec->waves.tokens[valid_future_indices[(sample_index + shift) % sample_count]];
			class_index = (int)future_tok->class_id;
			dir_index = predictor_direction_index(future_tok->dominant_direction);
			move_index = predictor_signed_move_bucket(future_tok->net_move_raw, &bucket_cfg);
			shuffled_class_index = (int)shuffled_future_tok->class_id;
			shuffled_dir_index = predictor_direction_index(shuffled_future_tok->dominant_direction);
			shuffled_move_index = predictor_signed_move_bucket(shuffled_future_tok->net_move_raw, &bucket_cfg);
			if (class_index >= 0 && class_index < class_count) {
				class_result.counts[class_index] += 1u;
			}
			class_result.total += 1u;
			direction_result.counts[dir_index] += 1u;
			direction_result.total += 1u;
			move_result.counts[move_index] += 1u;
			move_result.total += 1u;
			if (shuffled_class_index >= 0 && shuffled_class_index < class_count) {
				class_shuffle_result.counts[shuffled_class_index] += 1u;
			}
			class_shuffle_result.total += 1u;
			direction_shuffle_result.counts[shuffled_dir_index] += 1u;
			direction_shuffle_result.total += 1u;
			move_shuffle_result.counts[shuffled_move_index] += 1u;
			move_shuffle_result.total += 1u;
			for (feat_index = 0u; feat_index < predicate_count; ++feat_index) {
				if (predictor_predicate_matches_window(rec,
					sample_end + 1u - input_len,
					input_len,
					&bucket_cfg,
					&predicates[feat_index])) {
					class_features[feat_index].support += 1u;
					direction_features[feat_index].support += 1u;
					move_features[feat_index].support += 1u;
					class_features_shuffle[feat_index].support += 1u;
					direction_features_shuffle[feat_index].support += 1u;
					move_features_shuffle[feat_index].support += 1u;
					if (class_index >= 0 && class_index < class_count) {
						class_features[feat_index].counts[class_index] += 1u;
					}
					direction_features[feat_index].counts[dir_index] += 1u;
					move_features[feat_index].counts[move_index] += 1u;
					if (shuffled_class_index >= 0 && shuffled_class_index < class_count) {
						class_features_shuffle[feat_index].counts[shuffled_class_index] += 1u;
					}
					direction_features_shuffle[feat_index].counts[shuffled_dir_index] += 1u;
					move_features_shuffle[feat_index].counts[shuffled_move_index] += 1u;
				}
			}
			sample_index += 1u;
		}
		if (class_result.total == 0u) {
			continue;
		}
		predictor_finalize_target(&class_result,
			class_features,
			predicate_count,
			class_count,
			1u,
			WP_CLASS_CONDITIONAL_PRIOR_STRENGTH,
			WP_CLASS_MAX_SCORING_FAMILIES);
		predictor_finalize_target(&direction_result,
			direction_features,
			predicate_count,
			WP_DIRECTION_COUNT,
			8u,
			WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH,
			WP_DEFAULT_MAX_SCORING_FAMILIES);
		predictor_finalize_target(&move_result,
			move_features,
			predicate_count,
			WP_MOVE_BUCKET_COUNT,
			1u,
			WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH,
			WP_DEFAULT_MAX_SCORING_FAMILIES);
		predictor_finalize_target(&class_shuffle_result,
			class_features_shuffle,
			predicate_count,
			class_count,
			1u,
			WP_CLASS_CONDITIONAL_PRIOR_STRENGTH,
			WP_CLASS_MAX_SCORING_FAMILIES);
		predictor_finalize_target(&direction_shuffle_result,
			direction_features_shuffle,
			predicate_count,
			WP_DIRECTION_COUNT,
			8u,
			WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH,
			WP_DEFAULT_MAX_SCORING_FAMILIES);
		predictor_finalize_target(&move_shuffle_result,
			move_features_shuffle,
			predicate_count,
			WP_MOVE_BUCKET_COUNT,
			1u,
			WP_DEFAULT_CONDITIONAL_PRIOR_STRENGTH,
			WP_DEFAULT_MAX_SCORING_FAMILIES);
		predictor_fill_base_probs(&class_result, class_count, class_base_probs);
		predictor_fill_base_probs(&direction_result, WP_DIRECTION_COUNT, direction_base_probs);
		predictor_fill_base_probs(&move_result, WP_MOVE_BUCKET_COUNT, move_base_probs);
		predictor_fill_base_probs(&class_shuffle_result, class_count, class_shuffle_base_probs);
		predictor_fill_base_probs(&direction_shuffle_result, WP_DIRECTION_COUNT, direction_shuffle_base_probs);
		predictor_fill_base_probs(&move_shuffle_result, WP_MOVE_BUCKET_COUNT, move_shuffle_base_probs);
		class_kl_bits = predictor_distribution_kl_bits(class_result.probs, class_base_probs, class_count);
		direction_kl_bits = predictor_distribution_kl_bits(direction_result.probs, direction_base_probs, WP_DIRECTION_COUNT);
		move_kl_bits = predictor_distribution_kl_bits(move_result.probs, move_base_probs, WP_MOVE_BUCKET_COUNT);
		class_shuffle_kl_bits = predictor_distribution_kl_bits(class_shuffle_result.probs, class_shuffle_base_probs, class_count);
		direction_shuffle_kl_bits = predictor_distribution_kl_bits(direction_shuffle_result.probs, direction_shuffle_base_probs, WP_DIRECTION_COUNT);
		move_shuffle_kl_bits = predictor_distribution_kl_bits(move_shuffle_result.probs, move_shuffle_base_probs, WP_MOVE_BUCKET_COUNT);
		predicted_class_base_prob = class_base_probs[class_result.best_index];
		predicted_direction_base_prob = direction_base_probs[direction_result.best_index];
		predicted_move_base_prob = move_base_probs[move_result.best_index];
		predicted_class_lift = predictor_safe_lift(class_result.probs[class_result.best_index], predicted_class_base_prob);
		predicted_direction_lift = predictor_safe_lift(direction_result.probs[direction_result.best_index], predicted_direction_base_prob);
		predicted_move_lift = predictor_safe_lift(move_result.probs[move_result.best_index], predicted_move_base_prob);
		shuffled_class_lift = predictor_safe_lift(class_shuffle_result.probs[class_shuffle_result.best_index],
			class_base_probs[class_shuffle_result.best_index]);
		shuffled_direction_lift = predictor_safe_lift(direction_shuffle_result.probs[direction_shuffle_result.best_index],
			direction_base_probs[direction_shuffle_result.best_index]);
		shuffled_move_lift = predictor_safe_lift(move_shuffle_result.probs[move_shuffle_result.best_index],
			move_base_probs[move_shuffle_result.best_index]);
		for (i = 0; i < WP_MOVE_BUCKET_COUNT; ++i) {
			expected_move += move_result.probs[i] * (double)predictor_move_bucket_center(i, &bucket_cfg);
			baseline_expected_move += move_base_probs[i] * (double)predictor_move_bucket_center(i, &bucket_cfg);
		}
		if (h > 1u && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"horizon\":%u,\"support\":%u,\"actual_future\":{\"token_id\":%u,\"start_candle\":%u,\"end_candle\":%u,\"class_name\":\"%s\",\"direction\":\"%s\",\"net_move_raw\":%d,\"duration_candles\":%u},\"predicted_class\":\"%s\",\"predicted_class_probability\":%.4f,\"predicted_direction\":\"%s\",\"predicted_direction_probability\":%.4f,\"predicted_move_bucket\":\"%s\",\"predicted_move_probability\":%.4f,\"expected_net_move_raw\":%.2f,\"class_probabilities\":[",
			h,
			class_result.total,
			anchor_token + h,
			actual_future->start_candle,
			actual_future->end_candle,
			predictor_class_name(rec->waves.params.wave_model, (int)actual_future->class_id),
			predictor_direction_name(predictor_direction_index(actual_future->dominant_direction)),
			actual_future->net_move_raw,
			actual_future->duration_candles,
			predictor_class_name(rec->waves.params.wave_model, class_result.best_index),
			class_result.probs[class_result.best_index],
			predictor_direction_name(direction_result.best_index),
			direction_result.probs[direction_result.best_index],
			predictor_move_bucket_name(move_result.best_index),
			move_result.probs[move_result.best_index],
			expected_move) != 0) {
			return -1;
		}
		for (i = 0; i < class_count; ++i) {
			if (i > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"label\":\"%s\",\"probability\":%.4f}",
				predictor_class_name(rec->waves.params.wave_model, i),
				class_result.probs[i]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, "],\"direction_probabilities\":[") != 0) {
			return -1;
		}
		for (i = 0; i < WP_DIRECTION_COUNT; ++i) {
			if (i > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"label\":\"%s\",\"probability\":%.4f}",
				predictor_direction_name(i),
				direction_result.probs[i]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, "],\"move_probabilities\":[") != 0) {
			return -1;
		}
		for (i = 0; i < WP_MOVE_BUCKET_COUNT; ++i) {
			if (i > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"label\":\"%s\",\"probability\":%.4f}",
				predictor_move_bucket_name(i),
				move_result.probs[i]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, "],\"top_class_signals\":") != 0) {
			return -1;
		}
		if (predictor_encode_signals(out, out_cap, &used, &class_result) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, ",\"top_direction_signals\":") != 0) {
			return -1;
		}
		if (predictor_encode_signals(out, out_cap, &used, &direction_result) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, ",\"top_move_signals\":") != 0) {
			return -1;
		}
		if (predictor_encode_signals(out, out_cap, &used, &move_result) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, ",\"trace\":{") != 0) {
			return -1;
		}
		if (predictor_encode_target_trace_json(out,
			out_cap,
			&used,
			"class",
			0,
			rec->waves.params.wave_model,
			&class_result,
			class_features,
			predicate_count,
			class_count) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (predictor_encode_target_trace_json(out,
			out_cap,
			&used,
			"direction",
			1,
			rec->waves.params.wave_model,
			&direction_result,
			direction_features,
			predicate_count,
			WP_DIRECTION_COUNT) != 0) {
			return -1;
		}
		if (append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (predictor_encode_target_trace_json(out,
			out_cap,
			&used,
			"move",
			2,
			rec->waves.params.wave_model,
			&move_result,
			move_features,
			predicate_count,
			WP_MOVE_BUCKET_COUNT) != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"},\"evaluation\":{\"predicted_class_base_probability\":%.4f,\"predicted_class_lift\":%.4f,\"predicted_direction_base_probability\":%.4f,\"predicted_direction_lift\":%.4f,\"predicted_move_base_probability\":%.4f,\"predicted_move_lift\":%.4f,\"class_kl_bits\":%.6f,\"direction_kl_bits\":%.6f,\"move_kl_bits\":%.6f,\"baseline_expected_net_move_raw\":%.2f,\"expected_move_edge_raw\":%.2f,\"shuffled_class\":\"%s\",\"shuffled_class_probability\":%.4f,\"shuffled_class_lift\":%.4f,\"shuffled_direction\":\"%s\",\"shuffled_direction_probability\":%.4f,\"shuffled_direction_lift\":%.4f,\"shuffled_move_bucket\":\"%s\",\"shuffled_move_probability\":%.4f,\"shuffled_move_lift\":%.4f,\"shuffled_class_kl_bits\":%.6f,\"shuffled_direction_kl_bits\":%.6f,\"shuffled_move_kl_bits\":%.6f,\"base_class_probabilities\":[",
			predicted_class_base_prob,
			predicted_class_lift,
			predicted_direction_base_prob,
			predicted_direction_lift,
			predicted_move_base_prob,
			predicted_move_lift,
			class_kl_bits,
			direction_kl_bits,
			move_kl_bits,
			baseline_expected_move,
			expected_move - baseline_expected_move,
			predictor_class_name(rec->waves.params.wave_model, class_shuffle_result.best_index),
			class_shuffle_result.probs[class_shuffle_result.best_index],
			shuffled_class_lift,
			predictor_direction_name(direction_shuffle_result.best_index),
			direction_shuffle_result.probs[direction_shuffle_result.best_index],
			shuffled_direction_lift,
			predictor_move_bucket_name(move_shuffle_result.best_index),
			move_shuffle_result.probs[move_shuffle_result.best_index],
			shuffled_move_lift,
			class_shuffle_kl_bits,
			direction_shuffle_kl_bits,
			move_shuffle_kl_bits) != 0) {
			return -1;
		}
		for (i = 0; i < class_count; ++i) {
			if (i > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"label\":\"%s\",\"probability\":%.4f}",
				predictor_class_name(rec->waves.params.wave_model, i),
				class_base_probs[i]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, "],\"base_direction_probabilities\":[") != 0) {
			return -1;
		}
		for (i = 0; i < WP_DIRECTION_COUNT; ++i) {
			if (i > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"label\":\"%s\",\"probability\":%.4f}",
				predictor_direction_name(i),
				direction_base_probs[i]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, "],\"base_move_probabilities\":[") != 0) {
			return -1;
		}
		for (i = 0; i < WP_MOVE_BUCKET_COUNT; ++i) {
			if (i > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (append(out,
				out_cap,
				&used,
				"{\"label\":\"%s\",\"probability\":%.4f}",
				predictor_move_bucket_name(i),
				move_base_probs[i]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, "]}}") != 0) {
			return -1;
		}
		memset(class_features, 0, sizeof(class_features));
		memset(direction_features, 0, sizeof(direction_features));
		memset(move_features, 0, sizeof(move_features));
	}
	if (with_eval) {
		if (predictor_encode_evaluation_summary_json(rec,
			input_len,
			max_horizon,
			out,
			out_cap,
			&used) != 0) {
			return -1;
		}
	} else if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}

int wave_predictor_encode_base_probs_json(uint32_t dataset_id,
	char *out,
	size_t out_cap)
{
	DatasetRecord *rec;
	WavePredictorBucketConfig bucket_cfg;
	size_t used = 0;
	int feature_id;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL || rec->waves.tokens == NULL || rec->waves.token_count == 0u) {
		return -1;
	}
	predictor_build_bucket_config(rec, &bucket_cfg);
	if (append(out,
		out_cap,
		&used,
		"{\"dataset_id\":%u,\"wave_model\":\"%s\",\"token_count\":%u,\"features\":[",
		dataset_id,
		(rec->waves.params.wave_model == WAVE_MODEL_LAYERED) ? "layered" :
		(rec->waves.params.wave_model == WAVE_MODEL_PRIMITIVE) ? "primitive" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE) ? "relative" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE_2_STAGE) ? "relative_2_stage" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE_SEEDED) ? "relative_seeded" :
		(rec->waves.params.wave_model == WAVE_MODEL_RELATIVE_ITERATIVE) ? "relative_iterative" :
		"legacy",
		rec->waves.token_count) != 0) {
		return -1;
	}
	for (feature_id = 0; feature_id < WP_FEATURE_COUNT; ++feature_id) {
		uint32_t counts[WP_MAX_CLASS_COUNT];
		uint32_t total = 0u;
		int value_count;
		uint32_t token_index;
		int value;

		memset(counts, 0, sizeof(counts));
		value_count = predictor_feature_value_count(rec->waves.params.wave_model, feature_id);
		for (token_index = 0u; token_index < rec->waves.token_count; ++token_index) {
			int feature_value = predictor_feature_value(&rec->waves.tokens[token_index], feature_id, &bucket_cfg);

			if (feature_value < 0 || feature_value >= value_count || feature_value >= WP_MAX_CLASS_COUNT) {
				continue;
			}
			counts[feature_value] += 1u;
			total += 1u;
		}
		if (feature_id > 0 && append(out, out_cap, &used, ",") != 0) {
			return -1;
		}
		if (append(out,
			out_cap,
			&used,
			"{\"feature_id\":%d,\"name\":\"%s\",\"value_count\":%d",
			feature_id,
			predictor_feature_name(feature_id),
			value_count) != 0) {
			return -1;
		}
		if (predictor_feature_uses_abs_buckets(feature_id)) {
			if (append(out,
				out_cap,
				&used,
				",\"thresholds_normalized\":[%d,%d,%d,%d,%d]",
				bucket_cfg.abs_thresholds[feature_id][0],
				bucket_cfg.abs_thresholds[feature_id][1],
				bucket_cfg.abs_thresholds[feature_id][2],
				bucket_cfg.abs_thresholds[feature_id][3],
				bucket_cfg.abs_thresholds[feature_id][4]) != 0) {
				return -1;
			}
		}
		if (feature_id == 2) {
			if (append(out,
				out_cap,
				&used,
				",\"signed_move_thresholds_normalized\":[%d,%d,%d]",
				bucket_cfg.signed_move_thresholds[0],
				bucket_cfg.signed_move_thresholds[1],
				bucket_cfg.signed_move_thresholds[2]) != 0) {
				return -1;
			}
		}
		if (append(out, out_cap, &used, ",\"values\":[") != 0) {
			return -1;
		}
		for (value = 0; value < value_count; ++value) {
			double probability = 0.0;
			int32_t range_from = 0;
			int32_t range_to = 0;
			int has_upper_bound = 1;

			if (total > 0u) {
				probability = (double)counts[value] / (double)total;
			}
			if (predictor_feature_uses_abs_buckets(feature_id)) {
				switch (value) {
				case 0:
					range_from = 0;
					range_to = bucket_cfg.abs_thresholds[feature_id][0];
					break;
				case 1:
					range_from = bucket_cfg.abs_thresholds[feature_id][0] + 1;
					range_to = bucket_cfg.abs_thresholds[feature_id][1];
					break;
				case 2:
					range_from = bucket_cfg.abs_thresholds[feature_id][1] + 1;
					range_to = bucket_cfg.abs_thresholds[feature_id][2];
					break;
				case 3:
					range_from = bucket_cfg.abs_thresholds[feature_id][2] + 1;
					range_to = bucket_cfg.abs_thresholds[feature_id][3];
					break;
				case 4:
					range_from = bucket_cfg.abs_thresholds[feature_id][3] + 1;
					range_to = bucket_cfg.abs_thresholds[feature_id][4];
					break;
				default:
					range_from = bucket_cfg.abs_thresholds[feature_id][4] + 1;
					has_upper_bound = 0;
					break;
				}
			} else if (predictor_feature_uses_duration_buckets(feature_id)) {
				switch (value) {
				case 0:
					range_from = 1;
					range_to = 1;
					break;
				case 1:
					range_from = 2;
					range_to = 3;
					break;
				case 2:
					range_from = 4;
					range_to = 7;
					break;
				case 3:
					range_from = 8;
					range_to = 15;
					break;
				default:
					range_from = 16;
					has_upper_bound = 0;
					break;
				}
			} else if (predictor_feature_uses_ratio_buckets(feature_id)) {
				switch (value) {
				case 0:
					range_from = 0;
					range_to = 149;
					break;
				case 1:
					range_from = 150;
					range_to = 299;
					break;
				case 2:
					range_from = 300;
					range_to = 499;
					break;
				case 3:
					range_from = 500;
					range_to = 699;
					break;
				case 4:
					range_from = 700;
					range_to = 899;
					break;
				default:
					range_from = 900;
					has_upper_bound = 0;
					break;
				}
			}
			if (value > 0 && append(out, out_cap, &used, ",") != 0) {
				return -1;
			}
			if (predictor_feature_uses_abs_buckets(feature_id)) {
				if (has_upper_bound) {
					if (append(out,
						out_cap,
						&used,
						"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f,\"pips_from\":%d,\"pips_to\":%d}",
						value,
						predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
						counts[value],
						probability,
						range_from,
						range_to) != 0) {
						return -1;
					}
				} else {
					if (append(out,
						out_cap,
						&used,
						"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f,\"pips_from\":%d,\"pips_to\":null}",
						value,
						predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
						counts[value],
						probability,
						range_from) != 0) {
						return -1;
					}
				}
			} else if (predictor_feature_uses_duration_buckets(feature_id)) {
				if (has_upper_bound) {
					if (append(out,
						out_cap,
						&used,
						"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f,\"candles_from\":%d,\"candles_to\":%d}",
						value,
						predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
						counts[value],
						probability,
						range_from,
						range_to) != 0) {
						return -1;
					}
				} else {
					if (append(out,
						out_cap,
						&used,
						"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f,\"candles_from\":%d,\"candles_to\":null}",
						value,
						predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
						counts[value],
						probability,
						range_from) != 0) {
						return -1;
					}
				}
			} else if (predictor_feature_uses_ratio_buckets(feature_id)) {
				if (has_upper_bound) {
					if (append(out,
						out_cap,
						&used,
						"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f,\"ratio_from\":%d,\"ratio_to\":%d}",
						value,
						predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
						counts[value],
						probability,
						range_from,
						range_to) != 0) {
						return -1;
					}
				} else {
					if (append(out,
						out_cap,
						&used,
						"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f,\"ratio_from\":%d,\"ratio_to\":null}",
						value,
						predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
						counts[value],
						probability,
						range_from) != 0) {
						return -1;
					}
				}
			} else {
				if (append(out,
					out_cap,
					&used,
					"{\"value\":%d,\"label\":\"%s\",\"count\":%u,\"probability\":%.8f}",
					value,
					predictor_feature_value_name(rec->waves.params.wave_model, feature_id, value),
					counts[value],
					probability) != 0) {
					return -1;
				}
			}
		}
		if (append(out, out_cap, &used, "]}") != 0) {
			return -1;
		}
	}
	if (append(out, out_cap, &used, "]}") != 0) {
		return -1;
	}
	return (int)used;
}
