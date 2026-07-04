#include "core_api.h"
#include "cluster_dbscan.h"
#include "cluster_graph.h"
#include "cluster_kmedoids.h"
#include "internal_store.h"
#include "micro_harvest.h"
#include "word_mining.h"
#include "phrase_mining.h"
#include "wave_dp.h"
#include "logger.h"

#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#define MAX_JOBS 256

typedef struct {
	JobSummary summary;
	GraphParams graph_params;
	KMedoidsParams kmedoids_params;
	DBSCANParams dbscan_params;
	WordParams word_params;
	PhraseParams phrase_params;
	WaveParams wave_params;
	MicroHarvestSearchConfig micro_harvest_params;
	MicroHarvestReplayConfig micro_harvest_replay_params;
	_Atomic int cancel_requested;
} JobRecord;

static JobRecord g_jobs[MAX_JOBS];
static uint32_t g_job_count;
static uint64_t g_next_job_id = 1;
static pthread_mutex_t g_job_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_job_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static int g_worker_started;
static int g_worker_stop;

static int64_t now_ts(void)
{
	return (int64_t)time(NULL);
}

static JobRecord *find_job_nolock(uint64_t job_id)
{
	uint32_t i;
	for (i = 0; i < g_job_count; ++i) {
		if (g_jobs[i].summary.job_id == job_id) {
			return &g_jobs[i];
		}
	}
	return NULL;
}

static JobRecord *find_next_queued_job_nolock(void)
{
	uint32_t i;
	for (i = 0; i < g_job_count; ++i) {
		if (g_jobs[i].summary.status == JOB_STATUS_QUEUED) {
			return &g_jobs[i];
		}
	}
	return NULL;
}

static void log_job_param_u32(uint64_t job_id, const char *name, uint32_t value)
{
	trainer_log_info("job param job_id=%llu %s=%u",
		(unsigned long long)job_id,
		name,
		value);
}

static void log_job_param_i32(uint64_t job_id, const char *name, int32_t value)
{
	trainer_log_info("job param job_id=%llu %s=%d",
		(unsigned long long)job_id,
		name,
		value);
}

static void log_job_param_f32(uint64_t job_id, const char *name, float value)
{
	trainer_log_info("job param job_id=%llu %s=%.6f",
		(unsigned long long)job_id,
		name,
		value);
}

static void log_graph_job_params(uint64_t job_id, const JobRecord *job)
{
	const GraphParams *p = &job->graph_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_u32(job_id, "q", p->q);
	log_job_param_u32(job_id, "distance_mode", p->distance_mode);
	log_job_param_i32(job_id, "delta", p->delta);
	log_job_param_u32(job_id, "theta", p->theta);
	log_job_param_i32(job_id, "delta_body_gate", p->delta_body_gate);
	log_job_param_i32(job_id, "delta_range_gate", p->delta_range_gate);
	log_job_param_i32(job_id, "delta_bullcount_gate", p->delta_bullcount_gate);
	log_job_param_i32(job_id, "delta_signchange_gate", p->delta_signchange_gate);
	log_job_param_i32(job_id, "delta_upper_wick_gate", p->delta_upper_wick_gate);
	log_job_param_i32(job_id, "delta_lower_wick_gate", p->delta_lower_wick_gate);
	log_job_param_i32(job_id, "delta_lower_ratio_gate", p->delta_lower_ratio_gate);
	log_job_param_i32(job_id, "delta_upper_ratio_gate", p->delta_upper_ratio_gate);
	log_job_param_i32(job_id, "delta_total_wick_ratio_gate", p->delta_total_wick_ratio_gate);
	log_job_param_u32(job_id, "k_neighbors", p->k_neighbors);
	log_job_param_u32(job_id, "use_mutual_knn", p->use_mutual_knn);
	log_job_param_f32(job_id, "max_neighbor_distance", p->max_neighbor_distance);
	log_job_param_u32(job_id, "cluster_mode", p->cluster_mode);
	log_job_param_i32(job_id, "body_mode", p->body_mode);
	log_job_param_f32(job_id, "body_sign_penalty", p->body_sign_penalty);
	log_job_param_u32(job_id, "require_same_majority_sign", p->require_same_majority_sign);
	log_job_param_u32(job_id, "use_upper", p->use_upper);
	log_job_param_u32(job_id, "use_lower", p->use_lower);
	log_job_param_u32(job_id, "use_range", p->use_range);
	log_job_param_u32(job_id, "use_body", p->use_body);
	log_job_param_u32(job_id, "use_composition", p->use_composition);
	log_job_param_u32(job_id, "mirrored", (uint32_t)p->mirrored_mode);
	log_job_param_u32(job_id, "weighted", (uint32_t)p->weighted);
	log_job_param_f32(job_id, "w_upper", p->w_upper);
	log_job_param_f32(job_id, "w_lower", p->w_lower);
	log_job_param_f32(job_id, "w_body", p->w_body);
	log_job_param_f32(job_id, "w_range", p->w_range);
	log_job_param_f32(job_id, "w_bullcount", p->w_bullcount);
	log_job_param_f32(job_id, "w_signchanges", p->w_signchanges);
	log_job_param_f32(job_id, "w_runbull", p->w_runbull);
	log_job_param_f32(job_id, "w_runbear", p->w_runbear);
}

