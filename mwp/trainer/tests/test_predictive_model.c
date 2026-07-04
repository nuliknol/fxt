#include "core_api.h"
#include "internal_store.h"
#include "wave_predictor.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_JSON_CAP (1024u * 1024u)

static void test_log(const char *msg)
{
	printf("[predictive_model_tests] %s\n", msg);
}

static void expect_contains(const char *json, const char *needle, const char *context)
{
	printf("[predictive_model_tests] assert contains: %s -> %s\n", context, needle);
	assert(strstr(json, needle) != NULL);
}

static void expect_not_contains(const char *json, const char *needle, const char *context)
{
	printf("[predictive_model_tests] assert not contains: %s -> %s\n", context, needle);
	assert(strstr(json, needle) == NULL);
}

static double extract_scalar_after_key(const char *json, const char *key)
{
	const char *cursor;

	cursor = strstr(json, key);
	assert(cursor != NULL);
	cursor += strlen(key);
	return strtod(cursor, NULL);
}

static void extract_string_after_key(const char *json, const char *key, char *out, size_t out_cap)
{
	const char *cursor;
	const char *end;
	size_t len;

	assert(out != NULL);
	assert(out_cap > 0u);
	cursor = strstr(json, key);
	assert(cursor != NULL);
	cursor += strlen(key);
	end = strchr(cursor, '"');
	assert(end != NULL);
	len = (size_t)(end - cursor);
	if (len >= out_cap) {
		len = out_cap - 1u;
	}
	memcpy(out, cursor, len);
	out[len] = '\0';
}

static double extract_named_probability(const char *json, const char *array_key, const char *label)
{
	const char *start;
	const char *end;
	const char *cursor;
	char needle[128];

	snprintf(needle, sizeof(needle), "\"label\":\"%s\"", label);
	start = strstr(json, array_key);
	assert(start != NULL);
	cursor = strchr(start, '[');
	assert(cursor != NULL);
	end = strchr(cursor, ']');
	assert(end != NULL);
	cursor = strstr(cursor, needle);
	assert(cursor != NULL && cursor < end);
	cursor = strstr(cursor, "\"probability\":");
	assert(cursor != NULL && cursor < end);
	cursor += strlen("\"probability\":");
	return strtod(cursor, NULL);
}

static double extract_trace_named_probability(const char *trace_section, const char *label)
{
	const char *cursor;
	const char *end;
	char needle[128];

	assert(trace_section != NULL);
	snprintf(needle, sizeof(needle), "\"label\":\"%s\"", label);
	cursor = strstr(trace_section, "\"score_values\":[");
	assert(cursor != NULL);
	end = strstr(cursor, "],\"families\":[");
	assert(end != NULL);
	cursor = strstr(cursor, needle);
	assert(cursor != NULL && cursor < end);
	cursor = strstr(cursor, "\"probability\":");
	assert(cursor != NULL && cursor < end);
	cursor += strlen("\"probability\":");
	return strtod(cursor, NULL);
}

static double extract_array_probability_sum(const char *json, const char *array_key)
{
	const char *start;
	const char *cursor;
	const char *end;
	double sum = 0.0;

	start = strstr(json, array_key);
	assert(start != NULL);
	cursor = strchr(start, '[');
	assert(cursor != NULL);
	end = strchr(cursor, ']');
	assert(end != NULL);
	while (cursor < end) {
		const char *prob_key = strstr(cursor, "\"probability\":");
		char *parse_end;

		if (prob_key == NULL || prob_key >= end) {
			break;
		}
		prob_key += strlen("\"probability\":");
		sum += strtod(prob_key, &parse_end);
		assert(parse_end != prob_key);
		cursor = parse_end;
	}
	return sum;
}

static void expect_probability_sum_close(const char *json, const char *array_key, const char *context)
{
	double sum = extract_array_probability_sum(json, array_key);

	printf("[predictive_model_tests] assert probability sum: %s -> %.6f\n", context, sum);
	assert(sum > 0.9990 && sum < 1.0010);
}

