#ifndef INTERNAL_STORE_H
#define INTERNAL_STORE_H

#include "api_types.h"

#define MAX_DATASETS 32

typedef struct {
	uint32_t algorithm_type;
	uint32_t cluster_count;
	uint32_t noise_count;
	uint32_t graph_cluster_mode;
	int32_t *cluster_ids;
	ClusterSummary *summaries;
	GraphDiagnostics graph_diag;
} ClusterResult;

typedef struct {
	WaveToken *tokens;
	uint32_t token_count;
	WaveToken *layer1_tokens;
	uint32_t layer1_token_count;
	int64_t total_fit_error;
	uint32_t class_counts[WAVE_CLASS_COUNT];
	WaveParams params;
	WaveKPI kpi;
} WaveResult;

typedef struct {
	MicroHarvestResult latest_result;
	int available;
} MicroHarvestStore;

typedef struct {
	int used;
	DatasetMeta meta;
	Candle *candles;
	CandleFeat *features;
	CharMatrix chars;
	ClusterResult clustering;

	int32_t *symbol_stream;
	uint32_t symbol_count;
	RLEToken *rle_tokens;
	uint32_t rle_count;
	WordSummary *words;
	uint32_t word_count;
	PhraseSummary *phrases;
	uint32_t phrase_count;
	WaveResult waves;
	MicroHarvestStore micro_harvest;
} DatasetRecord;

typedef struct {
	int initialized;
	char storage_root[512];
	uint32_t next_dataset_id;
	DatasetRecord datasets[MAX_DATASETS];
} GlobalStore;

GlobalStore *store_get(void);
DatasetRecord *store_find_dataset(uint32_t dataset_id);
DatasetRecord *store_alloc_dataset(uint32_t *out_dataset_id);
void store_free_dataset_buffers(DatasetRecord *rec);
int store_name_from_path(const char *path, char *out, size_t out_sz);

#endif