static void log_kmedoids_job_params(uint64_t job_id, const JobRecord *job)
{
	const KMedoidsParams *p = &job->kmedoids_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_u32(job_id, "q", p->q);
	log_job_param_i32(job_id, "delta", p->delta);
	log_job_param_u32(job_id, "k", p->k);
	log_job_param_u32(job_id, "max_iter", p->max_iter);
	log_job_param_u32(job_id, "restarts", p->restarts);
	log_job_param_u32(job_id, "mirrored", (uint32_t)p->mirrored_mode);
	log_job_param_u32(job_id, "weighted", (uint32_t)p->weighted);
	log_job_param_f32(job_id, "w_upper", p->w_upper);
	log_job_param_f32(job_id, "w_lower", p->w_lower);
	log_job_param_f32(job_id, "w_body", p->w_body);
	log_job_param_f32(job_id, "w_range", p->w_range);
}

static void log_dbscan_job_params(uint64_t job_id, const JobRecord *job)
{
	const DBSCANParams *p = &job->dbscan_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_u32(job_id, "q", p->q);
	log_job_param_i32(job_id, "delta", p->delta);
	log_job_param_u32(job_id, "eps", p->eps);
	log_job_param_u32(job_id, "min_pts", p->min_pts);
	log_job_param_u32(job_id, "mirrored", (uint32_t)p->mirrored_mode);
	log_job_param_u32(job_id, "weighted", (uint32_t)p->weighted);
	log_job_param_f32(job_id, "w_upper", p->w_upper);
	log_job_param_f32(job_id, "w_lower", p->w_lower);
	log_job_param_f32(job_id, "w_body", p->w_body);
	log_job_param_f32(job_id, "w_range", p->w_range);
}

static void log_word_job_params(uint64_t job_id, const JobRecord *job)
{
	const WordParams *p = &job->word_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_u32(job_id, "min_len", p->min_len);
	log_job_param_u32(job_id, "max_len", p->max_len);
	log_job_param_u32(job_id, "use_rle", (uint32_t)p->use_rle);
}

static void log_phrase_job_params(uint64_t job_id, const JobRecord *job)
{
	const PhraseParams *p = &job->phrase_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_u32(job_id, "min_len", p->min_len);
	log_job_param_u32(job_id, "max_len", p->max_len);
	log_job_param_u32(job_id, "use_rle", (uint32_t)p->use_rle);
}

static void log_wave_job_params(uint64_t job_id, const JobRecord *job)
{
	const WaveParams *p = &job->wave_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_i32(job_id, "wave_model", p->wave_model);
	log_job_param_u32(job_id, "lmin_candles", p->lmin_candles);
	log_job_param_u32(job_id, "lmax_candles", p->lmax_candles);
	log_job_param_u32(job_id, "regime_context_window", p->regime_context_window);
	log_job_param_u32(job_id, "regime_context_mode", p->regime_context_mode);
	log_job_param_i32(job_id, "min_wave_move_pips", p->min_wave_move_pips);
	log_job_param_i32(job_id, "minimum_pips_for_trend", p->minimum_pips_for_trend);
	log_job_param_i32(job_id, "norm_scale", p->norm_scale);
	log_job_param_i32(job_id, "context_range_floor", p->context_range_floor);
	log_job_param_i32(job_id, "segment_penalty_lambda", p->segment_penalty_lambda);
	log_job_param_i32(job_id, "trend_priority_mode", p->trend_priority_mode);
	log_job_param_i32(job_id, "retrace_break_threshold", p->retrace_break_threshold);
	log_job_param_i32(job_id, "trend_split_gain_min", p->trend_split_gain_min);
	log_job_param_i32(job_id, "compression_min_duration", p->compression_min_duration);
	log_job_param_i32(job_id, "expansion_min_duration", p->expansion_min_duration);
	log_job_param_i32(job_id, "v_reversal_min_trend_strength", p->v_reversal_min_trend_strength);
	log_job_param_i32(job_id, "v_reversal_max_bridge_chop", p->v_reversal_max_bridge_chop);
	log_job_param_i32(job_id, "v_reversal_max_duration", p->v_reversal_max_duration);
}

