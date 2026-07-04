#ifndef API_TYPES_H
#define API_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	int32_t ts_epoch;
	int32_t open_pips;
	int32_t high_pips;
	int32_t low_pips;
	int32_t close_pips;
	int32_t volume;
} Candle;

typedef struct {
	int32_t u;
	int32_t l;
	int32_t b;
	int32_t r;
} CandleFeat;

typedef struct {
	int32_t bull_count;
	int32_t bear_count;
	int32_t sign_changes;
	int32_t max_bull_run;
	int32_t max_bear_run;
	int32_t net_body;
	int32_t gross_body;
	int32_t avg_range;
	int32_t upper_sum;
	int32_t lower_sum;
} BlockSummary;

typedef struct {
	uint32_t n_blocks;
	uint32_t q;
	uint32_t dim;
	int32_t *data;
	BlockSummary *summaries;
} CharMatrix;

typedef struct {
	uint32_t start_index;
	uint16_t q;
	uint16_t dim;
	int32_t *v;
} CharBlock;

typedef struct {
	uint32_t dataset_id;
	char name[128];
	char source_path[512];
	uint32_t pip_scale;
	uint32_t candle_count;
	uint32_t feature_count;
	uint32_t char_block_count;
	uint32_t q;
} DatasetMeta;

typedef struct {
	uint32_t cluster_id;
	uint32_t size;
	uint32_t representative_index;
	float score;
} ClusterSummary;

typedef struct {
	uint32_t start_candle;
	uint32_t end_candle;
	int32_t start_ts;
	int32_t end_ts;
	uint32_t object_id;
	uint32_t cluster_id;
} Occurrence;

typedef struct {
	int32_t symbol_id;
	uint32_t run_len;
	uint32_t start_char_index;
} RLEToken;

typedef struct {
	uint32_t word_id;
	uint32_t length_chars;
	uint32_t frequency;
} WordSummary;

typedef struct {
	uint32_t phrase_id;
	uint32_t token_len;
	uint32_t frequency;
	float avg_duration_chars;
	float avg_duration_candles;
} PhraseSummary;

typedef struct {
	uint32_t distance_mode;
	int mirrored_mode;
	int weighted;
	int32_t delta;
	float w_upper;
	float w_lower;
	float w_body;
	float w_range;
	float body_sign_penalty;
	float w_bullcount;
	float w_signchanges;
	float w_runbull;
	float w_runbear;
	uint32_t block_q;
	int32_t gate_body_diff;
	int32_t gate_range_diff;
	int32_t gate_bullcount_diff;
	int32_t gate_signchange_diff;
	int32_t gate_upper_wick_diff;
	int32_t gate_lower_wick_diff;
	int32_t gate_lower_ratio_diff;
	int32_t gate_upper_ratio_diff;
	int32_t gate_total_wick_ratio_diff;
	int32_t body_mode;
	uint32_t require_same_majority_sign;
	uint32_t use_upper;
	uint32_t use_lower;
	uint32_t use_range;
	uint32_t use_body;
	uint32_t use_composition;
} DistanceConfig;

typedef enum {
	DISTANCE_MODE_FEATURE_AWARE = 0,
	DISTANCE_MODE_LEGACY = 1,
	DISTANCE_MODE_STRICT_COMPOSITION = 2,
	DISTANCE_MODE_WICK_AWARE = 3
} DistanceMode;

typedef enum {
	GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS = 0,
	GRAPH_CLUSTER_MODE_CONNECTED_COMPONENTS = 1,
	GRAPH_CLUSTER_MODE_LABEL_PROPAGATION = 2,
	GRAPH_CLUSTER_MODE_LOUVAIN = 3
} GraphClusterMode;

typedef struct {
	uint32_t bucket_0;
	uint32_t bucket_1;
	uint32_t bucket_2_3;
	uint32_t bucket_4_7;
	uint32_t bucket_8_15;
	uint32_t bucket_16_plus;
} HistogramSummary;

typedef struct {
	uint32_t n_nodes;
	uint32_t n_edges;
	float average_degree;
	HistogramSummary degree_hist;
	uint32_t raw_component_count;
	uint32_t largest_component_size;
	float largest_component_fraction;
	int percolated;
	HistogramSummary cluster_size_hist;
} GraphDiagnostics;

typedef struct {
	int32_t pip_scale;
	int32_t sort_on_load;
	int32_t dedupe_timestamps;
} LoadOptions;

