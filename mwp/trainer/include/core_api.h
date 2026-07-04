#ifndef CORE_API_H
#define CORE_API_H

#include "api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int core_init(const char *storage_root);
void core_shutdown(void);

int core_import_mt4_csv(const char *path, const LoadOptions *opt, uint32_t *out_dataset_id);
int core_build_candle_features(uint32_t dataset_id);
int core_build_character_blocks(uint32_t dataset_id, uint32_t q);

int core_get_dataset_count(uint32_t *out_n);
int core_get_dataset_meta(uint32_t dataset_id, DatasetMeta *out_meta);
int core_list_datasets(DatasetMeta *out_items, uint32_t max_items, uint32_t *out_written);

int core_get_candles(uint32_t dataset_id, uint32_t start, uint32_t count, Candle *out_items, uint32_t *out_written);
int core_get_features(uint32_t dataset_id, uint32_t start, uint32_t count, CandleFeat *out_items, uint32_t *out_written);

int core_distance_block_pair(const CharMatrix *m, uint32_t a_idx, uint32_t b_idx, const DistanceConfig *cfg, float *out_dist);

int core_run_graph_clustering(uint32_t dataset_id, const GraphParams *params, uint64_t *out_job_id);
int core_run_kmedoids(uint32_t dataset_id, const KMedoidsParams *params, uint64_t *out_job_id);
int core_run_dbscan(uint32_t dataset_id, const DBSCANParams *params, uint64_t *out_job_id);
int core_run_word_mining(uint32_t dataset_id, const WordParams *params, uint64_t *out_job_id);
int core_run_phrase_mining(uint32_t dataset_id, const PhraseParams *params, uint64_t *out_job_id);
int core_run_wave_inference(uint32_t dataset_id, const WaveParams *params, uint64_t *out_job_id);
int core_run_micro_harvest(uint32_t dataset_id, const MicroHarvestSearchConfig *params, uint64_t *out_job_id);
int core_run_micro_harvest_replay(uint32_t dataset_id, const MicroHarvestReplayConfig *params, uint64_t *out_job_id);

int core_get_cluster_count(uint32_t dataset_id, uint32_t *out_n);
int core_get_cluster_summaries(uint32_t dataset_id, ClusterSummary *out_items, uint32_t max_items, uint32_t *out_written);
int core_get_cluster_occurrences(uint32_t dataset_id,
	uint32_t cluster_id,
	uint32_t start,
	uint32_t count,
	Occurrence *out_items,
	uint32_t *out_written,
	uint32_t *out_total);
int core_get_word_summaries(uint32_t dataset_id, WordSummary *out_items, uint32_t max_items, uint32_t *out_written);
int core_get_phrase_summaries(uint32_t dataset_id, PhraseSummary *out_items, uint32_t max_items, uint32_t *out_written);
int core_get_wave_summary(uint32_t dataset_id, WaveSummary *out_summary);
int core_get_wave_tokens(uint32_t dataset_id, uint32_t start, uint32_t count, WaveToken *out_items, uint32_t *out_written, uint32_t *out_total);

int core_get_job_count(uint32_t *out_n);
int core_list_jobs(JobSummary *out_items, uint32_t max_items, uint32_t *out_written);
int core_get_job(uint64_t job_id, JobSummary *out_job);
int core_cancel_job(uint64_t job_id);
int core_delete_job(uint64_t job_id);

int core_http_serve(const char *bind_ip, uint16_t port);

#ifdef __cplusplus
}
#endif

#endif