static void log_micro_harvest_job_params(uint64_t job_id, const JobRecord *job)
{
	const MicroHarvestSearchConfig *p = &job->micro_harvest_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_i32(job_id, "bias", p->bias);
	log_job_param_i32(job_id, "lookback_days", p->lookback_days);
	log_job_param_i32(job_id, "spread_pips", p->spread_pips);
	log_job_param_i32(job_id, "max_orders_min", p->max_orders_min);
	log_job_param_i32(job_id, "max_orders_max", p->max_orders_max);
	log_job_param_i32(job_id, "spacing_min_pips", p->spacing_min_pips);
	log_job_param_i32(job_id, "spacing_max_pips", p->spacing_max_pips);
	log_job_param_i32(job_id, "spacing_step_pips", p->spacing_step_pips);
	log_job_param_i32(job_id, "take_profit_min_pips", p->take_profit_min_pips);
	log_job_param_i32(job_id, "take_profit_max_pips", p->take_profit_max_pips);
	log_job_param_i32(job_id, "take_profit_step_pips", p->take_profit_step_pips);
	log_job_param_i32(job_id, "entry_edge_min", p->entry_edge_min);
	log_job_param_i32(job_id, "entry_edge_max", p->entry_edge_max);
	log_job_param_i32(job_id, "entry_edge_step", p->entry_edge_step);
	log_job_param_i32(job_id, "min_island_height_pips", p->min_island_height_pips);
	log_job_param_i32(job_id, "max_island_height_pips", p->max_island_height_pips);
}

static void log_micro_harvest_replay_job_params(uint64_t job_id, const JobRecord *job)
{
	const MicroHarvestReplayConfig *cfg = &job->micro_harvest_replay_params;
	const MicroHarvestSearchConfig *p = &cfg->search;
	const WaveParams *w = &cfg->wave_params;

	log_job_param_u32(job_id, "dataset_id", job->summary.dataset_id);
	log_job_param_i32(job_id, "bias", p->bias);
	log_job_param_i32(job_id, "lookback_days", p->lookback_days);
	log_job_param_i32(job_id, "spread_pips", p->spread_pips);
	log_job_param_i32(job_id, "max_orders_min", p->max_orders_min);
	log_job_param_i32(job_id, "max_orders_max", p->max_orders_max);
	log_job_param_i32(job_id, "spacing_min_pips", p->spacing_min_pips);
	log_job_param_i32(job_id, "spacing_max_pips", p->spacing_max_pips);
	log_job_param_i32(job_id, "spacing_step_pips", p->spacing_step_pips);
	log_job_param_i32(job_id, "take_profit_min_pips", p->take_profit_min_pips);
	log_job_param_i32(job_id, "take_profit_max_pips", p->take_profit_max_pips);
	log_job_param_i32(job_id, "take_profit_step_pips", p->take_profit_step_pips);
	log_job_param_i32(job_id, "entry_edge_min", p->entry_edge_min);
	log_job_param_i32(job_id, "entry_edge_max", p->entry_edge_max);
	log_job_param_i32(job_id, "entry_edge_step", p->entry_edge_step);
	log_job_param_i32(job_id, "min_island_height_pips", p->min_island_height_pips);
	log_job_param_i32(job_id, "max_island_height_pips", p->max_island_height_pips);
	trainer_log_info("job param job_id=%llu replay_path=%s",
		(unsigned long long)job_id,
		cfg->replay_path);
	log_job_param_i32(job_id, "wave_model", w->wave_model);
	log_job_param_u32(job_id, "lmin_candles", w->lmin_candles);
	log_job_param_u32(job_id, "lmax_candles", w->lmax_candles);
	log_job_param_u32(job_id, "regime_context_window", w->regime_context_window);
	log_job_param_u32(job_id, "regime_context_mode", w->regime_context_mode);
	log_job_param_i32(job_id, "min_wave_move_pips", w->min_wave_move_pips);
	log_job_param_i32(job_id, "minimum_pips_for_trend", w->minimum_pips_for_trend);
	log_job_param_i32(job_id, "norm_scale", w->norm_scale);
	log_job_param_i32(job_id, "context_range_floor", w->context_range_floor);
	log_job_param_i32(job_id, "epsilon", w->epsilon);
	log_job_param_i32(job_id, "segment_penalty_lambda", w->segment_penalty_lambda);
	log_job_param_i32(job_id, "trend_priority_mode", w->trend_priority_mode);
	log_job_param_i32(job_id, "retrace_break_threshold", w->retrace_break_threshold);
	log_job_param_i32(job_id, "trend_split_gain_min", w->trend_split_gain_min);
	log_job_param_i32(job_id, "compression_min_duration", w->compression_min_duration);
	log_job_param_i32(job_id, "expansion_min_duration", w->expansion_min_duration);
	log_job_param_i32(job_id, "v_reversal_min_trend_strength", w->v_reversal_min_trend_strength);
	log_job_param_i32(job_id, "v_reversal_max_bridge_chop", w->v_reversal_max_bridge_chop);
	log_job_param_i32(job_id, "v_reversal_max_duration", w->v_reversal_max_duration);
	log_job_param_i32(job_id, "relative_trend_retracement_max", w->relative_trend_retracement_max);
	log_job_param_i32(job_id, "relative_strong_trend_retracement_max", w->relative_strong_trend_retracement_max);
	log_job_param_i32(job_id, "relative_trend_der_step_min", w->relative_trend_der_step_min);
	log_job_param_i32(job_id, "relative_trend_der_body_min", w->relative_trend_der_body_min);
	log_job_param_i32(job_id, "relative_trend_body_eff_min", w->relative_trend_body_eff_min);
	log_job_param_i32(job_id, "relative_trend_overlap_max", w->relative_trend_overlap_max);
	log_job_param_i32(job_id, "relative_trend_sign_flip_max", w->relative_trend_sign_flip_max);
	log_job_param_i32(job_id, "relative_trend_wick_bias_min", w->relative_trend_wick_bias_min);
	log_job_param_i32(job_id, "relative_range_der_step_max", w->relative_range_der_step_max);
	log_job_param_i32(job_id, "relative_range_body_eff_max", w->relative_range_body_eff_max);
	log_job_param_i32(job_id, "relative_range_overlap_min", w->relative_range_overlap_min);
	log_job_param_i32(job_id, "relative_range_sign_flip_min", w->relative_range_sign_flip_min);
}