typedef struct {
	uint32_t q;
	uint32_t distance_mode;
	int32_t delta;
	uint32_t theta;
	int mirrored_mode;
	int weighted;
	int32_t delta_body_gate;
	int32_t delta_range_gate;
	int32_t delta_bullcount_gate;
	int32_t delta_signchange_gate;
	int32_t delta_upper_wick_gate;
	int32_t delta_lower_wick_gate;
	int32_t delta_lower_ratio_gate;
	int32_t delta_upper_ratio_gate;
	int32_t delta_total_wick_ratio_gate;
	uint32_t k_neighbors;
	uint32_t use_mutual_knn;
	float max_neighbor_distance;
	uint32_t cluster_mode;
	int32_t body_mode;
	uint32_t require_same_majority_sign;
	uint32_t use_upper;
	uint32_t use_lower;
	uint32_t use_range;
	uint32_t use_body;
	uint32_t use_composition;
	float w_upper;
	float w_lower;
	float w_body;
	float w_range;
	float body_sign_penalty;
	float w_bullcount;
	float w_signchanges;
	float w_runbull;
	float w_runbear;
} GraphParams;

typedef struct {
	uint32_t q;
	int32_t delta;
	uint32_t k;
	uint32_t max_iter;
	uint32_t restarts;
	int mirrored_mode;
	int weighted;
	float w_upper;
	float w_lower;
	float w_body;
	float w_range;
} KMedoidsParams;

typedef struct {
	uint32_t q;
	int32_t delta;
	uint32_t eps;
	uint32_t min_pts;
	int mirrored_mode;
	int weighted;
	float w_upper;
	float w_lower;
	float w_body;
	float w_range;
} DBSCANParams;

typedef struct {
	uint32_t min_len;
	uint32_t max_len;
	int use_rle;
} WordParams;

typedef struct {
	uint32_t min_len;
	uint32_t max_len;
	int use_rle;
} PhraseParams;

typedef enum {
	WAVE_CONTEXT_CENTERED = 0,
	WAVE_CONTEXT_TRAILING = 1
} WaveContextMode;

typedef enum {
	WAVE_MODEL_LEGACY = 0,
	WAVE_MODEL_PRIMITIVE = 1,
	WAVE_MODEL_RELATIVE = 2,
	WAVE_MODEL_RELATIVE_2_STAGE = 3,
	WAVE_MODEL_RELATIVE_SEEDED = 4,
	WAVE_MODEL_RELATIVE_ITERATIVE = 5,
	WAVE_MODEL_LAYERED = 6
} WaveModelId;

typedef enum {
	PRIMITIVE_CLASS_CONSECUTIVE = 0,
	PRIMITIVE_CLASS_ALTERNATING = 1,
	PRIMITIVE_CLASS_SEPARATOR = 2,
	PRIMITIVE_CLASS_COUNT = 3
} PrimitiveWaveClassId;

typedef enum {
	RELATIVE_CLASS_TREND = 0,
	RELATIVE_CLASS_RANGE = 1,
	RELATIVE_CLASS_COUNT = 2
} RelativeWaveClassId;

typedef enum {
	WAVE_TOKEN_FLAG_SEPARATOR_LEFT_ADDED = 1u << 0,
	WAVE_TOKEN_FLAG_SEPARATOR_RIGHT_ADDED = 1u << 1
} WaveTokenDebugFlag;

typedef enum {
	WAVE_TOKEN_REDUCTION_RULE_FIRST = 1u << 0,
	WAVE_TOKEN_REDUCTION_GPU_DP = 1u << 1,
	WAVE_TOKEN_REDUCTION_SPLIT_MIXED = 1u << 2,
	WAVE_TOKEN_REDUCTION_SPLIT_INTERNAL = 1u << 3,
	WAVE_TOKEN_REDUCTION_SPLIT_EDGE = 1u << 4,
	WAVE_TOKEN_REDUCTION_COALESCE = 1u << 5,
	WAVE_TOKEN_REDUCTION_COLLAPSE_SEPARATOR = 1u << 6,
	WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_CONSEC = 1u << 7,
	WAVE_TOKEN_REDUCTION_MERGE_SEPARATOR_ALT = 1u << 8,
	WAVE_TOKEN_REDUCTION_BRIDGE_NEUTRAL = 1u << 9,
	WAVE_TOKEN_REDUCTION_EXTEND_CONSEC_EDGE = 1u << 10
} WaveTokenReductionFlag;

