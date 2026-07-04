#include "core_api.h"
#include "internal_store.h"
#include "gpu_dispatch.h"
#include "live_store.h"
#include "micro_harvest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int job_manager_init(void);
void job_manager_shutdown(void);

static GlobalStore g_store;

GlobalStore *store_get(void)
{
	return &g_store;
}

void store_free_dataset_buffers(DatasetRecord *rec)
{
	if (rec == NULL) {
		return;
	}
	free(rec->candles);
	rec->candles = NULL;
	free(rec->features);
	rec->features = NULL;
	free(rec->chars.data);
	rec->chars.data = NULL;
	free(rec->chars.summaries);
	rec->chars.summaries = NULL;
	free(rec->clustering.cluster_ids);
	rec->clustering.cluster_ids = NULL;
	free(rec->clustering.summaries);
	rec->clustering.summaries = NULL;
	rec->clustering.cluster_count = 0;
	rec->clustering.noise_count = 0;
	rec->clustering.algorithm_type = 0;
	rec->clustering.graph_cluster_mode = 0;
	memset(&rec->clustering.graph_diag, 0, sizeof(rec->clustering.graph_diag));
	free(rec->symbol_stream);
	rec->symbol_stream = NULL;
	rec->symbol_count = 0;
	free(rec->rle_tokens);
	rec->rle_tokens = NULL;
	rec->rle_count = 0;
	free(rec->words);
	rec->words = NULL;
	rec->word_count = 0;
	free(rec->phrases);
	rec->phrases = NULL;
	rec->phrase_count = 0;
	free(rec->waves.tokens);
	rec->waves.tokens = NULL;
	rec->waves.token_count = 0;
	free(rec->waves.layer1_tokens);
	rec->waves.layer1_tokens = NULL;
	rec->waves.layer1_token_count = 0;
	rec->waves.total_fit_error = 0;
	memset(rec->waves.class_counts, 0, sizeof(rec->waves.class_counts));
	memset(&rec->waves.params, 0, sizeof(rec->waves.params));
	memset(&rec->waves.kpi, 0, sizeof(rec->waves.kpi));
	micro_harvest_result_free(&rec->micro_harvest.latest_result);
	rec->micro_harvest.available = 0;
	rec->chars.n_blocks = 0;
	rec->chars.dim = 0;
	rec->chars.q = 0;
}

DatasetRecord *store_find_dataset(uint32_t dataset_id)
{
	uint32_t i;

	for (i = 0; i < MAX_DATASETS; ++i) {
		if (g_store.datasets[i].used && g_store.datasets[i].meta.dataset_id == dataset_id) {
			return &g_store.datasets[i];
		}
	}
	return NULL;
}

DatasetRecord *store_alloc_dataset(uint32_t *out_dataset_id)
{
	uint32_t i;

	for (i = 0; i < MAX_DATASETS; ++i) {
		if (!g_store.datasets[i].used) {
			DatasetRecord *rec = &g_store.datasets[i];
			memset(rec, 0, sizeof(*rec));
			rec->used = 1;
			rec->meta.dataset_id = g_store.next_dataset_id++;
			if (out_dataset_id != NULL) {
				*out_dataset_id = rec->meta.dataset_id;
			}
			return rec;
		}
	}
	return NULL;
}

int store_name_from_path(const char *path, char *out, size_t out_sz)
{
	const char *base;

	if (path == NULL || out == NULL || out_sz == 0) {
		return -1;
	}
	base = strrchr(path, '/');
	if (base == NULL) {
		base = path;
	} else {
		base += 1;
	}
	if (snprintf(out, out_sz, "%s", base) <= 0) {
		return -1;
	}
	return 0;
}

int core_init(const char *storage_root)
{
	memset(&g_store, 0, sizeof(g_store));
	live_store_clear_runtime();
	g_store.initialized = 1;
	g_store.next_dataset_id = 1;
	if (storage_root != NULL && storage_root[0] != '\0') {
		snprintf(g_store.storage_root, sizeof(g_store.storage_root), "%s", storage_root);
	} else {
		snprintf(g_store.storage_root, sizeof(g_store.storage_root), "%s", "trainer_data");
	}
	job_manager_init();
	return 0;
}

void core_shutdown(void)
{
	uint32_t i;

	job_manager_shutdown();
	for (i = 0; i < MAX_DATASETS; ++i) {
		if (g_store.datasets[i].used) {
			store_free_dataset_buffers(&g_store.datasets[i]);
			g_store.datasets[i].used = 0;
		}
	}
	gpu_dispatch_shutdown();
	g_store.initialized = 0;
}

int core_get_dataset_count(uint32_t *out_n)
{
	uint32_t i;
	uint32_t n = 0;

	if (out_n == NULL) {
		return -1;
	}
	for (i = 0; i < MAX_DATASETS; ++i) {
		if (g_store.datasets[i].used) {
			n += 1;
		}
	}
	*out_n = n;
	return 0;
}