static void log_job_params(uint64_t job_id, const JobRecord *job)
{
	if (job->summary.job_type == JOB_TYPE_GRAPH_CLUSTERING) {
		log_graph_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_KMEDOIDS) {
		log_kmedoids_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_DBSCAN) {
		log_dbscan_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_WORD_MINING) {
		log_word_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_PHRASE_MINING) {
		log_phrase_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_WAVE_INFERENCE) {
		log_wave_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_MICRO_HARVEST) {
		log_micro_harvest_job_params(job_id, job);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_MICRO_HARVEST_REPLAY) {
		log_micro_harvest_replay_job_params(job_id, job);
		return;
	}
}

static int micro_harvest_job_run(uint32_t dataset_id,
	const MicroHarvestSearchConfig *config,
	volatile float *progress,
	char *err,
	size_t err_sz)
{
	DatasetRecord *rec;
	MicroHarvestResult result;
	int rc;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		snprintf(err, err_sz, "%s", "dataset not found");
		return -1;
	}
	if (rec->waves.tokens == NULL || rec->waves.token_count == 0) {
		snprintf(err, err_sz, "%s", "Run wave inference first.");
		return -2;
	}
	if (rec->waves.params.wave_model != WAVE_MODEL_LAYERED) {
		snprintf(err, err_sz, "%s", "micro harvest requires layered_waves results");
		return -3;
	}
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run_replay(dataset_id,
		&rec->waves.params,
		config,
		&result,
		progress);
	if (rc != 0) {
		snprintf(err, err_sz, "micro harvest replay failed rc=%d", rc);
		micro_harvest_result_free(&result);
		return rc;
	}
	result.replay_mode = 1;
	snprintf(result.replay_source_path, sizeof(result.replay_source_path), "%s", rec->meta.source_path);
	micro_harvest_result_free(&rec->micro_harvest.latest_result);
	rec->micro_harvest.latest_result = result;
	rec->micro_harvest.available = 1;
	return 0;
}

static int micro_harvest_replay_job_run(uint32_t dataset_id,
	const MicroHarvestReplayConfig *config,
	volatile float *progress,
	char *err,
	size_t err_sz)
{
	DatasetRecord *replay_rec;
	MicroHarvestResult result;
	int rc;

	replay_rec = store_find_dataset(dataset_id);
	if (replay_rec == NULL) {
		snprintf(err, err_sz, "%s", "dataset not found");
		return -1;
	}
	if (config == NULL) {
		snprintf(err, err_sz, "%s", "missing replay config");
		return -2;
	}
	if (config->wave_params.wave_model != WAVE_MODEL_LAYERED) {
		snprintf(err, err_sz, "%s", "micro harvest replay requires layered wave params");
		return -3;
	}
	memset(&result, 0, sizeof(result));
	rc = micro_harvest_run_replay(dataset_id,
		&config->wave_params,
		&config->search,
		&result,
		progress);
	if (rc != 0) {
		snprintf(err, err_sz, "micro harvest replay failed rc=%d", rc);
		micro_harvest_result_free(&result);
		return rc;
	}
	result.replay_mode = 1;
	snprintf(result.replay_source_path, sizeof(result.replay_source_path), "%s", replay_rec->meta.source_path);
	micro_harvest_result_free(&replay_rec->micro_harvest.latest_result);
	replay_rec->micro_harvest.latest_result = result;
	replay_rec->micro_harvest.available = 1;
	return 0;
}

static void run_job_record(JobRecord *job, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz, int *out_rc)
{
	if (job->summary.job_type == JOB_TYPE_GRAPH_CLUSTERING) {
		*out_rc = cluster_graph_run(job->summary.dataset_id, &job->graph_params, progress, cancel_flag, err, err_sz);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_KMEDOIDS) {
		*out_rc = cluster_kmedoids_run(job->summary.dataset_id, &job->kmedoids_params, progress, cancel_flag, err, err_sz);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_DBSCAN) {
		*out_rc = cluster_dbscan_run(job->summary.dataset_id, &job->dbscan_params, progress, cancel_flag, err, err_sz);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_WORD_MINING) {
		*out_rc = word_mining_run(job->summary.dataset_id, &job->word_params, progress, cancel_flag, err, err_sz);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_PHRASE_MINING) {
		*out_rc = phrase_mining_run(job->summary.dataset_id, &job->phrase_params, progress, cancel_flag, err, err_sz);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_WAVE_INFERENCE) {
		*out_rc = wave_dp_run(job->summary.dataset_id, &job->wave_params, progress, cancel_flag, err, err_sz);
		return;
	}
	if (job->summary.job_type == JOB_TYPE_MICRO_HARVEST) {
		*progress = 0.05f;
		*out_rc = micro_harvest_job_run(job->summary.dataset_id,
			&job->micro_harvest_params,
			progress,
			err,
			err_sz);
		if (*out_rc == 0) {
			*progress = 1.0f;
		}
		return;
	}
	if (job->summary.job_type == JOB_TYPE_MICRO_HARVEST_REPLAY) {
		*progress = 0.05f;
		*out_rc = micro_harvest_replay_job_run(job->summary.dataset_id,
			&job->micro_harvest_replay_params,
			progress,
			err,
			err_sz);
		if (*out_rc == 0) {
			*progress = 1.0f;
		}
		return;
	}
	snprintf(err, err_sz, "%s", "unsupported job type");
	*out_rc = -999;
}