static WaveToken make_token(uint32_t start_candle,
	uint32_t duration_candles,
	uint32_t class_id,
	int32_t dominant_direction,
	int32_t net_move_raw,
	int32_t retracement_ratio,
	int32_t overlap_ratio,
	int32_t der_step,
	int32_t upper_wick_raw,
	int32_t lower_wick_raw,
	int32_t boundary_confidence,
	int32_t weak_fit)
{
	WaveToken tok;

	memset(&tok, 0, sizeof(tok));
	tok.start_candle = start_candle;
	tok.end_candle = start_candle + duration_candles - 1u;
	tok.duration_candles = duration_candles;
	tok.class_id = class_id;
	tok.dominant_direction = dominant_direction;
	tok.net_move_raw = net_move_raw;
	tok.relative_retracement_ratio = retracement_ratio;
	tok.relative_overlap_ratio = overlap_ratio;
	tok.relative_directional_efficiency_step = der_step;
	tok.relative_upper_wick_sum_raw = upper_wick_raw;
	tok.relative_lower_wick_sum_raw = lower_wick_raw;
	tok.boundary_confidence = boundary_confidence;
	tok.weak_fit = weak_fit;
	return tok;
}

static DatasetRecord *create_predictor_dataset(const WaveToken *tokens,
	uint32_t token_count,
	int32_t pip_scale,
	uint32_t *out_dataset_id)
{
	DatasetRecord *rec;
	uint32_t dataset_id;
	uint32_t i;

	rec = store_alloc_dataset(&dataset_id);
	assert(rec != NULL);
	if (out_dataset_id != NULL) {
		*out_dataset_id = dataset_id;
	}
	rec->meta.dataset_id = dataset_id;
	rec->meta.pip_scale = (uint32_t)pip_scale;
	rec->meta.candle_count = (token_count > 0u) ? (tokens[token_count - 1u].end_candle + 1u) : 0u;
	rec->meta.feature_count = rec->meta.candle_count;
	rec->waves.params.wave_model = WAVE_MODEL_LAYERED;
	rec->waves.token_count = token_count;
	rec->waves.tokens = (WaveToken *)malloc((size_t)token_count * sizeof(*rec->waves.tokens));
	assert(rec->waves.tokens != NULL);
	memcpy(rec->waves.tokens, tokens, (size_t)token_count * sizeof(*tokens));
	memset(rec->waves.class_counts, 0, sizeof(rec->waves.class_counts));
	for (i = 0u; i < token_count; ++i) {
		if (tokens[i].class_id < WAVE_CLASS_COUNT) {
			rec->waves.class_counts[tokens[i].class_id] += 1u;
		}
	}
	return rec;
}

static void test_base_probabilities_known_counts(void)
{
	WaveToken tokens[8];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	int rc;

	test_log("test_base_probabilities_known_counts");
	rc = core_init("trainer_test_predictor_base_probs");
	assert(rc == 0);
	for (i = 0u; i < 8u; ++i) {
		if (i < 6u) {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 500, 120, 80, 850, 20, 20, 900, 0);
		} else {
			tokens[i] = make_token(candle, 10u, RELATIVE_CLASS_RANGE, -1, 80, 700, 700, 120, 140, 140, 100, 1);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 8u, 100, &dataset_id);
	rc = wave_predictor_encode_base_probs_json(dataset_id, json, sizeof(json));
	printf("[predictive_model_tests] base probs dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"dataset_id\":", "base_probs dataset id");
	expect_contains(json, "\"token_count\":8", "base_probs token count");
	expect_contains(json, "\"name\":\"class\"", "base_probs class feature");
	expect_contains(json, "\"label\":\"trend\"", "base_probs trend label");
	expect_contains(json, "\"count\":6", "base_probs trend count");
	expect_contains(json, "\"label\":\"range\"", "base_probs range label");
	expect_contains(json, "\"count\":2", "base_probs range count");
	core_shutdown();
}