int core_get_dataset_meta(uint32_t dataset_id, DatasetMeta *out_meta)
{
	DatasetRecord *rec;

	if (out_meta == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	*out_meta = rec->meta;
	return 0;
}

int core_list_datasets(DatasetMeta *out_items, uint32_t max_items, uint32_t *out_written)
{
	uint32_t i;
	uint32_t w = 0;

	if (out_written == NULL) {
		return -1;
	}
	for (i = 0; i < MAX_DATASETS; ++i) {
		if (!g_store.datasets[i].used) {
			continue;
		}
		if (out_items != NULL && w < max_items) {
			out_items[w] = g_store.datasets[i].meta;
		}
		w += 1;
	}
	*out_written = w;
	return 0;
}

int core_get_candles(uint32_t dataset_id, uint32_t start, uint32_t count, Candle *out_items, uint32_t *out_written)
{
	DatasetRecord *rec;
	uint32_t remain;
	uint32_t n;

	if (out_written == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	if (start >= rec->meta.candle_count) {
		*out_written = 0;
		return 0;
	}
	remain = rec->meta.candle_count - start;
	n = (count < remain) ? count : remain;
	if (out_items != NULL && n > 0) {
		memcpy(out_items, rec->candles + start, n * sizeof(Candle));
	}
	*out_written = n;
	return 0;
}

int core_get_features(uint32_t dataset_id, uint32_t start, uint32_t count, CandleFeat *out_items, uint32_t *out_written)
{
	DatasetRecord *rec;
	uint32_t remain;
	uint32_t n;

	if (out_written == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	if (start >= rec->meta.feature_count) {
		*out_written = 0;
		return 0;
	}
	remain = rec->meta.feature_count - start;
	n = (count < remain) ? count : remain;
	if (out_items != NULL && n > 0) {
		memcpy(out_items, rec->features + start, n * sizeof(CandleFeat));
	}
	*out_written = n;
	return 0;
}

int core_get_cluster_count(uint32_t dataset_id, uint32_t *out_n)
{
	DatasetRecord *rec;

	if (out_n == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	*out_n = rec->clustering.cluster_count;
	return 0;
}

int core_get_cluster_summaries(uint32_t dataset_id, ClusterSummary *out_items, uint32_t max_items, uint32_t *out_written)
{
	DatasetRecord *rec;
	uint32_t n;

	if (out_written == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	n = (rec->clustering.cluster_count < max_items) ? rec->clustering.cluster_count : max_items;
	if (out_items != NULL && n > 0) {
		memcpy(out_items, rec->clustering.summaries, n * sizeof(ClusterSummary));
	}
	*out_written = rec->clustering.cluster_count;
	return 0;
}

int core_get_cluster_occurrences(uint32_t dataset_id,
	uint32_t cluster_id,
	uint32_t start,
	uint32_t count,
	Occurrence *out_items,
	uint32_t *out_written,
	uint32_t *out_total)
{
	DatasetRecord *rec;
	uint32_t total = 0;
	uint32_t written = 0;
	uint32_t i;

	if (out_written == NULL || out_total == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	if (rec->clustering.cluster_ids == NULL || rec->chars.q == 0u || rec->candles == NULL) {
		return -3;
	}
	if (cluster_id >= rec->clustering.cluster_count) {
		return -4;
	}

	for (i = 0; i < rec->chars.n_blocks; ++i) {
		uint32_t end_candle;

		if ((uint32_t)rec->clustering.cluster_ids[i] != cluster_id) {
			continue;
		}
		if (total >= start && written < count && out_items != NULL) {
			Occurrence *occ = &out_items[written];

			end_candle = i + rec->chars.q - 1u;
			occ->start_candle = i;
			occ->end_candle = end_candle;
			occ->start_ts = rec->candles[i].ts_epoch;
			occ->end_ts = rec->candles[end_candle].ts_epoch;
			occ->object_id = i;
			occ->cluster_id = cluster_id;
			written += 1u;
		}
		total += 1u;
	}

	*out_written = written;
	*out_total = total;
	return 0;
}

int core_get_word_summaries(uint32_t dataset_id, WordSummary *out_items, uint32_t max_items, uint32_t *out_written)
{
	DatasetRecord *rec;
	uint32_t n;

	if (out_written == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	n = (rec->word_count < max_items) ? rec->word_count : max_items;
	if (out_items != NULL && n > 0) {
		memcpy(out_items, rec->words, n * sizeof(WordSummary));
	}
	*out_written = rec->word_count;
	return 0;
}

int core_get_phrase_summaries(uint32_t dataset_id, PhraseSummary *out_items, uint32_t max_items, uint32_t *out_written)
{
	DatasetRecord *rec;
	uint32_t n;

	if (out_written == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	n = (rec->phrase_count < max_items) ? rec->phrase_count : max_items;
	if (out_items != NULL && n > 0) {
		memcpy(out_items, rec->phrases, n * sizeof(PhraseSummary));
	}
	*out_written = rec->phrase_count;
	return 0;
}

int core_get_wave_summary(uint32_t dataset_id, WaveSummary *out_summary)
{
	DatasetRecord *rec;

	if (out_summary == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	out_summary->token_count = rec->waves.token_count;
	out_summary->total_fit_error = rec->waves.total_fit_error;
	memcpy(out_summary->class_counts, rec->waves.class_counts, sizeof(out_summary->class_counts));
	out_summary->params = rec->waves.params;
	out_summary->kpi = rec->waves.kpi;
	return 0;
}

int core_get_wave_tokens(uint32_t dataset_id, uint32_t start, uint32_t count, WaveToken *out_items, uint32_t *out_written, uint32_t *out_total)
{
	DatasetRecord *rec;
	uint32_t remain;
	uint32_t n;

	if (out_written == NULL || out_total == NULL) {
		return -1;
	}
	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -2;
	}
	*out_total = rec->waves.token_count;
	if (start >= rec->waves.token_count) {
		*out_written = 0;
		return 0;
	}
	remain = rec->waves.token_count - start;
	n = (count < remain) ? count : remain;
	if (out_items != NULL && n > 0) {
		memcpy(out_items, rec->waves.tokens + start, (size_t)n * sizeof(*out_items));
	}
	*out_written = n;
	return 0;
}