static void *worker_loop(void *arg)
{
	(void)arg;
	for (;;) {
		JobRecord *job;
		uint32_t job_index;
		uint64_t job_id;
		char err[256];
		int rc;

		pthread_mutex_lock(&g_job_mu);
		while (!g_worker_stop && find_next_queued_job_nolock() == NULL) {
			pthread_cond_wait(&g_job_cv, &g_job_mu);
		}
		if (g_worker_stop) {
			pthread_mutex_unlock(&g_job_mu);
			break;
		}
		job = find_next_queued_job_nolock();
		if (job == NULL) {
			pthread_mutex_unlock(&g_job_mu);
			continue;
		}
		job->summary.status = JOB_STATUS_RUNNING;
		job->summary.progress = 0.01f;
		job->summary.updated_ts = now_ts();
		job->cancel_requested = 0;
		job_index = (uint32_t)(job - g_jobs);
		job_id = job->summary.job_id;
		trainer_log_info("job start job_id=%llu dataset_id=%u type=%u",
			(unsigned long long)job_id,
			job->summary.dataset_id,
			job->summary.job_type);
		log_job_params(job_id, job);
		pthread_mutex_unlock(&g_job_mu);

		err[0] = '\0';
		rc = -1;
		run_job_record(&g_jobs[job_index],
			&g_jobs[job_index].summary.progress,
			(volatile int *)&g_jobs[job_index].cancel_requested,
			err,
			sizeof(err),
			&rc);

		pthread_mutex_lock(&g_job_mu);
		job = find_job_nolock(job_id);
		if (job == NULL) {
			pthread_mutex_unlock(&g_job_mu);
			continue;
		}
		if (job->cancel_requested) {
			job->summary.status = JOB_STATUS_CANCELLED;
			job->summary.progress = 0.0f;
			snprintf(job->summary.error_message, sizeof(job->summary.error_message), "%s", "cancelled");
			trainer_log_info("job cancelled job_id=%llu", (unsigned long long)job_id);
		} else if (rc != 0) {
			job->summary.status = JOB_STATUS_FAILED;
			snprintf(job->summary.error_message, sizeof(job->summary.error_message), "%s", err[0] ? err : "job failed");
			trainer_log_error("job failed job_id=%llu rc=%d progress=%.3f err=\"%s\"",
				(unsigned long long)job_id,
				rc,
				job->summary.progress,
				job->summary.error_message);
		} else {
			job->summary.status = JOB_STATUS_COMPLETED;
			job->summary.progress = 1.0f;
			job->summary.error_message[0] = '\0';
			trainer_log_info("job completed job_id=%llu", (unsigned long long)job_id);
		}
		job->summary.updated_ts = now_ts();
		pthread_mutex_unlock(&g_job_mu);
	}
	return NULL;
}

int job_manager_init(void)
{
	pthread_mutex_lock(&g_job_mu);
	g_job_count = 0;
	g_next_job_id = 1;
	g_worker_stop = 0;
	if (!g_worker_started) {
		if (pthread_create(&g_worker, NULL, worker_loop, NULL) != 0) {
			pthread_mutex_unlock(&g_job_mu);
			return -1;
		}
		g_worker_started = 1;
	}
	pthread_mutex_unlock(&g_job_mu);
	return 0;
}

void job_manager_shutdown(void)
{
	pthread_mutex_lock(&g_job_mu);
	if (!g_worker_started) {
		pthread_mutex_unlock(&g_job_mu);
		return;
	}
	g_worker_stop = 1;
	pthread_cond_signal(&g_job_cv);
	pthread_mutex_unlock(&g_job_mu);
	pthread_join(g_worker, NULL);
	pthread_mutex_lock(&g_job_mu);
	g_worker_started = 0;
	pthread_mutex_unlock(&g_job_mu);
}