static void test_predictor_repeated_pattern_prediction(void)
{
	WaveToken tokens[24];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	int rc;

	test_log("test_predictor_repeated_pattern_prediction");
	rc = core_init("trainer_test_predictor_repeated_pattern");
	assert(rc == 0);
	for (i = 0u; i < 24u; ++i) {
		if ((i % 2u) == 0u) {
			tokens[i] = make_token(candle, 18u, RELATIVE_CLASS_RANGE, -1, 60, 780, 760, 110, 180, 160, 90, 1);
		} else {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 900, 80, 70, 920, 20, 25, 920, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 24u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] repeated pattern dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"predicted_class\":\"trend\"", "repeated pattern predicted class");
	expect_contains(json, "\"predicted_direction\":\"bullish\"", "repeated pattern predicted direction");
	expect_contains(json, "\"top_class_signals\":[", "repeated pattern top class signals");
	expect_contains(json, "contains_pair(", "repeated pattern pair predicate signal");
	core_shutdown();
}

static void test_predictor_trace_block_present(void)
{
	WaveToken tokens[12];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	int rc;

	test_log("test_predictor_trace_block_present");
	rc = core_init("trainer_test_predictor_trace");
	assert(rc == 0);
	for (i = 0u; i < 12u; ++i) {
		if ((i % 3u) == 0u) {
			tokens[i] = make_token(candle, 14u, RELATIVE_CLASS_RANGE, -1, 55, 700, 720, 100, 150, 130, 120, 1);
		} else {
			tokens[i] = make_token(candle, 5u, RELATIVE_CLASS_TREND, 1, 640, 90, 60, 860, 30, 20, 860, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 12u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] trace dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"trace\":{", "trace block present");
	expect_contains(json, "\"class\":{", "trace class present");
	expect_contains(json, "\"direction\":{", "trace direction present");
	expect_contains(json, "\"move\":{", "trace move present");
	expect_contains(json, "\"temperature\":3.00", "trace temperature present");
	expect_contains(json, "\"families\":[", "trace families present");
	core_shutdown();
}

static void test_predictor_bucket_threshold_boundaries(void)
{
	WaveToken tokens[10];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	int rc;

	test_log("test_predictor_bucket_threshold_boundaries");
	rc = core_init("trainer_test_predictor_bucket_thresholds");
	assert(rc == 0);
	for (i = 0u; i < 10u; ++i) {
		tokens[i] = make_token(candle,
			4u + i,
			RELATIVE_CLASS_TREND,
			1,
			(int32_t)((i + 1u) * 100u),
			100,
			50,
			800,
			(int32_t)((i + 1u) * 100u),
			(int32_t)((i + 1u) * 100u),
			900,
			0);
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 10u, 100, &dataset_id);
	rc = wave_predictor_encode_base_probs_json(dataset_id, json, sizeof(json));
	printf("[predictive_model_tests] bucket thresholds dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"name\":\"abs_net_move\"", "abs_net_move feature exists");
	expect_contains(json, "\"thresholds_normalized\":[1,3,5,7,9]", "abs_net_move thresholds exact");
	expect_contains(json, "\"label\":\"tiny\"", "tiny bucket label exact");
	expect_contains(json, "\"label\":\"small\"", "small bucket label exact");
	expect_contains(json, "\"label\":\"medium\"", "medium bucket label exact");
	expect_contains(json, "\"label\":\"large\"", "large bucket label exact");
	expect_contains(json, "\"label\":\"xlarge\"", "xlarge bucket label exact");
	expect_contains(json, "\"label\":\"impulse\"", "impulse bucket label exact");
	expect_contains(json, "\"count\":1", "bucket count one present");
	expect_contains(json, "\"count\":2", "bucket count two present");
	core_shutdown();
}

static void test_predictor_ambiguous_window_stays_uncertain(void)
{
	WaveToken tokens[5];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t candle = 0u;
	int rc;

	test_log("test_predictor_ambiguous_window_stays_uncertain");
	rc = core_init("trainer_test_predictor_ambiguous");
	assert(rc == 0);
	tokens[0] = make_token(candle, 3u, RELATIVE_CLASS_TREND, 1, 110, 100, 40, 600, 100, 10, 900, 0);
	candle = tokens[0].end_candle + 1u;
	tokens[1] = make_token(candle, 4u, RELATIVE_CLASS_TREND, -1, 220, 220, 120, 500, 200, 20, 700, 1);
	candle = tokens[1].end_candle + 1u;
	tokens[2] = make_token(candle, 5u, RELATIVE_CLASS_RANGE, 1, 330, 340, 220, 400, 300, 30, 500, 0);
	candle = tokens[2].end_candle + 1u;
	tokens[3] = make_token(candle, 6u, RELATIVE_CLASS_TREND, -1, 440, 460, 320, 300, 400, 40, 300, 1);
	candle = tokens[3].end_candle + 1u;
	tokens[4] = make_token(candle, 7u, RELATIVE_CLASS_RANGE, 1, 550, 580, 420, 200, 500, 50, 100, 0);
	(void)create_predictor_dataset(tokens, 5u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] ambiguous dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"predicted_class\":\"trend\"", "ambiguous tie chooses first class");
	expect_contains(json, "\"class_probabilities\":[{\"label\":\"trend\",\"probability\":0.5000},{\"label\":\"range\",\"probability\":0.5000}]", "ambiguous class probabilities exact");
	core_shutdown();
}

