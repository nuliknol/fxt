#include "cluster_kmedoids.h"
#include "core_api.h"
#include "gpu_dispatch.h"
#include "internal_store.h"
#include "logger.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
	if (msg != NULL && msg[0] != '\0') {
		trainer_log_error("kmedoids error: %s", msg);
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

int cluster_kmedoids_run(uint32_t dataset_id, const KMedoidsParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz)
{
	DatasetRecord *rec;
	DistanceConfig cfg;
	uint32_t n;
	uint32_t k;
	uint32_t max_iter;
	uint32_t restarts;
	uint32_t *medoids;
	uint32_t *best_medoids;
	int32_t *assign;
	int32_t *best_assign;
	float best_score = FLT_MAX;
	uint32_t rs;

	if (params == NULL) {
		set_err(err, err_sz, "missing kmedoids params");
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
	if (n == 0 || params->k == 0) {
		set_err(err, err_sz, "invalid k or empty blocks");
		return -4;
	}
	k = (params->k > n) ? n : params->k;
	max_iter = (params->max_iter == 0) ? 20 : params->max_iter;
	restarts = (params->restarts == 0) ? 1 : params->restarts;
	trainer_log_info("kmedoids start dataset_id=%u n_blocks=%u k=%u restarts=%u max_iter=%u",
		dataset_id,
		n,
		k,
		restarts,
		max_iter);

	cfg.delta = params->delta;
	cfg.mirrored_mode = params->mirrored_mode;
	cfg.weighted = params->weighted;
	cfg.w_upper = params->w_upper;
	cfg.w_lower = params->w_lower;
	cfg.w_body = params->w_body;
	cfg.w_range = params->w_range;

	medoids = (uint32_t *)malloc((size_t)k * sizeof(uint32_t));
	best_medoids = (uint32_t *)malloc((size_t)k * sizeof(uint32_t));
	assign = (int32_t *)malloc((size_t)n * sizeof(int32_t));
	best_assign = (int32_t *)malloc((size_t)n * sizeof(int32_t));
	if (medoids == NULL || best_medoids == NULL || assign == NULL || best_assign == NULL) {
		free(medoids);
		free(best_medoids);
		free(assign);
		free(best_assign);
		set_err(err, err_sz, "allocation failed");
		return -5;
	}

	for (rs = 0; rs < restarts; ++rs) {
		uint32_t it;
		unsigned int seed = 1103515245u * (rs + 1u) + 12345u;
		float score = FLT_MAX;

		for (uint32_t c = 0; c < k; ++c) {
			seed = 1103515245u * seed + 12345u;
			medoids[c] = seed % n;
		}

		for (it = 0; it < max_iter; ++it) {
			int changed = 0;
			score = 0.0f;

			for (uint32_t i = 0; i < n; ++i) {
				int32_t best_d = INT32_MAX;
				int32_t best_c = 0;
				for (uint32_t c = 0; c < k; ++c) {
					int32_t d;
					if (distance_pair_gpu(&rec->chars, i, medoids[c], &cfg, &d) != 0) {
						free(medoids);
						free(best_medoids);
						free(assign);
						free(best_assign);
						set_err(err, err_sz, "gpu distance failed");
						return -6;
					}
					if (d < best_d) {
						best_d = d;
						best_c = (int32_t)c;
					}
				}
				assign[i] = best_c;
				score += (float)best_d;
			}

			for (uint32_t c = 0; c < k; ++c) {
				uint32_t best_medoid = medoids[c];
				float best_local = FLT_MAX;
				int has_member = 0;

				for (uint32_t cand = 0; cand < n; ++cand) {
					float local_sum = 0.0f;
					if (assign[cand] != (int32_t)c) {
						continue;
					}
					has_member = 1;
					for (uint32_t j = 0; j < n; ++j) {
						int32_t d;
						if (assign[j] != (int32_t)c) {
							continue;
						}
						if (distance_pair_gpu(&rec->chars, cand, j, &cfg, &d) != 0) {
							free(medoids);
							free(best_medoids);
							free(assign);
							free(best_assign);
							set_err(err, err_sz, "gpu distance failed");
							return -7;
						}
						local_sum += (float)d;
					}
					if (local_sum < best_local) {
						best_local = local_sum;
						best_medoid = cand;
					}
				}
				if (!has_member) {
					best_medoid = c % n;
				}
				if (best_medoid != medoids[c]) {
					medoids[c] = best_medoid;
					changed = 1;
				}
			}

			if (progress != NULL) {
				*progress = 0.05f + 0.90f * ((float)(rs * max_iter + it + 1) / (float)(restarts * max_iter));
			}
			if (cancel_flag != NULL && *cancel_flag) {
				free(medoids);
				free(best_medoids);
				free(assign);
				free(best_assign);
				set_err(err, err_sz, "cancelled");
				return -8;
			}
			if (!changed) {
				break;
			}
		}

		if (score < best_score) {
			best_score = score;
			memcpy(best_assign, assign, (size_t)n * sizeof(int32_t));
			memcpy(best_medoids, medoids, (size_t)k * sizeof(uint32_t));
		}
	}

	free(rec->clustering.cluster_ids);
	free(rec->clustering.summaries);
	rec->clustering.cluster_ids = (int32_t *)malloc((size_t)n * sizeof(int32_t));
	rec->clustering.summaries = (ClusterSummary *)malloc((size_t)k * sizeof(ClusterSummary));
	if (rec->clustering.cluster_ids == NULL || rec->clustering.summaries == NULL) {
		free(medoids);
		free(best_medoids);
		free(assign);
		free(best_assign);
		set_err(err, err_sz, "result allocation failed");
		return -9;
	}
	for (uint32_t c = 0; c < k; ++c) {
		rec->clustering.summaries[c].cluster_id = c;
		rec->clustering.summaries[c].size = 0;
		rec->clustering.summaries[c].representative_index = best_medoids[c];
		rec->clustering.summaries[c].score = best_score;
	}
	for (uint32_t i = 0; i < n; ++i) {
		int32_t cid = best_assign[i];
		rec->clustering.cluster_ids[i] = cid;
		if (cid >= 0 && (uint32_t)cid < k) {
			rec->clustering.summaries[cid].size += 1;
		}
	}
	rec->clustering.algorithm_type = JOB_TYPE_KMEDOIDS;
	rec->clustering.cluster_count = k;
	rec->clustering.noise_count = 0;
	trainer_log_info("kmedoids complete dataset_id=%u clusters=%u score=%.3f",
		dataset_id,
		k,
		best_score);

	free(medoids);
	free(best_medoids);
	free(assign);
	free(best_assign);
	if (progress != NULL) {
		*progress = 1.0f;
	}
	set_err(err, err_sz, "");
	return 0;
}