static int push_job(uint32_t dataset_id, uint32_t job_type, const void *params, uint64_t *out_job_id)
{
	JobRecord *j;

	pthread_mutex_lock(&g_job_mu);
	if (g_job_count >= MAX_JOBS) {
		pthread_mutex_unlock(&g_job_mu);
		return -1;
	}
	j = &g_jobs[g_job_count++];
	memset(j, 0, sizeof(*j));
	j->summary.job_id = g_next_job_id++;
	j->summary.dataset_id = dataset_id;
	j->summary.job_type = job_type;
	j->summary.status = JOB_STATUS_QUEUED;
	j->summary.progress = 0.0f;
	j->summary.created_ts = now_ts();
	j->summary.updated_ts = j->summary.created_ts;
	j->summary.error_message[0] = '\0';
	if (job_type == JOB_TYPE_GRAPH_CLUSTERING && params != NULL) {
		j->graph_params = *(const GraphParams *)params;
	}
	if (job_type == JOB_TYPE_KMEDOIDS && params != NULL) {
		j->kmedoids_params = *(const KMedoidsParams *)params;
	}
	if (job_type == JOB_TYPE_DBSCAN && params != NULL) {
		j->dbscan_params = *(const DBSCANParams *)params;
	}
	if (job_type == JOB_TYPE_WORD_MINING && params != NULL) {
		j->word_params = *(const WordParams *)params;
	}
	if (job_type == JOB_TYPE_PHRASE_MINING && params != NULL) {
		j->phrase_params = *(const PhraseParams *)params;
	}
	if (job_type == JOB_TYPE_WAVE_INFERENCE && params != NULL) {
		j->wave_params = *(const WaveParams *)params;
	}
	if (job_type == JOB_TYPE_MICRO_HARVEST && params != NULL) {
		j->micro_harvest_params = *(const MicroHarvestSearchConfig *)params;
	}
	if (job_type == JOB_TYPE_MICRO_HARVEST_REPLAY && params != NULL) {
		j->micro_harvest_replay_params = *(const MicroHarvestReplayConfig *)params;
	}
	if (out_job_id != NULL) {
		*out_job_id = j->summary.job_id;
	}
	trainer_log_info("job queued job_id=%llu dataset_id=%u type=%u",
		(unsigned long long)j->summary.job_id,
		j->summary.dataset_id,
		j->summary.job_type);
	pthread_cond_signal(&g_job_cv);
	pthread_mutex_unlock(&g_job_mu);
	return 0;
}

int core_run_graph_clustering(uint32_t dataset_id, const GraphParams *params, uint64_t *out_job_id)
{
	GraphParams p;

	if (params == NULL) {
		p.q = 4;
		p.distance_mode = DISTANCE_MODE_FEATURE_AWARE;
		p.delta = 4;
		p.theta = 4;
		p.mirrored_mode = 0;
		p.weighted = 1;
		p.delta_body_gate = 8;
		p.delta_range_gate = 8;
		p.delta_bullcount_gate = 2;
		p.delta_signchange_gate = 2;
		p.delta_upper_wick_gate = 8;
		p.delta_lower_wick_gate = 8;
		p.delta_lower_ratio_gate = 25;
		p.delta_upper_ratio_gate = 25;
		p.delta_total_wick_ratio_gate = 30;
		p.k_neighbors = 8;
		p.use_mutual_knn = 1;
		p.max_neighbor_distance = 0.0f;
		p.cluster_mode = GRAPH_CLUSTER_MODE_LABEL_PROPAGATION;
		p.body_mode = 0;
		p.require_same_majority_sign = 0;
		p.use_upper = 1;
		p.use_lower = 1;
		p.use_range = 1;
		p.use_body = 1;
		p.use_composition = 1;
		p.w_upper = 1.0f;
		p.w_lower = 1.0f;
		p.w_body = 1.0f;
		p.w_range = 1.0f;
		p.body_sign_penalty = 2.0f;
		p.w_bullcount = 1.0f;
		p.w_signchanges = 1.0f;
		p.w_runbull = 1.0f;
		p.w_runbear = 1.0f;
		params = &p;
	}
	if (params->q == 0 || params->k_neighbors == 0) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_GRAPH_CLUSTERING, params, out_job_id);
}

int core_run_kmedoids(uint32_t dataset_id, const KMedoidsParams *params, uint64_t *out_job_id)
{
	KMedoidsParams p;
	if (params == NULL) {
		p.q = 4;
		p.delta = 4;
		p.k = 8;
		p.max_iter = 20;
		p.restarts = 1;
		p.mirrored_mode = 0;
		p.weighted = 0;
		p.w_upper = 1.0f;
		p.w_lower = 1.0f;
		p.w_body = 1.0f;
		p.w_range = 1.0f;
		params = &p;
	}
	if (params->q == 0 || params->k == 0) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_KMEDOIDS, params, out_job_id);
}

int core_run_dbscan(uint32_t dataset_id, const DBSCANParams *params, uint64_t *out_job_id)
{
	DBSCANParams p;
	if (params == NULL) {
		p.q = 4;
		p.delta = 4;
		p.eps = 4;
		p.min_pts = 4;
		p.mirrored_mode = 0;
		p.weighted = 0;
		p.w_upper = 1.0f;
		p.w_lower = 1.0f;
		p.w_body = 1.0f;
		p.w_range = 1.0f;
		params = &p;
	}
	if (params->q == 0 || params->min_pts == 0) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_DBSCAN, params, out_job_id);
}