static void test_predictor_exact_static_probability_prior_only(void)
{
	WaveToken tokens[5];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t candle = 0u;
	double trend_prob;
	double range_prob;
	int rc;

	test_log("test_predictor_exact_static_probability_prior_only");
	rc = core_init("trainer_test_predictor_exact_static_prior");
	assert(rc == 0);
	tokens[0] = make_token(candle, 3u, RELATIVE_CLASS_TREND, 1, 110, 100, 40, 600, 100, 10, 900, 0);
	candle = tokens[0].end_candle + 1u;
	tokens[1] = make_token(candle, 4u, RELATIVE_CLASS_TREND, -1, 220, 220, 120, 500, 200, 20, 700, 1);
	candle = tokens[1].end_candle + 1u;
	tokens[2] = make_token(candle, 5u, RELATIVE_CLASS_TREND, 1, 330, 340, 220, 400, 300, 30, 500, 0);
	candle = tokens[2].end_candle + 1u;
	tokens[3] = make_token(candle, 6u, RELATIVE_CLASS_TREND, -1, 440, 460, 320, 300, 400, 40, 300, 1);
	candle = tokens[3].end_candle + 1u;
	tokens[4] = make_token(candle, 7u, RELATIVE_CLASS_RANGE, 1, 550, 580, 420, 200, 500, 50, 100, 0);
	(void)create_predictor_dataset(tokens, 5u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] exact static prior dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	trend_prob = extract_named_probability(json, "\"class_probabilities\":", "trend");
	range_prob = extract_named_probability(json, "\"class_probabilities\":", "range");
	printf("[predictive_model_tests] exact static prior trend_prob=%.6f range_prob=%.6f\n",
		trend_prob,
		range_prob);
	expect_contains(json, "\"predicted_class\":\"trend\"", "exact static prior predicted class");
	assert(trend_prob > range_prob);
	assert(trend_prob > 0.50 && trend_prob < 0.70);
	assert(range_prob > 0.30 && range_prob < 0.50);
	core_shutdown();
}

static void test_predictor_family_deduplication(void)
{
	WaveToken tokens[9];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	int rc;

	test_log("test_predictor_family_deduplication");
	rc = core_init("trainer_test_predictor_family_dedup");
	assert(rc == 0);
	for (i = 0u; i < 9u; ++i) {
		if ((i % 2u) == 0u) {
			tokens[i] = make_token(candle, 18u, RELATIVE_CLASS_RANGE, -1, 60, 780, 760, 110, 180, 160, 90, 1);
		} else {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 900, 80, 70, 920, 20, 25, 920, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 9u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] family dedup dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"feature\":\"w[0].direction=bearish\"", "family dedup keeps positional direction signal");
	expect_not_contains(json, "\"feature\":\"contains(direction=bearish)\"", "family dedup suppresses duplicate contains signal");
	expect_not_contains(json, "\"feature\":\"count(direction=bearish)>=1\"", "family dedup suppresses duplicate count signal");
	expect_not_contains(json, "\"feature\":\"majority(direction=bearish)\"", "family dedup suppresses duplicate majority signal");
	core_shutdown();
}