typedef enum {
	WAVE_CLASS_STRONG_TREND = 0,
	WAVE_CLASS_WEAK_TREND = 1,
	WAVE_CLASS_RETRACEMENT = 2,
	WAVE_CLASS_ALTERNATING_CHOP = 3,
	WAVE_CLASS_MIXED_COMPRESSION = 4,
	WAVE_CLASS_VOLATILE_EXPANSION = 5,
	WAVE_CLASS_V_SHAPED_REVERSAL = 6,
	WAVE_CLASS_COUNT = 7
} WaveClassId;

typedef struct {
	int32_t wave_model;
	uint32_t lmin_candles;
	uint32_t lmax_candles;
	uint32_t regime_context_window;
	uint32_t regime_context_mode;
	int32_t min_wave_move_pips;
	int32_t minimum_pips_for_trend;
	int32_t norm_scale;
	int32_t context_range_floor;
	int32_t epsilon;
	int32_t segment_penalty_lambda;
	int32_t trend_priority_mode;
	int32_t retrace_break_threshold;
	int32_t trend_split_gain_min;
	int32_t compression_min_duration;
	int32_t expansion_min_duration;
	int32_t v_reversal_min_trend_strength;
	int32_t v_reversal_max_bridge_chop;
	int32_t v_reversal_max_duration;
	int32_t weak_fit_threshold;
	int32_t ambiguity_low_margin_threshold;
	int32_t boundary_low_conf_threshold;

	int32_t trend_net_min;
	int32_t trend_purity_min;
	int32_t trend_retrace_max;
	int32_t trend_sign_max;
	int32_t trend_adv_min;
	int32_t trend_w_net;
	int32_t trend_w_purity;
	int32_t trend_w_retrace;
	int32_t trend_w_sign;
	int32_t trend_w_adv;

	int32_t retrace_retrace_min;
	int32_t retrace_net_lo;
	int32_t retrace_net_hi;
	int32_t retrace_sign_lo;
	int32_t retrace_sign_hi;
	int32_t retrace_duration_max;
	int32_t retrace_purity_lo;
	int32_t retrace_purity_hi;
	int32_t retrace_w_retrace;
	int32_t retrace_w_net;
	int32_t retrace_w_sign;
	int32_t retrace_w_duration;
	int32_t retrace_w_purity;

	int32_t chop_sign_min;
	int32_t chop_net_max;
	int32_t chop_purity_max;
	int32_t chop_adv_max;
	int32_t chop_w_sign;
	int32_t chop_w_net;
	int32_t chop_w_purity;
	int32_t chop_w_adv;

	int32_t compress_growth_max;
	int32_t compress_net_lo;
	int32_t compress_net_hi;
	int32_t compress_sign_lo;
	int32_t compress_sign_hi;
	int32_t compress_w_growth;
	int32_t compress_w_net;
	int32_t compress_w_sign;

	int32_t expand_growth_min;
	int32_t expand_gross_min;
	int32_t expand_w_growth;
	int32_t expand_w_gross;

	int32_t v_reversal_w_strength;
	int32_t v_reversal_w_bridge;
	int32_t v_reversal_w_duration;

	int32_t transition_penalty_retracement_edge;
	int32_t transition_penalty_retracement_context;
	int32_t transition_penalty_retracement_direction;
	int32_t transition_penalty_same_dir_trend_split;
	int32_t transition_penalty_opp_dir_trend_no_v;
	int32_t transition_penalty_retracement_chain;

	int32_t relative_trend_retracement_max;
	int32_t relative_strong_trend_retracement_max;
	int32_t relative_trend_der_step_min;
	int32_t relative_trend_der_body_min;
	int32_t relative_trend_body_eff_min;
	int32_t relative_trend_overlap_max;
	int32_t relative_trend_sign_flip_max;
	int32_t relative_trend_wick_bias_min;
	int32_t relative_range_der_step_max;
	int32_t relative_range_body_eff_max;
	int32_t relative_range_overlap_min;
	int32_t relative_range_sign_flip_min;
} WaveParams;