int core_run_word_mining(uint32_t dataset_id, const WordParams *params, uint64_t *out_job_id)
{
	WordParams p;
	if (params == NULL) {
		p.min_len = 2;
		p.max_len = 12;
		p.use_rle = 0;
		params = &p;
	}
	if (params->min_len < 2 || params->max_len < params->min_len) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_WORD_MINING, params, out_job_id);
}

int core_run_phrase_mining(uint32_t dataset_id, const PhraseParams *params, uint64_t *out_job_id)
{
	PhraseParams p;
	if (params == NULL) {
		p.min_len = 2;
		p.max_len = 8;
		p.use_rle = 1;
		params = &p;
	}
	if (params->min_len < 2 || params->max_len < params->min_len) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_PHRASE_MINING, params, out_job_id);
}

int core_run_wave_inference(uint32_t dataset_id, const WaveParams *params, uint64_t *out_job_id)
{
	WaveParams p;

	if (params == NULL) {
		memset(&p, 0, sizeof(p));
		p.wave_model = WAVE_MODEL_LEGACY;
		p.lmin_candles = 1;
		p.lmax_candles = 60;
		p.regime_context_window = 512;
		p.regime_context_mode = WAVE_CONTEXT_TRAILING;
		p.norm_scale = 1000;
		p.context_range_floor = 1;
		p.epsilon = 1;
		p.segment_penalty_lambda = 220;
		p.trend_priority_mode = 1;
		p.retrace_break_threshold = 200;
		p.trend_split_gain_min = 80;
		p.compression_min_duration = 4;
		p.expansion_min_duration = 4;
		p.v_reversal_min_trend_strength = 420;
		p.v_reversal_max_bridge_chop = 520;
		p.v_reversal_max_duration = 12;
		params = &p;
	}
	if (params->lmin_candles == 0 || params->lmax_candles < params->lmin_candles) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_WAVE_INFERENCE, params, out_job_id);
}

int core_run_micro_harvest(uint32_t dataset_id, const MicroHarvestSearchConfig *params, uint64_t *out_job_id)
{
	MicroHarvestSearchConfig p;

	if (params == NULL) {
		memset(&p, 0, sizeof(p));
		p.bias = MH_BIAS_BUY;
		p.lookback_days = 14;
		p.spread_pips = 1;
		p.max_orders_min = 1;
		p.max_orders_max = 4;
		p.spacing_min_pips = 5;
		p.spacing_max_pips = 25;
		p.spacing_step_pips = 5;
		p.take_profit_min_pips = 4;
		p.take_profit_max_pips = 20;
		p.take_profit_step_pips = 4;
		p.entry_edge_min = 600;
		p.entry_edge_max = 900;
		p.entry_edge_step = 50;
		p.min_island_height_pips = 10;
		p.max_island_height_pips = 300;
		params = &p;
	}
	if ((params->bias != MH_BIAS_BUY && params->bias != MH_BIAS_SELL) ||
		params->max_orders_min <= 0 ||
		params->max_orders_max < params->max_orders_min ||
		params->spacing_step_pips <= 0 ||
		params->take_profit_step_pips <= 0 ||
		params->entry_edge_step <= 0) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_MICRO_HARVEST, params, out_job_id);
}

int core_run_micro_harvest_replay(uint32_t dataset_id, const MicroHarvestReplayConfig *params, uint64_t *out_job_id)
{
	MicroHarvestReplayConfig p;

	if (params == NULL) {
		memset(&p, 0, sizeof(p));
		p.search.bias = MH_BIAS_BUY;
		p.search.lookback_days = 14;
		p.search.spread_pips = 50;
		p.search.max_orders_min = 1;
		p.search.max_orders_max = 4;
		p.search.spacing_min_pips = 5;
		p.search.spacing_max_pips = 25;
		p.search.spacing_step_pips = 5;
		p.search.take_profit_min_pips = 50;
		p.search.take_profit_max_pips = 200;
		p.search.take_profit_step_pips = 10;
		p.search.entry_edge_min = 600;
		p.search.entry_edge_max = 900;
		p.search.entry_edge_step = 50;
		p.search.min_island_height_pips = 300;
		p.search.max_island_height_pips = 10000;
		p.wave_params.wave_model = WAVE_MODEL_LAYERED;
		p.wave_params.lmin_candles = 1;
		p.wave_params.lmax_candles = 60;
		p.wave_params.regime_context_window = 512;
		p.wave_params.regime_context_mode = WAVE_CONTEXT_TRAILING;
		p.wave_params.minimum_pips_for_trend = 7;
		p.wave_params.norm_scale = 1000;
		p.wave_params.context_range_floor = 1;
		p.wave_params.epsilon = 1;
		p.wave_params.segment_penalty_lambda = 220;
		p.wave_params.trend_priority_mode = 1;
		p.wave_params.retrace_break_threshold = 200;
		p.wave_params.trend_split_gain_min = 80;
		p.wave_params.compression_min_duration = 4;
		p.wave_params.expansion_min_duration = 4;
		p.wave_params.v_reversal_min_trend_strength = 420;
		p.wave_params.v_reversal_max_bridge_chop = 520;
		p.wave_params.v_reversal_max_duration = 12;
		p.wave_params.relative_trend_retracement_max = 300;
		p.wave_params.relative_strong_trend_retracement_max = 180;
		p.wave_params.relative_trend_der_step_min = 550;
		p.wave_params.relative_trend_der_body_min = 500;
		p.wave_params.relative_trend_body_eff_min = 450;
		p.wave_params.relative_trend_overlap_max = 350;
		p.wave_params.relative_trend_sign_flip_max = 450;
		p.wave_params.relative_trend_wick_bias_min = 1050;
		p.wave_params.relative_range_der_step_max = 500;
		p.wave_params.relative_range_body_eff_max = 500;
		p.wave_params.relative_range_overlap_min = 350;
		p.wave_params.relative_range_sign_flip_min = 400;
		params = &p;
	}
	if ((params->search.bias != MH_BIAS_BUY && params->search.bias != MH_BIAS_SELL) ||
		params->search.max_orders_min <= 0 ||
		params->search.max_orders_max < params->search.max_orders_min ||
		params->search.spacing_step_pips <= 0 ||
		params->search.take_profit_step_pips <= 0 ||
		params->search.entry_edge_step <= 0 ||
		params->wave_params.wave_model != WAVE_MODEL_LAYERED) {
		return -2;
	}
	return push_job(dataset_id, JOB_TYPE_MICRO_HARVEST_REPLAY, params, out_job_id);
}