static void test_predictor_strong_prior_remains_dominant(void)
{
	WaveToken tokens[21];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	double trend_prob;
	double range_prob;
	int rc;

	test_log("test_predictor_strong_prior_remains_dominant");
	rc = core_init("trainer_test_predictor_strong_prior_no_signal");
	assert(rc == 0);
	for (i = 0u; i < 21u; ++i) {
		uint32_t class_id = (i < 19u) ? RELATIVE_CLASS_TREND : RELATIVE_CLASS_RANGE;

		tokens[i] = make_token(candle, 6u, class_id, 1, 400, 180, 120, 700, 80, 20, 700, 0);
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 21u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] strong prior dominant dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	trend_prob = extract_named_probability(json, "\"class_probabilities\":", "trend");
	range_prob = extract_named_probability(json, "\"class_probabilities\":", "range");
	printf("[predictive_model_tests] strong prior dominant trend_prob=%.6f range_prob=%.6f\n",
		trend_prob,
		range_prob);
	assert(trend_prob > range_prob);
	expect_contains(json, "\"predicted_class\":\"trend\"", "strong prior dominant predicted class");
	core_shutdown();
}

static void test_predictor_trace_matches_selected_prediction(void)
{
	WaveToken tokens[18];
	char json[TEST_JSON_CAP];
	char predicted_class[32];
	char trace_class[32];
	char predicted_direction[32];
	char trace_direction[32];
	char predicted_move[32];
	char trace_move[32];
	const char *class_trace;
	const char *direction_trace;
	const char *move_trace;
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	double predicted_class_prob;
	double predicted_direction_prob;
	double predicted_move_prob;
	double trace_class_prob;
	double trace_direction_prob;
	double trace_move_prob;
	int rc;

	test_log("test_predictor_trace_matches_selected_prediction");
	rc = core_init("trainer_test_predictor_trace_match");
	assert(rc == 0);
	for (i = 0u; i < 18u; ++i) {
		if ((i % 2u) == 0u) {
			tokens[i] = make_token(candle, 16u, RELATIVE_CLASS_RANGE, -1, 70, 760, 730, 150, 170, 150, 110, 1);
		} else {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 880, 90, 60, 910, 20, 25, 910, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 18u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 1u, 2u, 2u, 0, json, sizeof(json));
	printf("[predictive_model_tests] trace match dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);

	extract_string_after_key(json, "\"predicted_class\":\"", predicted_class, sizeof(predicted_class));
	class_trace = strstr(strstr(json, "\"trace\":{"), "\"class\":{");
	direction_trace = strstr(strstr(json, "\"trace\":{"), "\"direction\":{");
	move_trace = strstr(strstr(json, "\"trace\":{"), "\"move\":{");
	extract_string_after_key(class_trace, "\"selected_label\":\"", trace_class, sizeof(trace_class));
	extract_string_after_key(json, "\"predicted_direction\":\"", predicted_direction, sizeof(predicted_direction));
	extract_string_after_key(direction_trace, "\"selected_label\":\"", trace_direction, sizeof(trace_direction));
	extract_string_after_key(json, "\"predicted_move_bucket\":\"", predicted_move, sizeof(predicted_move));
	extract_string_after_key(move_trace, "\"selected_label\":\"", trace_move, sizeof(trace_move));

	predicted_class_prob = extract_scalar_after_key(json, "\"predicted_class_probability\":");
	predicted_direction_prob = extract_scalar_after_key(json, "\"predicted_direction_probability\":");
	predicted_move_prob = extract_scalar_after_key(json, "\"predicted_move_probability\":");

	trace_class_prob = extract_trace_named_probability(class_trace, trace_class);
	trace_direction_prob = extract_trace_named_probability(direction_trace, trace_direction);
	trace_move_prob = extract_trace_named_probability(move_trace, trace_move);

	printf("[predictive_model_tests] trace match predicted_class=%s trace_class=%s predicted_class_prob=%.4f trace_class_prob=%.4f\n",
		predicted_class,
		trace_class,
		predicted_class_prob,
		trace_class_prob);
	printf("[predictive_model_tests] trace match predicted_direction=%s trace_direction=%s predicted_direction_prob=%.4f trace_direction_prob=%.4f\n",
		predicted_direction,
		trace_direction,
		predicted_direction_prob,
		trace_direction_prob);
	printf("[predictive_model_tests] trace match predicted_move=%s trace_move=%s predicted_move_prob=%.4f trace_move_prob=%.4f\n",
		predicted_move,
		trace_move,
		predicted_move_prob,
		trace_move_prob);

	assert(strcmp(predicted_class, trace_class) == 0);
	assert(strcmp(predicted_direction, trace_direction) == 0);
	assert(strcmp(predicted_move, trace_move) == 0);
	assert(fabs(predicted_class_prob - trace_class_prob) < 0.0002);
	assert(fabs(predicted_direction_prob - trace_direction_prob) < 0.0002);
	assert(fabs(predicted_move_prob - trace_move_prob) < 0.0002);
	core_shutdown();
}

static void test_predictor_with_eval_summary_invariants(void)
{
	WaveToken tokens[24];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	double class_accuracy;
	double direction_accuracy;
	double move_bucket_accuracy;
	double move_sign_accuracy;
	double avg_predicted_class_lift;
	double expected_move_correlation;
	int rc;

	test_log("test_predictor_with_eval_summary_invariants");
	rc = core_init("trainer_test_predictor_eval_summary");
	assert(rc == 0);
	for (i = 0u; i < 24u; ++i) {
		if ((i % 3u) == 0u) {
			tokens[i] = make_token(candle, 15u, RELATIVE_CLASS_RANGE, -1, 65, 760, 740, 140, 180, 160, 100, 1);
		} else {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 820, 100, 70, 900, 30, 25, 900, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 24u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 6u, 3u, 2u, 1, json, sizeof(json));
	printf("[predictive_model_tests] eval summary dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_contains(json, "\"evaluation_summary\":{\"horizons\":[", "evaluation summary present");
	expect_contains(json, "\"class_calibration\":[", "evaluation class calibration present");
	expect_contains(json, "\"direction_calibration\":[", "evaluation direction calibration present");

	class_accuracy = extract_scalar_after_key(json, "\"class_accuracy\":");
	direction_accuracy = extract_scalar_after_key(json, "\"direction_accuracy\":");
	move_bucket_accuracy = extract_scalar_after_key(json, "\"move_bucket_accuracy\":");
	move_sign_accuracy = extract_scalar_after_key(json, "\"move_sign_accuracy\":");
	avg_predicted_class_lift = extract_scalar_after_key(json, "\"avg_predicted_class_lift\":");
	expected_move_correlation = extract_scalar_after_key(json, "\"expected_move_correlation\":");

	printf("[predictive_model_tests] eval summary class_accuracy=%.4f direction_accuracy=%.4f move_bucket_accuracy=%.4f move_sign_accuracy=%.4f avg_predicted_class_lift=%.4f expected_move_correlation=%.6f\n",
		class_accuracy,
		direction_accuracy,
		move_bucket_accuracy,
		move_sign_accuracy,
		avg_predicted_class_lift,
		expected_move_correlation);

	assert(class_accuracy >= 0.0 && class_accuracy <= 1.0);
	assert(direction_accuracy >= 0.0 && direction_accuracy <= 1.0);
	assert(move_bucket_accuracy >= 0.0 && move_bucket_accuracy <= 1.0);
	assert(move_sign_accuracy >= 0.0 && move_sign_accuracy <= 1.0);
	assert(avg_predicted_class_lift > 0.0);
	assert(expected_move_correlation > -1.0001 && expected_move_correlation < 1.0001);
	core_shutdown();
}

static void test_predictor_structured_signal_beats_shuffled_baseline(void)
{
	WaveToken tokens[30];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	double predicted_class_lift;
	double shuffled_class_lift;
	double class_kl_bits;
	double shuffled_class_kl_bits;
	int rc;

	test_log("test_predictor_structured_signal_beats_shuffled_baseline");
	rc = core_init("trainer_test_predictor_structured_vs_shuffled");
	assert(rc == 0);
	for (i = 0u; i < 30u; ++i) {
		if ((i % 2u) == 0u) {
			tokens[i] = make_token(candle, 16u, RELATIVE_CLASS_RANGE, -1, 60, 780, 760, 120, 170, 150, 100, 1);
		} else {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 900, 80, 70, 930, 20, 25, 930, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 30u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 8u, 3u, 2u, 1, json, sizeof(json));
	printf("[predictive_model_tests] structured vs shuffled dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	predicted_class_lift = extract_scalar_after_key(json, "\"predicted_class_lift\":");
	shuffled_class_lift = extract_scalar_after_key(json, "\"shuffled_class_lift\":");
	class_kl_bits = extract_scalar_after_key(json, "\"class_kl_bits\":");
	shuffled_class_kl_bits = extract_scalar_after_key(json, "\"shuffled_class_kl_bits\":");
	printf("[predictive_model_tests] structured vs shuffled predicted_class_lift=%.4f shuffled_class_lift=%.4f class_kl_bits=%.6f shuffled_class_kl_bits=%.6f\n",
		predicted_class_lift,
		shuffled_class_lift,
		class_kl_bits,
		shuffled_class_kl_bits);
	assert(predicted_class_lift >= shuffled_class_lift);
	assert(class_kl_bits >= 0.0);
	assert(shuffled_class_kl_bits >= 0.0);
	core_shutdown();
}

static void test_predictor_evaluation_math_consistency(void)
{
	WaveToken tokens[20];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t i;
	uint32_t candle = 0u;
	double predicted_class_prob;
	double predicted_class_base_prob;
	double predicted_class_lift;
	double expected_move;
	double baseline_expected_move;
	double expected_move_edge;
	double recomputed_class_lift;
	double recomputed_move_edge;
	int rc;

	test_log("test_predictor_evaluation_math_consistency");
	rc = core_init("trainer_test_predictor_eval_math");
	assert(rc == 0);
	for (i = 0u; i < 20u; ++i) {
		if ((i % 4u) == 0u) {
			tokens[i] = make_token(candle, 14u, RELATIVE_CLASS_RANGE, -1, 70, 760, 740, 150, 180, 150, 110, 1);
		} else {
			tokens[i] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 840, 90, 60, 910, 25, 20, 910, 0);
		}
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 20u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 5u, 3u, 2u, 1, json, sizeof(json));
	printf("[predictive_model_tests] eval math dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);

	predicted_class_prob = extract_scalar_after_key(json, "\"predicted_class_probability\":");
	predicted_class_base_prob = extract_scalar_after_key(json, "\"predicted_class_base_probability\":");
	predicted_class_lift = extract_scalar_after_key(json, "\"predicted_class_lift\":");
	expected_move = extract_scalar_after_key(json, "\"expected_net_move_raw\":");
	baseline_expected_move = extract_scalar_after_key(json, "\"baseline_expected_net_move_raw\":");
	expected_move_edge = extract_scalar_after_key(json, "\"expected_move_edge_raw\":");
	recomputed_class_lift = predicted_class_prob / predicted_class_base_prob;
	recomputed_move_edge = expected_move - baseline_expected_move;

	printf("[predictive_model_tests] eval math predicted_class_prob=%.4f base_prob=%.4f lift=%.4f recomputed_lift=%.4f expected_move=%.2f baseline_expected_move=%.2f edge=%.2f recomputed_edge=%.2f\n",
		predicted_class_prob,
		predicted_class_base_prob,
		predicted_class_lift,
		recomputed_class_lift,
		expected_move,
		baseline_expected_move,
		expected_move_edge,
		recomputed_move_edge);

	assert(fabs(predicted_class_lift - recomputed_class_lift) < 0.01);
	assert(fabs(expected_move_edge - recomputed_move_edge) < 0.02);
	core_shutdown();
}

static void test_predictor_probability_sums_randomized(void)
{
	WaveToken tokens[32];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t candle = 0u;
	uint32_t i;
	int rc;
	uint32_t state = 123456789u;

	test_log("test_predictor_probability_sums_randomized");
	rc = core_init("trainer_test_predictor_probability_sums");
	assert(rc == 0);
	for (i = 0u; i < 32u; ++i) {
		uint32_t duration;
		int32_t direction;
		int32_t net_move;
		uint32_t class_id;

		state = state * 1664525u + 1013904223u;
		duration = 2u + (state % 20u);
		state = state * 1664525u + 1013904223u;
		direction = ((state >> 31) != 0u) ? 1 : -1;
		state = state * 1664525u + 1013904223u;
		net_move = (int32_t)(50u + (state % 950u)) * direction;
		state = state * 1664525u + 1013904223u;
		class_id = (state % 3u == 0u) ? RELATIVE_CLASS_RANGE : RELATIVE_CLASS_TREND;
		tokens[i] = make_token(candle,
			duration,
			class_id,
			direction,
			net_move,
			(int32_t)(100u + (state % 800u)),
			(int32_t)(50u + ((state / 7u) % 800u)),
			(int32_t)(100u + ((state / 11u) % 900u)),
			(int32_t)(20u + ((state / 13u) % 400u)),
			(int32_t)(20u + ((state / 17u) % 400u)),
			(int32_t)(50u + ((state / 19u) % 950u)),
			(int32_t)(state & 1u));
		candle = tokens[i].end_candle + 1u;
	}
	(void)create_predictor_dataset(tokens, 32u, 100, &dataset_id);
	rc = wave_predictor_encode_json(dataset_id, 7u, 3u, 2u, 0, json, sizeof(json));
	printf("[predictive_model_tests] probability sums dataset_id=%u rc=%d\n", dataset_id, rc);
	assert(rc > 0);
	expect_probability_sum_close(json, "\"class_probabilities\":", "class probabilities sum to one");
	expect_probability_sum_close(json, "\"direction_probabilities\":", "direction probabilities sum to one");
	expect_probability_sum_close(json, "\"move_probabilities\":", "move probabilities sum to one");
	core_shutdown();
}

static void test_predictor_invalid_inputs(void)
{
	WaveToken tokens[2];
	char json[TEST_JSON_CAP];
	uint32_t dataset_id = 0u;
	uint32_t candle = 0u;
	int rc;

	test_log("test_predictor_invalid_inputs");
	rc = core_init("trainer_test_predictor_invalid_inputs");
	assert(rc == 0);
	tokens[0] = make_token(candle, 4u, RELATIVE_CLASS_TREND, 1, 400, 100, 50, 800, 20, 20, 800, 0);
	candle = tokens[0].end_candle + 1u;
	tokens[1] = make_token(candle, 5u, RELATIVE_CLASS_RANGE, -1, 80, 700, 700, 100, 150, 150, 100, 1);
	(void)create_predictor_dataset(tokens, 2u, 100, &dataset_id);
	rc = wave_predictor_encode_json(9999u, 0u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] invalid dataset rc=%d\n", rc);
	assert(rc < 0);
	rc = wave_predictor_encode_json(dataset_id, 9u, 1u, 1u, 0, json, sizeof(json));
	printf("[predictive_model_tests] invalid anchor rc=%d\n", rc);
	assert(rc < 0);
	rc = wave_predictor_encode_base_probs_json(9999u, json, sizeof(json));
	printf("[predictive_model_tests] invalid base probs rc=%d\n", rc);
	assert(rc < 0);
	core_shutdown();
}

int main(void)
{
	test_log("starting predictor-only tests");
	test_base_probabilities_known_counts();
	test_predictor_repeated_pattern_prediction();
	test_predictor_trace_block_present();
	test_predictor_bucket_threshold_boundaries();
	test_predictor_ambiguous_window_stays_uncertain();
	test_predictor_exact_static_probability_prior_only();
	test_predictor_family_deduplication();
	test_predictor_strong_prior_remains_dominant();
	test_predictor_trace_matches_selected_prediction();
	test_predictor_with_eval_summary_invariants();
	test_predictor_structured_signal_beats_shuffled_baseline();
	test_predictor_evaluation_math_consistency();
	test_predictor_probability_sums_randomized();
	test_predictor_invalid_inputs();
	test_log("all predictor-only tests passed");
	return 0;
}
