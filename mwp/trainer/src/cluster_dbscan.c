#include "cluster_dbscan.h"
#include "core_api.h"
#include "gpu_dispatch.h"
#include "internal_store.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LABEL_UNVISITED (-99)
#define LABEL_NOISE (-1)

static void set_err(char *err, size_t err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
	if (msg != NULL && msg[0] != '\0') {
		trainer_log_error("dbscan error: %s", msg);
	}
}

static int ensure_chars(uint32_t dataset_id, uint32_t q, DatasetRecord **out_rec)
{
	DatasetRecord *rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		return -1;
	}
	if (rec->features == NULL && core_build_candle_features(dataset_id) != 0) {
		return -2;
	}
	if ((rec->chars.data == NULL || rec->chars.q != q) && core_build_character_blocks(dataset_id, q) != 0) {
		return -3;
	}
	if (out_rec != NULL) {
		*out_rec = rec;
	}
	return 0;
}

static int distance_pair_gpu(const CharMatrix *m, uint32_t a, uint32_t b, const DistanceConfig *cfg, int32_t *out_d)
{
	return gpu_distance_tile(m, a, 1, b, 1, cfg, out_d);
}

static int gather_neighbors(const CharMatrix *m,
	uint32_t p,
	uint32_t eps,
	const DistanceConfig *cfg,
	uint32_t *out_neighbors,
	uint32_t max_neighbors,
	uint32_t *out_count)
{
	uint32_t j;
	uint32_t c = 0;

	for (j = 0; j < m->n_blocks; ++j) {
		int32_t d;
		if (distance_pair_gpu(m, p, j, cfg, &d) != 0) {
			return -1;
		}
		if ((uint32_t)d <= eps) {
			if (c < max_neighbors) {
				out_neighbors[c] = j;
			}
			c += 1;
		}
	}
	*out_count = c;
	return 0;
}

int cluster_dbscan_run(uint32_t dataset_id, const DBSCANParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz)
{
	DatasetRecord *rec;
	DistanceConfig cfg;
	uint32_t n;
	int32_t *labels;
	uint32_t *queue;
	uint32_t *neighbors;
	uint32_t cid = 0;
	uint32_t i;

	if (params == NULL) {
		set_err(err, err_sz, "missing dbscan params");
		return -1;
	}
	if (!gpu_dispatch_available()) {
		set_err(err, err_sz, "HIP backend unavailable");
		return -2;
	}
	if (ensure_chars(dataset_id, params->q, &rec) != 0) {
		set_err(err, err_sz, "character preparation failed");
		return -3;
	}

	n = rec->chars.n_blocks;
	if (n == 0 || params->min_pts == 0) {
		set_err(err, err_sz, "invalid dbscan params");
		return -4;
	}
	trainer_log_info("dbscan start dataset_id=%u n_blocks=%u eps=%u min_pts=%u",
		dataset_id,
		n,
		params->eps,
		params->min_pts);

	cfg.delta = params->delta;
	cfg.mirrored_mode = params->mirrored_mode;
	cfg.weighted = params->weighted;
	cfg.w_upper = params->w_upper;
	cfg.w_lower = params->w_lower;
	cfg.w_body = params->w_body;
	cfg.w_range = params->w_range;

	labels = (int32_t *)malloc((size_t)n * sizeof(int32_t));
	queue = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
	neighbors = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
	if (labels == NULL || queue == NULL || neighbors == NULL) {
		free(labels);
		free(queue);
		free(neighbors);
		set_err(err, err_sz, "allocation failed");
		return -5;
	}
	for (i = 0; i < n; ++i) {
		labels[i] = LABEL_UNVISITED;
	}

	for (i = 0; i < n; ++i) {
		uint32_t nb_count;
		if (labels[i] != LABEL_UNVISITED) {
			continue;
		}
		if (gather_neighbors(&rec->chars, i, params->eps, &cfg, neighbors, n, &nb_count) != 0) {
			free(labels);
			free(queue);
			free(neighbors);
			set_err(err, err_sz, "gpu distance failed");
			return -6;
		}
		if (nb_count < params->min_pts) {
			labels[i] = LABEL_NOISE;
		} else {
			uint32_t qh = 0;
			uint32_t qt = 0;
			labels[i] = (int32_t)cid;
			for (uint32_t k = 0; k < nb_count; ++k) {
				queue[qt++] = neighbors[k];
			}
			while (qh < qt) {
				uint32_t p = queue[qh++];
				uint32_t p_nb_count;
				if (labels[p] == LABEL_NOISE) {
					labels[p] = (int32_t)cid;
				}
				if (labels[p] != LABEL_UNVISITED) {
					continue;
				}
				labels[p] = (int32_t)cid;
				if (gather_neighbors(&rec->chars, p, params->eps, &cfg, neighbors, n, &p_nb_count) != 0) {
					free(labels);
					free(queue);
					free(neighbors);
					set_err(err, err_sz, "gpu distance failed");
					return -7;
				}
				if (p_nb_count >= params->min_pts) {
					for (uint32_t k = 0; k < p_nb_count && qt < n; ++k) {
						queue[qt++] = neighbors[k];
					}
				}
			}
			cid += 1;
		}
		if (progress != NULL) {
			*progress = 0.05f + 0.90f * ((float)(i + 1) / (float)n);
		}
		if (cancel_flag != NULL && *cancel_flag) {
			free(labels);
			free(queue);
			free(neighbors);
			set_err(err, err_sz, "cancelled");
			return -8;
		}
	}

	free(rec->clustering.cluster_ids);
	free(rec->clustering.summaries);
	rec->clustering.cluster_ids = (int32_t *)malloc((size_t)n * sizeof(int32_t));
	rec->clustering.summaries = (ClusterSummary *)malloc((size_t)cid * sizeof(ClusterSummary));
	if (rec->clustering.cluster_ids == NULL || (cid > 0 && rec->clustering.summaries == NULL)) {
		free(labels);
		free(queue);
		free(neighbors);
		set_err(err, err_sz, "result allocation failed");
		return -9;
	}
	for (uint32_t c = 0; c < cid; ++c) {
		rec->clustering.summaries[c].cluster_id = c;
		rec->clustering.summaries[c].size = 0;
		rec->clustering.summaries[c].representative_index = UINT32_MAX;
		rec->clustering.summaries[c].score = 0.0f;
	}
	rec->clustering.noise_count = 0;
	for (i = 0; i < n; ++i) {
		rec->clustering.cluster_ids[i] = labels[i];
		if (labels[i] == LABEL_NOISE) {
			rec->clustering.noise_count += 1;
		} else {
			uint32_t c = (uint32_t)labels[i];
			rec->clustering.summaries[c].size += 1;
			if (rec->clustering.summaries[c].representative_index == UINT32_MAX) {
				rec->clustering.summaries[c].representative_index = i;
			}
		}
	}
	rec->clustering.algorithm_type = JOB_TYPE_DBSCAN;
	rec->clustering.cluster_count = cid;
	trainer_log_info("dbscan complete dataset_id=%u clusters=%u noise=%u",
		dataset_id,
		cid,
		rec->clustering.noise_count);

	free(labels);
	free(queue);
	free(neighbors);
	if (progress != NULL) {
		*progress = 1.0f;
	}
	set_err(err, err_sz, "");
	return 0;
}