int core_get_job_count(uint32_t *out_n)
{
	if (out_n == NULL) {
		return -1;
	}
	pthread_mutex_lock(&g_job_mu);
	*out_n = g_job_count;
	pthread_mutex_unlock(&g_job_mu);
	return 0;
}

int core_list_jobs(JobSummary *out_items, uint32_t max_items, uint32_t *out_written)
{
	uint32_t n;
	uint32_t i;

	if (out_written == NULL) {
		return -1;
	}
	pthread_mutex_lock(&g_job_mu);
	n = (g_job_count < max_items) ? g_job_count : max_items;
	if (out_items != NULL && n > 0) {
		for (i = 0; i < n; ++i) {
			out_items[i] = g_jobs[i].summary;
		}
	}
	*out_written = g_job_count;
	pthread_mutex_unlock(&g_job_mu);
	return 0;
}

int core_get_job(uint64_t job_id, JobSummary *out_job)
{
	JobRecord *j;

	if (out_job == NULL) {
		return -1;
	}
	pthread_mutex_lock(&g_job_mu);
	j = find_job_nolock(job_id);
	if (j == NULL) {
		pthread_mutex_unlock(&g_job_mu);
		return -2;
	}
	*out_job = j->summary;
	pthread_mutex_unlock(&g_job_mu);
	return 0;
}

int core_cancel_job(uint64_t job_id)
{
	JobRecord *j;

	pthread_mutex_lock(&g_job_mu);
	j = find_job_nolock(job_id);
	if (j == NULL) {
		pthread_mutex_unlock(&g_job_mu);
		return -2;
	}
	if (j->summary.status == JOB_STATUS_COMPLETED || j->summary.status == JOB_STATUS_FAILED) {
		pthread_mutex_unlock(&g_job_mu);
		return -3;
	}
	if (j->summary.status == JOB_STATUS_QUEUED) {
		j->summary.status = JOB_STATUS_CANCELLED;
		j->summary.updated_ts = now_ts();
		snprintf(j->summary.error_message, sizeof(j->summary.error_message), "%s", "cancelled");
		trainer_log_info("job cancelled while queued job_id=%llu", (unsigned long long)job_id);
		pthread_mutex_unlock(&g_job_mu);
		return 0;
	}
	j->cancel_requested = 1;
	j->summary.updated_ts = now_ts();
	trainer_log_info("job cancel requested job_id=%llu", (unsigned long long)job_id);
	pthread_mutex_unlock(&g_job_mu);
	return 0;
}

int core_delete_job(uint64_t job_id)
{
	uint32_t index;

	pthread_mutex_lock(&g_job_mu);
	for (index = 0; index < g_job_count; ++index) {
		if (g_jobs[index].summary.job_id != job_id) {
			continue;
		}
		if (g_jobs[index].summary.status == JOB_STATUS_QUEUED ||
			g_jobs[index].summary.status == JOB_STATUS_RUNNING) {
			pthread_mutex_unlock(&g_job_mu);
			return -3;
		}
		if (index + 1u < g_job_count) {
			memmove(&g_jobs[index],
				&g_jobs[index + 1u],
				(g_job_count - index - 1u) * sizeof(g_jobs[0]));
		}
		memset(&g_jobs[g_job_count - 1u], 0, sizeof(g_jobs[0]));
		g_job_count -= 1u;
		trainer_log_info("job deleted job_id=%llu", (unsigned long long)job_id);
		pthread_mutex_unlock(&g_job_mu);
		return 0;
	}
	pthread_mutex_unlock(&g_job_mu);
	return -2;
}