typedef struct {
	uint32_t start_candle;
	uint32_t end_candle;
	uint32_t seed_start_candle;
	uint32_t seed_end_candle;
	uint32_t seed_pass;
	uint32_t layered_child_start_index;
	uint32_t layered_child_end_index;
	uint32_t layered_sequential_count;
	uint32_t layered_alternating_count;
	uint32_t layered_sequential_candles;
	uint32_t layered_alternating_candles;
	int32_t layered_sequential_net_move_sum_raw;
	int32_t layered_alternating_net_move_sum_raw;
	int32_t layered_directional_agreement;
	uint32_t class_id;
	int32_t fit_error;
	int32_t second_best_error;
	int32_t ambiguity_margin;
	uint32_t duration_candles;
	int32_t net_move_raw;
	int32_t retracement_ratio;
	int32_t sign_change_density;
	int32_t directional_purity_body;
	int32_t range_growth_ratio;
	int32_t context_range;
	int32_t primitive_consecutive_ratio;
	int32_t primitive_alternating_ratio;
	int32_t primitive_max_consecutive_run;
	int32_t primitive_max_alternating_run;
	int32_t primitive_alternating_pair_count;
	int32_t primitive_counter_body_ratio;
	int32_t primitive_minority_body_ratio;
	int32_t relative_net_move_raw;
	int32_t relative_abs_body_sum_raw;
	int32_t relative_abs_step_sum_raw;
	int32_t relative_directional_efficiency_body;
	int32_t relative_directional_efficiency_step;
	int32_t relative_avg_body_efficiency;
	int32_t relative_body_direction_share;
	int32_t relative_upper_wick_sum_raw;
	int32_t relative_lower_wick_sum_raw;
	int32_t relative_total_wick_sum_raw;
	int32_t relative_wick_to_body_ratio;
	int32_t relative_wick_bias_up;
	int32_t relative_wick_bias_down;
	int32_t relative_wick_bias_aligned;
	int32_t relative_max_pullback_raw;
	int32_t relative_impulse_leg_raw;
	int32_t relative_retracement_ratio;
	int32_t relative_body_overlap_sum_raw;
	int32_t relative_overlap_ratio;
	int32_t relative_range_growth_ratio;
	int32_t relative_body_growth_ratio;
	int32_t relative_trend_quality_raw;
	int32_t relative_trend_quality_bucket;
	int32_t knn_class_agreement;
	int32_t knn_neighbor_purity;
	int32_t knn_distance_continuation;
	int32_t knn_distance_exhaustion;
	int32_t knn_majority_label;
	int32_t knn_margin;
	int32_t dominant_direction;
	int32_t boundary_confidence;
	int32_t weak_fit;
	uint32_t debug_flags;
	uint32_t debug_reduction_flags;
	uint32_t debug_reduction_count;
} WaveToken;

typedef struct {
	uint32_t rule_retracement_edge_count;
	uint32_t rule_retracement_context_count;
	uint32_t rule_retracement_direction_count;
	uint32_t rule_retracement_chain_count;
	uint32_t same_direction_trend_merged_count;
	uint32_t same_direction_trend_kept_count;
	uint32_t opposite_direction_v_reversal_merged_count;
	uint32_t opposite_direction_v_reversal_rejected_count;
	uint32_t retracement_threshold_rejected_count;
	uint32_t low_margin_token_count;
	uint32_t low_boundary_confidence_count;
	uint32_t weak_fit_count;
	uint32_t long_token_count;
	float avg_ambiguity_margin;
	float avg_token_length;
	float tokens_per_1000_candles;
	float pct_one_candle_tokens;
	float pct_two_candle_tokens;
	float pct_near_lmax_tokens;
	float avg_long_token_error;
	float avg_boundary_confidence;
	uint32_t class_candle_coverage[WAVE_CLASS_COUNT];
	float class_avg_length[WAVE_CLASS_COUNT];
	float avg_relative_directional_efficiency_step;
	float avg_relative_directional_efficiency_body;
	float avg_relative_retracement_ratio;
	float avg_relative_overlap_ratio;
	float avg_relative_body_efficiency;
	uint32_t relative_trend_count;
	uint32_t seeded_pass1_trend_count;
	uint32_t seeded_pass2_trend_count;
	uint32_t relative_range_count;
	uint32_t relative_trend_low_efficiency_count;
	uint32_t relative_trend_high_retracement_count;
	uint32_t relative_range_updrift_count;
	uint32_t relative_range_downdrift_count;
} WaveKPI;

typedef struct {
	uint32_t token_count;
	int64_t total_fit_error;
	uint32_t class_counts[WAVE_CLASS_COUNT];
	WaveParams params;
	WaveKPI kpi;
} WaveSummary;

typedef enum {
	JOB_TYPE_GRAPH_CLUSTERING = 1,
	JOB_TYPE_WORD_MINING = 2,
	JOB_TYPE_PHRASE_MINING = 3,
	JOB_TYPE_KMEDOIDS = 4,
	JOB_TYPE_DBSCAN = 5,
	JOB_TYPE_WAVE_INFERENCE = 6,
	JOB_TYPE_MICRO_HARVEST = 7,
	JOB_TYPE_MICRO_HARVEST_REPLAY = 8
} JobType;

typedef enum {
	JOB_STATUS_QUEUED = 1,
	JOB_STATUS_RUNNING = 2,
	JOB_STATUS_COMPLETED = 3,
	JOB_STATUS_FAILED = 4,
	JOB_STATUS_CANCELLED = 5
} JobStatus;

typedef struct {
	uint64_t job_id;
	uint32_t dataset_id;
	uint32_t job_type;
	uint32_t status;
	float progress;
	int64_t created_ts;
	int64_t updated_ts;
	char error_message[256];
} JobSummary;

#define MH_BIAS_BUY 1
#define MH_BIAS_SELL -1
#define MH_MAX_OPEN_ORDERS 32

typedef struct {
	int32_t bias;
	int32_t max_orders;
	int32_t min_spacing_pips;
	int32_t take_profit_pips;
	int32_t spread_pips;
	int32_t min_island_height_pips;
	int32_t max_island_height_pips;
	int32_t min_alternating_ratio;
	int32_t min_overlap_ratio;
	int32_t max_drift_ratio;
	int32_t max_directional_agreement;
	int32_t max_range_variance_ratio;
	int32_t entry_edge_fraction;
	int32_t cooldown_candles;
	int32_t signal_feature_id;
	int32_t signal_compare_op;
	int32_t signal_threshold;
} MicroHarvestParams;

typedef struct {
	int32_t bias;
	int32_t lookback_days;
	int32_t spread_pips;
	int32_t max_orders_min;
	int32_t max_orders_max;
	int32_t spacing_min_pips;
	int32_t spacing_max_pips;
	int32_t spacing_step_pips;
	int32_t take_profit_min_pips;
	int32_t take_profit_max_pips;
	int32_t take_profit_step_pips;
	int32_t entry_edge_min;
	int32_t entry_edge_max;
	int32_t entry_edge_step;
	int32_t min_island_height_pips;
	int32_t max_island_height_pips;
} MicroHarvestSearchConfig;

typedef struct {
	MicroHarvestSearchConfig search;
	WaveParams wave_params;
	char replay_path[512];
} MicroHarvestReplayConfig;

typedef struct {
	int32_t direction;
	int32_t open_candle_index;
	int32_t open_price_pips;
	int32_t active;
} MicroHarvestOpenOrder;

typedef struct {
	MicroHarvestOpenOrder open_orders[MH_MAX_OPEN_ORDERS];
	int32_t open_count;
	int32_t max_open_count;
	int32_t closed_profit_pips;
	int32_t closed_trade_count;
	int32_t ignored_open_trade_count;
} MicroHarvestSimState;

typedef struct {
	int32_t island_index;
	int32_t open_candle_index;
	int32_t close_candle_index;
	int32_t direction;
	int32_t open_price_pips;
	int32_t close_price_pips;
	int32_t take_profit_pips;
	int32_t profit_pips;
	int32_t closed;
} MicroHarvestTrade;

typedef struct {
	int32_t index;
	int32_t start_candle_index;
	int32_t end_candle_index;
	int32_t start_wave_index;
	int32_t end_wave_index;
	int32_t low_price_pips;
	int32_t high_price_pips;
	int32_t height_pips;
	int32_t profit_pips;
	int32_t closed_trade_count;
	int32_t ignored_open_trade_count;
	int32_t max_concurrent_open_trade_count;
	int32_t selected;
} MicroHarvestIsland;

typedef struct {
	MicroHarvestParams recommended_params;
	int32_t total_profit_pips;
	int32_t total_closed_trade_count;
	int32_t total_ignored_open_trade_count;
	int32_t total_max_concurrent_open_trade_count;
	int32_t selected_island_count;
	int32_t replay_mode;
	char replay_source_path[512];
	MicroHarvestIsland *islands;
	int32_t island_count;
	int32_t island_capacity;
	MicroHarvestTrade *trades;
	int32_t trade_count;
	int32_t trade_capacity;
} MicroHarvestResult;

#endif
