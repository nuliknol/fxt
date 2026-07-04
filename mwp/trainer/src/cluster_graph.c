#include "cluster_graph.h"
#include "core_api.h"
#include "gpu_dispatch.h"
#include "internal_store.h"
#include "logger.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAPH_PERCOLATION_FRACTION 0.80f
#define GRAPH_GPU_TILE 1024u
#define LABEL_PROP_MAX_ITER 50

typedef struct {
	uint32_t src;
	uint32_t dst;
	float distance;
	float weight;
} GraphEdge;

typedef struct {
	uint32_t parent;
	uint32_t rank;
} DsuNode;

typedef struct {
	uint32_t orig_index;
	int32_t regime_group;
	int32_t regime_depth;
	int32_t bull_count;
	int32_t bear_count;
	int32_t total_range;
	int32_t gross_body;
	int32_t upper_sum;
	int32_t lower_sum;
} BlockOrderItem;

static const CharMatrix *g_block_order_matrix = NULL;

static void set_err(char *err, size_t err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
	if (msg != NULL && msg[0] != '\0') {
		trainer_log_error("graph clustering error: %s", msg);
	}
}

static uint32_t dsu_find(DsuNode *dsu, uint32_t x)
{
	uint32_t p = dsu[x].parent;

	if (p != x) {
		dsu[x].parent = dsu_find(dsu, p);
	}
	return dsu[x].parent;
}

static void dsu_union(DsuNode *dsu, uint32_t a, uint32_t b)
{
	uint32_t pa = dsu_find(dsu, a);
	uint32_t pb = dsu_find(dsu, b);

	if (pa == pb) {
		return;
	}
	if (dsu[pa].rank < dsu[pb].rank) {
		dsu[pa].parent = pb;
	} else if (dsu[pa].rank > dsu[pb].rank) {
		dsu[pb].parent = pa;
	} else {
		dsu[pb].parent = pa;
		dsu[pa].rank += 1;
	}
}

static int ensure_chars(uint32_t dataset_id, uint32_t q, DatasetRecord **out_rec)
{
	DatasetRecord *rec = store_find_dataset(dataset_id);

	if (rec == NULL) {
		return -1;
	}
	if (rec->chars.data != NULL && rec->chars.q == q) {
		if (rec->chars.summaries == NULL) {
			uint32_t i;

			rec->chars.summaries = (BlockSummary *)malloc((size_t)rec->chars.n_blocks * sizeof(*rec->chars.summaries));
			if (rec->chars.summaries == NULL) {
				return -4;
			}
			for (i = 0; i < rec->chars.n_blocks; ++i) {
				BlockSummary *out = &rec->chars.summaries[i];
				const int32_t *block = &rec->chars.data[(size_t)i * rec->chars.dim];
				uint32_t j;
				int32_t last_sign = 0;
				int32_t bull_run = 0;
				int32_t bear_run = 0;
				int32_t range_sum = 0;

				memset(out, 0, sizeof(*out));
				for (j = 0; j < rec->chars.q; ++j) {
					int32_t upper = block[j * 4u + 0u];
					int32_t lower = block[j * 4u + 1u];
					int32_t body = block[j * 4u + 2u];
					int32_t range = block[j * 4u + 3u];
					int32_t sign = 0;

					if (body > 0) {
						sign = 1;
						out->bull_count += 1;
						bull_run += 1;
						bear_run = 0;
					} else if (body < 0) {
						sign = -1;
						out->bear_count += 1;
						bear_run += 1;
						bull_run = 0;
					} else {
						bull_run = 0;
						bear_run = 0;
					}
					if (sign != 0 && last_sign != 0 && sign != last_sign) {
						out->sign_changes += 1;
					}
					if (sign != 0) {
						last_sign = sign;
					}
					if (bull_run > out->max_bull_run) {
						out->max_bull_run = bull_run;
					}
					if (bear_run > out->max_bear_run) {
						out->max_bear_run = bear_run;
					}
					out->net_body += body;
					out->gross_body += abs(body);
					out->upper_sum += upper;
					out->lower_sum += lower;
					range_sum += range;
				}
				if (rec->chars.q > 0u) {
					out->avg_range = range_sum / (int32_t)rec->chars.q;
				}
			}
		}
		if (out_rec != NULL) {
			*out_rec = rec;
		}
		return 0;
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

static void fill_distance_config_from_graph(const GraphParams *params, DistanceConfig *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->distance_mode = params->distance_mode;
	cfg->mirrored_mode = params->mirrored_mode;
	cfg->weighted = params->weighted;
	cfg->delta = params->delta;
	cfg->block_q = params->q;
	cfg->w_upper = params->w_upper;
	cfg->w_lower = params->w_lower;
	cfg->w_body = params->w_body;
	cfg->w_range = params->w_range;
	cfg->body_sign_penalty = params->body_sign_penalty;
	cfg->w_bullcount = params->w_bullcount;
	cfg->w_signchanges = params->w_signchanges;
	cfg->w_runbull = params->w_runbull;
	cfg->w_runbear = params->w_runbear;
	cfg->gate_body_diff = params->delta_body_gate;
	cfg->gate_range_diff = params->delta_range_gate;
	cfg->gate_bullcount_diff = params->delta_bullcount_gate;
	cfg->gate_signchange_diff = params->delta_signchange_gate;
	cfg->gate_upper_wick_diff = params->delta_upper_wick_gate;
	cfg->gate_lower_wick_diff = params->delta_lower_wick_gate;
	cfg->gate_lower_ratio_diff = params->delta_lower_ratio_gate;
	cfg->gate_upper_ratio_diff = params->delta_upper_ratio_gate;
	cfg->gate_total_wick_ratio_diff = params->delta_total_wick_ratio_gate;
	cfg->body_mode = params->body_mode;
	cfg->require_same_majority_sign = params->require_same_majority_sign;
	cfg->use_upper = params->use_upper;
	cfg->use_lower = params->use_lower;
	cfg->use_range = params->use_range;
	cfg->use_body = params->use_body;
	cfg->use_composition = params->use_composition;
}

static uint64_t count_tile_pairs(uint32_t row_start, uint32_t row_count, uint32_t col_start, uint32_t col_count)
{
	if (row_count == 0u || col_count == 0u) {
		return 0u;
	}
	if (row_start == col_start) {
		return ((uint64_t)row_count * (uint64_t)(row_count - 1u)) / 2u;
	}
	return (uint64_t)row_count * (uint64_t)col_count;
}

static void update_pair_progress(volatile float *progress, uint64_t done_pairs, uint64_t total_pairs)
{
	if (progress != NULL && total_pairs > 0u) {
		*progress = 0.05f + 0.45f * ((float)done_pairs / (float)total_pairs);
	}
}

static int normalize_cluster_labels(uint32_t n_nodes, int32_t *labels, uint32_t *out_cluster_count, uint32_t *cluster_sizes);

static int threshold_regime_group(const BlockSummary *summary, uint32_t q)
{
	if (summary->bear_count == (int32_t)q) {
		return 0;
	}
	if (summary->bull_count == (int32_t)q) {
		return 1;
	}
	if (summary->bull_count > summary->bear_count) {
		return 2;
	}
	if (summary->bear_count > summary->bull_count) {
		return 3;
	}
	return 4;
}

static int threshold_regime_depth(const BlockSummary *summary, uint32_t q)
{
	(void)q;

	if (summary->bull_count > summary->bear_count) {
		return summary->bear_count;
	}
	if (summary->bear_count > summary->bull_count) {
		return summary->bull_count;
	}
	return summary->bull_count + summary->bear_count;
}

static int threshold_composition_match(const BlockSummary *a, const BlockSummary *b, uint32_t q)
{
	int group_a = threshold_regime_group(a, q);
	int group_b = threshold_regime_group(b, q);

	if (group_a != group_b) {
		return 0;
	}
	if (group_a == 0 || group_a == 1) {
		return 1;
	}
	if (group_a == 2) {
		return a->bear_count == b->bear_count;
	}
	if (group_a == 3) {
		return a->bull_count == b->bull_count;
	}
	return a->bull_count == b->bull_count && a->bear_count == b->bear_count;
}

static int block_order_compare(const void *lhs, const void *rhs)
{
	const BlockOrderItem *a = (const BlockOrderItem *)lhs;
	const BlockOrderItem *b = (const BlockOrderItem *)rhs;
	const int32_t *av;
	const int32_t *bv;
	uint32_t i;

	if (a->regime_group != b->regime_group) {
		return (a->regime_group < b->regime_group) ? -1 : 1;
	}
	if (a->regime_depth != b->regime_depth) {
		return (a->regime_depth < b->regime_depth) ? -1 : 1;
	}
	if (a->bear_count != b->bear_count) {
		return (a->bear_count < b->bear_count) ? -1 : 1;
	}
	if (a->bull_count != b->bull_count) {
		return (a->bull_count < b->bull_count) ? -1 : 1;
	}
	if (a->total_range != b->total_range) {
		return (a->total_range < b->total_range) ? -1 : 1;
	}
	if (a->gross_body != b->gross_body) {
		return (a->gross_body < b->gross_body) ? -1 : 1;
	}
	if (a->upper_sum != b->upper_sum) {
		return (a->upper_sum < b->upper_sum) ? -1 : 1;
	}
	if (a->lower_sum != b->lower_sum) {
		return (a->lower_sum < b->lower_sum) ? -1 : 1;
	}
	if (g_block_order_matrix == NULL || g_block_order_matrix->data == NULL) {
		return 0;
	}
	av = &g_block_order_matrix->data[(size_t)a->orig_index * g_block_order_matrix->dim];
	bv = &g_block_order_matrix->data[(size_t)b->orig_index * g_block_order_matrix->dim];
	for (i = 0; i < g_block_order_matrix->dim; ++i) {
		if (av[i] != bv[i]) {
			return (av[i] < bv[i]) ? -1 : 1;
		}
	}
	return 0;
}

static int build_canonical_threshold_matrix(const CharMatrix *src,
	CharMatrix *out_sorted,
	uint32_t **out_sorted_to_orig,
	char *err,
	size_t err_sz)
{
	BlockOrderItem *items = NULL;
	uint32_t *sorted_to_orig = NULL;
	int32_t *sorted_data = NULL;
	BlockSummary *sorted_summaries = NULL;
	uint32_t i;

	memset(out_sorted, 0, sizeof(*out_sorted));
	if (src == NULL || src->data == NULL || src->summaries == NULL || out_sorted == NULL || out_sorted_to_orig == NULL) {
		set_err(err, err_sz, "invalid canonical threshold matrix input");
		return -1;
	}

	items = (BlockOrderItem *)malloc((size_t)src->n_blocks * sizeof(*items));
	sorted_to_orig = (uint32_t *)malloc((size_t)src->n_blocks * sizeof(*sorted_to_orig));
	sorted_data = (int32_t *)malloc((size_t)src->n_blocks * src->dim * sizeof(*sorted_data));
	sorted_summaries = (BlockSummary *)malloc((size_t)src->n_blocks * sizeof(*sorted_summaries));
	if (items == NULL || sorted_to_orig == NULL || sorted_data == NULL || sorted_summaries == NULL) {
		free(items);
		free(sorted_to_orig);
		free(sorted_data);
		free(sorted_summaries);
		set_err(err, err_sz, "canonical threshold allocation failed");
		return -2;
	}

	for (i = 0; i < src->n_blocks; ++i) {
		const int32_t *block = &src->data[(size_t)i * src->dim];
		const BlockSummary *summary = &src->summaries[i];
		uint32_t j;
		int32_t total_range = 0;
		int32_t upper_sum = 0;
		int32_t lower_sum = 0;

		items[i].orig_index = i;
		items[i].bull_count = summary->bull_count;
		items[i].bear_count = summary->bear_count;
		items[i].gross_body = summary->gross_body;
		items[i].regime_group = threshold_regime_group(summary, src->q);
		items[i].regime_depth = threshold_regime_depth(summary, src->q);
		for (j = 0; j < src->q; ++j) {
			upper_sum += block[j * 4u + 0u];
			lower_sum += block[j * 4u + 1u];
			total_range += block[j * 4u + 3u];
		}
		items[i].total_range = total_range;
		items[i].upper_sum = upper_sum;
		items[i].lower_sum = lower_sum;
	}

	g_block_order_matrix = src;
	qsort(items, (size_t)src->n_blocks, sizeof(*items), block_order_compare);
	g_block_order_matrix = NULL;

	for (i = 0; i < src->n_blocks; ++i) {
		uint32_t orig = items[i].orig_index;

		sorted_to_orig[i] = orig;
		memcpy(&sorted_data[(size_t)i * src->dim], &src->data[(size_t)orig * src->dim], (size_t)src->dim * sizeof(*sorted_data));
		sorted_summaries[i] = src->summaries[orig];
	}

	out_sorted->n_blocks = src->n_blocks;
	out_sorted->q = src->q;
	out_sorted->dim = src->dim;
	out_sorted->data = sorted_data;
	out_sorted->summaries = sorted_summaries;
	*out_sorted_to_orig = sorted_to_orig;
	free(items);
	return 0;
}

static void knn_init(uint32_t *indices, float *distances, uint32_t n_nodes, uint32_t k)
{
	uint32_t i;

	for (i = 0; i < n_nodes * k; ++i) {
		indices[i] = UINT32_MAX;
		distances[i] = FLT_MAX;
	}
}

static void knn_try_insert(uint32_t *indices, float *distances, uint32_t base, uint32_t k, uint32_t candidate, float distance)
{
	uint32_t empty_slot = UINT32_MAX;
	uint32_t worst_slot = 0;
	uint32_t slot;

	for (slot = 0; slot < k; ++slot) {
		uint32_t idx = base + slot;

		if (indices[idx] == candidate) {
			if (distance < distances[idx]) {
				distances[idx] = distance;
			}
			return;
		}
		if (indices[idx] == UINT32_MAX) {
			if (empty_slot == UINT32_MAX) {
				empty_slot = slot;
			}
			continue;
		}
		if (distances[idx] > distances[base + worst_slot]) {
			worst_slot = slot;
		}
	}
	if (empty_slot != UINT32_MAX) {
		indices[base + empty_slot] = candidate;
		distances[base + empty_slot] = distance;
		return;
	}
	if (distance < distances[base + worst_slot]) {
		indices[base + worst_slot] = candidate;
		distances[base + worst_slot] = distance;
	}
}

static int knn_contains(uint32_t *indices, uint32_t base, uint32_t k, uint32_t candidate)
{
	uint32_t slot;

	for (slot = 0; slot < k; ++slot) {
		if (indices[base + slot] == candidate) {
			return 1;
		}
	}
	return 0;
}

static float knn_distance_for(uint32_t *indices, float *distances, uint32_t base, uint32_t k, uint32_t candidate)
{
	uint32_t slot;

	for (slot = 0; slot < k; ++slot) {
		if (indices[base + slot] == candidate) {
			return distances[base + slot];
		}
	}
	return FLT_MAX;
}

static int graph_edge_push(GraphEdge **edges, uint32_t *count, uint32_t *capacity, uint32_t src, uint32_t dst, float distance)
{
	GraphEdge *grown;

	if (*count >= *capacity) {
		uint32_t new_cap = (*capacity == 0) ? 256u : (*capacity * 2u);

		grown = (GraphEdge *)realloc(*edges, (size_t)new_cap * sizeof(**edges));
		if (grown == NULL) {
			return -1;
		}
		*edges = grown;
		*capacity = new_cap;
	}
	(*edges)[*count].src = src;
	(*edges)[*count].dst = dst;
	(*edges)[*count].distance = distance;
	(*edges)[*count].weight = 1.0f / (1.0f + distance);
	*count += 1u;
	return 0;
}

static void histogram_add(HistogramSummary *hist, uint32_t value)
{
	if (value == 0u) {
		hist->bucket_0 += 1u;
		return;
	}
	if (value == 1u) {
		hist->bucket_1 += 1u;
		return;
	}
	if (value <= 3u) {
		hist->bucket_2_3 += 1u;
		return;
	}
	if (value <= 7u) {
		hist->bucket_4_7 += 1u;
		return;
	}
	if (value <= 15u) {
		hist->bucket_8_15 += 1u;
		return;
	}
	hist->bucket_16_plus += 1u;
}

static void compute_raw_graph_diagnostics(uint32_t n_nodes, const GraphEdge *edges, uint32_t edge_count, GraphDiagnostics *out_diag)
{
	DsuNode *dsu;
	uint32_t *component_sizes;
	uint32_t *degrees;
	uint32_t i;
	uint32_t largest = 0;
	uint32_t component_count = 0;

	memset(out_diag, 0, sizeof(*out_diag));
	out_diag->n_nodes = n_nodes;
	out_diag->n_edges = edge_count;
	if (n_nodes == 0u) {
		return;
	}

	dsu = (DsuNode *)malloc((size_t)n_nodes * sizeof(*dsu));
	component_sizes = (uint32_t *)calloc((size_t)n_nodes, sizeof(*component_sizes));
	degrees = (uint32_t *)calloc((size_t)n_nodes, sizeof(*degrees));
	if (dsu == NULL || component_sizes == NULL || degrees == NULL) {
		free(dsu);
		free(component_sizes);
		free(degrees);
		return;
	}

	for (i = 0; i < n_nodes; ++i) {
		dsu[i].parent = i;
		dsu[i].rank = 0;
	}
	for (i = 0; i < edge_count; ++i) {
		dsu_union(dsu, edges[i].src, edges[i].dst);
		degrees[edges[i].src] += 1u;
		degrees[edges[i].dst] += 1u;
	}
	for (i = 0; i < n_nodes; ++i) {
		uint32_t root = dsu_find(dsu, i);

		component_sizes[root] += 1u;
		histogram_add(&out_diag->degree_hist, degrees[i]);
	}
	for (i = 0; i < n_nodes; ++i) {
		if (component_sizes[i] == 0u) {
			continue;
		}
		component_count += 1u;
		if (component_sizes[i] > largest) {
			largest = component_sizes[i];
		}
	}

	out_diag->average_degree = (2.0f * (float)edge_count) / (float)n_nodes;
	out_diag->raw_component_count = component_count;
	out_diag->largest_component_size = largest;
	out_diag->largest_component_fraction = (float)largest / (float)n_nodes;
	out_diag->percolated = (out_diag->largest_component_fraction >= GRAPH_PERCOLATION_FRACTION) ? 1 : 0;

	free(dsu);
	free(component_sizes);
	free(degrees);
}

static void compute_streamed_graph_diagnostics(uint32_t n_nodes,
	const DsuNode *dsu_state,
	const uint32_t *degrees,
	uint64_t edge_count,
	GraphDiagnostics *out_diag)
{
	DsuNode *dsu;
	uint32_t *component_sizes;
	uint32_t i;
	uint32_t largest = 0;
	uint32_t component_count = 0;

	memset(out_diag, 0, sizeof(*out_diag));
	out_diag->n_nodes = n_nodes;
	out_diag->n_edges = (edge_count > UINT32_MAX) ? UINT32_MAX : (uint32_t)edge_count;
	if (n_nodes == 0u || dsu_state == NULL || degrees == NULL) {
		return;
	}

	dsu = (DsuNode *)malloc((size_t)n_nodes * sizeof(*dsu));
	component_sizes = (uint32_t *)calloc((size_t)n_nodes, sizeof(*component_sizes));
	if (dsu == NULL || component_sizes == NULL) {
		free(dsu);
		free(component_sizes);
		return;
	}
	memcpy(dsu, dsu_state, (size_t)n_nodes * sizeof(*dsu));
	for (i = 0; i < n_nodes; ++i) {
		uint32_t root = dsu_find(dsu, i);

		component_sizes[root] += 1u;
		histogram_add(&out_diag->degree_hist, degrees[i]);
	}
	for (i = 0; i < n_nodes; ++i) {
		if (component_sizes[i] == 0u) {
			continue;
		}
		component_count += 1u;
		if (component_sizes[i] > largest) {
			largest = component_sizes[i];
		}
	}
	out_diag->average_degree = (2.0f * (float)edge_count) / (float)n_nodes;
	out_diag->raw_component_count = component_count;
	out_diag->largest_component_size = largest;
	out_diag->largest_component_fraction = (float)largest / (float)n_nodes;
	out_diag->percolated = (out_diag->largest_component_fraction >= GRAPH_PERCOLATION_FRACTION) ? 1 : 0;

	free(dsu);
	free(component_sizes);
}

static int build_sparse_knn_graph(const CharMatrix *m,
	const GraphParams *params,
	volatile float *progress,
	volatile int *cancel_flag,
	GraphEdge **out_edges,
	uint32_t *out_edge_count,
	GraphDiagnostics *out_diag,
	char *err,
	size_t err_sz)
{
	uint32_t n = m->n_blocks;
	uint32_t k = params->k_neighbors;
	uint32_t *knn_indices;
	float *knn_distances;
	GraphEdge *edges = NULL;
	uint32_t edge_count = 0;
	uint32_t edge_capacity = 0;
	DistanceConfig cfg;
	int32_t *tile_distances;
	uint64_t total_pairs;
	uint64_t done_pairs = 0;
	uint32_t row_start;
	uint32_t col_start;
	uint32_t i;
	uint32_t j;

	knn_indices = (uint32_t *)malloc((size_t)n * k * sizeof(*knn_indices));
	knn_distances = (float *)malloc((size_t)n * k * sizeof(*knn_distances));
	tile_distances = (int32_t *)malloc((size_t)GRAPH_GPU_TILE * GRAPH_GPU_TILE * sizeof(*tile_distances));
	if (knn_indices == NULL || knn_distances == NULL || tile_distances == NULL) {
		free(knn_indices);
		free(knn_distances);
		free(tile_distances);
		set_err(err, err_sz, "knn allocation failed");
		return -1;
	}
	knn_init(knn_indices, knn_distances, n, k);
	fill_distance_config_from_graph(params, &cfg);

	total_pairs = ((uint64_t)n * (uint64_t)(n - 1u)) / 2u;
	for (row_start = 0; row_start < n; row_start += GRAPH_GPU_TILE) {
		uint32_t row_count = ((n - row_start) < GRAPH_GPU_TILE) ? (n - row_start) : GRAPH_GPU_TILE;

		for (col_start = row_start; col_start < n; col_start += GRAPH_GPU_TILE) {
			uint32_t col_count = ((n - col_start) < GRAPH_GPU_TILE) ? (n - col_start) : GRAPH_GPU_TILE;
			uint32_t r;

			if (cancel_flag != NULL && *cancel_flag) {
				free(knn_indices);
				free(knn_distances);
				free(tile_distances);
				free(edges);
				set_err(err, err_sz, "cancelled");
				return -2;
			}
			if (gpu_distance_tile(m, row_start, row_count, col_start, col_count, &cfg, tile_distances) != 0) {
				free(knn_indices);
				free(knn_distances);
				free(tile_distances);
				free(edges);
				set_err(err, err_sz, "GPU not available");
				return -3;
			}
			for (r = 0; r < row_count; ++r) {
				uint32_t i = row_start + r;
				uint32_t c_start = (row_start == col_start) ? (r + 1u) : 0u;
				uint32_t c;

				for (c = c_start; c < col_count; ++c) {
					uint32_t j = col_start + c;
					int32_t distance_i32 = tile_distances[(size_t)r * col_count + c];
					float distance;

					if (distance_i32 == INT32_MAX) {
						continue;
					}
					distance = (float)distance_i32;
					if (params->max_neighbor_distance > 0.0f && distance > params->max_neighbor_distance) {
						continue;
					}
					knn_try_insert(knn_indices, knn_distances, i * k, k, j, distance);
					knn_try_insert(knn_indices, knn_distances, j * k, k, i, distance);
				}
			}
			done_pairs += count_tile_pairs(row_start, row_count, col_start, col_count);
			update_pair_progress(progress, done_pairs, total_pairs);
		}
	}

	if (params->use_mutual_knn) {
		for (i = 0; i < n; ++i) {
			uint32_t slot;

			for (slot = 0; slot < k; ++slot) {
				uint32_t idx = knn_indices[i * k + slot];
				float distance;

				if (idx == UINT32_MAX || idx <= i) {
					continue;
				}
				if (!knn_contains(knn_indices, idx * k, k, i)) {
					continue;
				}
				distance = knn_distance_for(knn_indices, knn_distances, i * k, k, idx);
				if (graph_edge_push(&edges, &edge_count, &edge_capacity, i, idx, distance) != 0) {
					free(knn_indices);
					free(knn_distances);
					free(edges);
					set_err(err, err_sz, "edge allocation failed");
					return -4;
				}
			}
		}
	} else {
		for (i = 0; i < n; ++i) {
			for (j = i + 1u; j < n; ++j) {
				int in_ij = knn_contains(knn_indices, i * k, k, j);
				int in_ji = knn_contains(knn_indices, j * k, k, i);
				float distance;

				if (!in_ij && !in_ji) {
					continue;
				}
				distance = in_ij ? knn_distance_for(knn_indices, knn_distances, i * k, k, j) : FLT_MAX;
				if (in_ji) {
					float reverse = knn_distance_for(knn_indices, knn_distances, j * k, k, i);

					if (reverse < distance) {
						distance = reverse;
					}
				}
				if (graph_edge_push(&edges, &edge_count, &edge_capacity, i, j, distance) != 0) {
					free(knn_indices);
					free(knn_distances);
					free(edges);
					set_err(err, err_sz, "edge allocation failed");
					return -4;
				}
			}
		}
	}

	compute_raw_graph_diagnostics(n, edges, edge_count, out_diag);

	free(knn_indices);
	free(knn_distances);
	free(tile_distances);
	*out_edges = edges;
	*out_edge_count = edge_count;
	return 0;
}

static int build_legacy_threshold_graph(const CharMatrix *m,
	const GraphParams *params,
	volatile float *progress,
	volatile int *cancel_flag,
	int32_t **out_labels,
	uint32_t *out_cluster_count,
	float **out_node_strength,
	float **out_cluster_scores,
	GraphDiagnostics *out_diag,
	char *err,
	size_t err_sz)
{
	CharMatrix sorted_m;
	uint32_t *sorted_to_orig = NULL;
	DistanceConfig cfg;
	DsuNode *dsu = NULL;
	uint32_t *degrees = NULL;
	float *node_strength = NULL;
	int32_t *labels = NULL;
	float *cluster_scores = NULL;
	float *cluster_weight_sum = NULL;
	uint32_t *cluster_edge_count = NULL;
	uint32_t *cluster_sizes = NULL;
	uint32_t *compatible_count = NULL;
	uint32_t *touched_clusters = NULL;
	uint32_t *matched_nodes = NULL;
	float *matched_weights = NULL;
	int32_t *tile_distances = NULL;
	uint32_t n;
	uint32_t i;
	uint32_t cluster_count;
	uint64_t edge_count = 0;
	uint64_t total_pairs;
	uint64_t done_pairs = 0;

	if (build_canonical_threshold_matrix(m, &sorted_m, &sorted_to_orig, err, err_sz) != 0) {
		return -1;
	}
	n = sorted_m.n_blocks;
	total_pairs = ((uint64_t)n * (uint64_t)(n - 1u)) / 2u;
	fill_distance_config_from_graph(params, &cfg);
	dsu = (DsuNode *)malloc((size_t)n * sizeof(*dsu));
	degrees = (uint32_t *)calloc((size_t)n, sizeof(*degrees));
	node_strength = (float *)calloc((size_t)n, sizeof(*node_strength));
	labels = (int32_t *)malloc((size_t)n * sizeof(*labels));
	cluster_scores = (float *)calloc((size_t)n, sizeof(*cluster_scores));
	cluster_weight_sum = (float *)calloc((size_t)n, sizeof(*cluster_weight_sum));
	cluster_edge_count = (uint32_t *)calloc((size_t)n, sizeof(*cluster_edge_count));
	cluster_sizes = (uint32_t *)malloc((size_t)n * sizeof(*cluster_sizes));
	compatible_count = (uint32_t *)calloc((size_t)n, sizeof(*compatible_count));
	touched_clusters = (uint32_t *)malloc((size_t)n * sizeof(*touched_clusters));
	matched_nodes = (uint32_t *)malloc((size_t)n * sizeof(*matched_nodes));
	matched_weights = (float *)malloc((size_t)n * sizeof(*matched_weights));
	tile_distances = (int32_t *)malloc((size_t)GRAPH_GPU_TILE * GRAPH_GPU_TILE * sizeof(*tile_distances));
	if (dsu == NULL || degrees == NULL || node_strength == NULL || labels == NULL || cluster_scores == NULL ||
		cluster_weight_sum == NULL || cluster_edge_count == NULL || cluster_sizes == NULL || compatible_count == NULL ||
		touched_clusters == NULL || matched_nodes == NULL || matched_weights == NULL || tile_distances == NULL) {
		free(dsu);
		free(degrees);
		free(node_strength);
		free(labels);
		free(cluster_scores);
		free(cluster_weight_sum);
		free(cluster_edge_count);
		free(cluster_sizes);
		free(compatible_count);
		free(touched_clusters);
		free(matched_nodes);
		free(matched_weights);
		free(tile_distances);
		free(sorted_to_orig);
		free(sorted_m.data);
		free(sorted_m.summaries);
		set_err(err, err_sz, "legacy threshold allocation failed");
		return -1;
	}
	for (i = 0; i < n; ++i) {
		dsu[i].parent = i;
		dsu[i].rank = 0;
		cluster_sizes[i] = 0u;
	}
	if (n == 0u) {
		cluster_count = 0u;
	} else {
		labels[0] = 0;
		cluster_sizes[0] = 1u;
		cluster_count = 1u;
	}

	for (i = 1u; i < n; ++i) {
		uint32_t col_start;
		uint32_t matched_count = 0u;
		uint32_t touched_count = 0u;
		uint32_t chosen_cluster = UINT32_MAX;
		uint32_t t;

		if (cancel_flag != NULL && *cancel_flag) {
			free(dsu);
			free(degrees);
			free(node_strength);
			free(labels);
			free(cluster_scores);
			free(cluster_weight_sum);
			free(cluster_edge_count);
			free(cluster_sizes);
			free(compatible_count);
			free(touched_clusters);
			free(matched_nodes);
			free(matched_weights);
			free(tile_distances);
			free(sorted_to_orig);
			free(sorted_m.data);
			free(sorted_m.summaries);
			set_err(err, err_sz, "cancelled");
			return -2;
		}

		for (col_start = 0u; col_start < i; col_start += GRAPH_GPU_TILE) {
			uint32_t col_count = (((i - col_start) < GRAPH_GPU_TILE) ? (i - col_start) : GRAPH_GPU_TILE);
			uint32_t c;

			if (gpu_distance_tile(&sorted_m, i, 1u, col_start, col_count, &cfg, tile_distances) != 0) {
				free(dsu);
				free(degrees);
				free(node_strength);
				free(labels);
				free(cluster_scores);
				free(cluster_weight_sum);
				free(cluster_edge_count);
				free(cluster_sizes);
				free(compatible_count);
				free(touched_clusters);
				free(matched_nodes);
				free(matched_weights);
				free(tile_distances);
				free(sorted_to_orig);
				free(sorted_m.data);
				free(sorted_m.summaries);
				set_err(err, err_sz, "GPU not available");
				return -3;
			}
			for (c = 0u; c < col_count; ++c) {
				uint32_t j = col_start + c;
				int32_t distance_i32 = tile_distances[c];
				uint32_t cid;

				if (!threshold_composition_match(&sorted_m.summaries[i], &sorted_m.summaries[j], sorted_m.q)) {
					continue;
				}
				if (distance_i32 == INT32_MAX || (uint32_t)distance_i32 > params->theta) {
					continue;
				}
				dsu_union(dsu, i, j);
				degrees[i] += 1u;
				degrees[j] += 1u;
				edge_count += 1u;

				cid = (uint32_t)labels[j];
				if (compatible_count[cid] == 0u) {
					touched_clusters[touched_count++] = cid;
				}
				compatible_count[cid] += 1u;
				matched_nodes[matched_count] = j;
				matched_weights[matched_count] = 1.0f / (1.0f + (float)distance_i32);
				matched_count += 1u;
			}
		}

		for (t = 0u; t < touched_count; ++t) {
			uint32_t cid = touched_clusters[t];

			if (compatible_count[cid] == cluster_sizes[cid]) {
				if (chosen_cluster == UINT32_MAX || cid < chosen_cluster) {
					chosen_cluster = cid;
				}
			}
		}
		if (chosen_cluster == UINT32_MAX) {
			chosen_cluster = cluster_count;
			cluster_count += 1u;
		}

		labels[i] = (int32_t)chosen_cluster;
		cluster_sizes[chosen_cluster] += 1u;

		for (t = 0u; t < matched_count; ++t) {
			uint32_t j = matched_nodes[t];
			float weight = matched_weights[t];

			if ((uint32_t)labels[j] != chosen_cluster) {
				continue;
			}
			node_strength[i] += weight;
			node_strength[j] += weight;
			cluster_weight_sum[chosen_cluster] += weight;
			cluster_edge_count[chosen_cluster] += 1u;
		}

		for (t = 0u; t < touched_count; ++t) {
			compatible_count[touched_clusters[t]] = 0u;
		}

		done_pairs += (uint64_t)i;
		update_pair_progress(progress, done_pairs, total_pairs);
	}

	compute_streamed_graph_diagnostics(n, dsu, degrees, edge_count, out_diag);

	for (i = 0; i < cluster_count; ++i) {
		if (cluster_edge_count[i] > 0u) {
			cluster_scores[i] = cluster_weight_sum[i] / (float)cluster_edge_count[i];
		}
	}

	{
		int32_t *orig_labels = (int32_t *)malloc((size_t)n * sizeof(*orig_labels));
		float *orig_node_strength = (float *)calloc((size_t)n, sizeof(*orig_node_strength));
		uint32_t *orig_degrees = (uint32_t *)calloc((size_t)n, sizeof(*orig_degrees));
		uint32_t s;

		if (orig_labels == NULL || orig_node_strength == NULL || orig_degrees == NULL) {
			free(orig_labels);
			free(orig_node_strength);
			free(orig_degrees);
			free(dsu);
			free(degrees);
			free(node_strength);
			free(labels);
			free(cluster_scores);
			free(cluster_weight_sum);
			free(cluster_edge_count);
			free(cluster_sizes);
			free(compatible_count);
			free(touched_clusters);
			free(matched_nodes);
			free(matched_weights);
			free(tile_distances);
			free(sorted_to_orig);
			free(sorted_m.data);
			free(sorted_m.summaries);
			set_err(err, err_sz, "legacy threshold reorder allocation failed");
			return -4;
		}
		for (s = 0u; s < n; ++s) {
			uint32_t orig = sorted_to_orig[s];

			orig_labels[orig] = labels[s];
			orig_node_strength[orig] = node_strength[s];
			orig_degrees[orig] = degrees[s];
		}
		memcpy(labels, orig_labels, (size_t)n * sizeof(*labels));
		memcpy(node_strength, orig_node_strength, (size_t)n * sizeof(*node_strength));
		memcpy(degrees, orig_degrees, (size_t)n * sizeof(*degrees));
		free(orig_labels);
		free(orig_node_strength);
		free(orig_degrees);
	}

	free(dsu);
	free(degrees);
	free(cluster_sizes);
	free(compatible_count);
	free(touched_clusters);
	free(matched_nodes);
	free(matched_weights);
	free(tile_distances);
	free(sorted_to_orig);
	free(sorted_m.data);
	free(sorted_m.summaries);
	free(cluster_weight_sum);
	free(cluster_edge_count);
	*out_labels = labels;
	*out_cluster_count = cluster_count;
	*out_node_strength = node_strength;
	*out_cluster_scores = cluster_scores;
	return 0;
}

static int normalize_cluster_labels(uint32_t n_nodes, int32_t *labels, uint32_t *out_cluster_count, uint32_t *cluster_sizes)
{
	uint32_t *map;
	uint32_t i;
	uint32_t cluster_count = 0;

	map = (uint32_t *)malloc((size_t)n_nodes * sizeof(*map));
	if (map == NULL) {
		return -1;
	}
	for (i = 0; i < n_nodes; ++i) {
		map[i] = UINT32_MAX;
		cluster_sizes[i] = 0u;
	}
	for (i = 0; i < n_nodes; ++i) {
		uint32_t raw = (uint32_t)labels[i];

		if (raw >= n_nodes) {
			free(map);
			return -2;
		}
		if (map[raw] == UINT32_MAX) {
			map[raw] = cluster_count;
			cluster_count += 1u;
		}
		labels[i] = (int32_t)map[raw];
		cluster_sizes[(uint32_t)labels[i]] += 1u;
	}
	*out_cluster_count = cluster_count;
	free(map);
	return 0;
}

static int cluster_connected_components(uint32_t n_nodes, const GraphEdge *edges, uint32_t edge_count, int32_t *labels, uint32_t *out_cluster_count)
{
	DsuNode *dsu;
	uint32_t *cluster_sizes;
	uint32_t i;
	int rc;

	dsu = (DsuNode *)malloc((size_t)n_nodes * sizeof(*dsu));
	cluster_sizes = (uint32_t *)malloc((size_t)n_nodes * sizeof(*cluster_sizes));
	if (dsu == NULL || cluster_sizes == NULL) {
		free(dsu);
		free(cluster_sizes);
		return -1;
	}
	for (i = 0; i < n_nodes; ++i) {
		dsu[i].parent = i;
		dsu[i].rank = 0;
	}
	for (i = 0; i < edge_count; ++i) {
		dsu_union(dsu, edges[i].src, edges[i].dst);
	}
	for (i = 0; i < n_nodes; ++i) {
		labels[i] = (int32_t)dsu_find(dsu, i);
	}
	rc = normalize_cluster_labels(n_nodes, labels, out_cluster_count, cluster_sizes);
	free(dsu);
	free(cluster_sizes);
	return rc;
}

static int build_adjacency(uint32_t n_nodes, const GraphEdge *edges, uint32_t edge_count, uint32_t **out_offsets, uint32_t **out_neighbors, float **out_weights)
{
	uint32_t *offsets;
	uint32_t *neighbors;
	float *weights;
	uint32_t *cursor;
	uint32_t i;

	offsets = (uint32_t *)calloc((size_t)n_nodes + 1u, sizeof(*offsets));
	neighbors = (uint32_t *)malloc((size_t)edge_count * 2u * sizeof(*neighbors));
	weights = (float *)malloc((size_t)edge_count * 2u * sizeof(*weights));
	cursor = (uint32_t *)calloc((size_t)n_nodes, sizeof(*cursor));
	if (offsets == NULL || neighbors == NULL || weights == NULL || cursor == NULL) {
		free(offsets);
		free(neighbors);
		free(weights);
		free(cursor);
		return -1;
	}
	for (i = 0; i < edge_count; ++i) {
		offsets[edges[i].src + 1u] += 1u;
		offsets[edges[i].dst + 1u] += 1u;
	}
	for (i = 1; i <= n_nodes; ++i) {
		offsets[i] += offsets[i - 1u];
	}
	memcpy(cursor, offsets, (size_t)n_nodes * sizeof(*cursor));
	for (i = 0; i < edge_count; ++i) {
		uint32_t a = edges[i].src;
		uint32_t b = edges[i].dst;
		uint32_t pos_a = cursor[a]++;
		uint32_t pos_b = cursor[b]++;

		neighbors[pos_a] = b;
		weights[pos_a] = edges[i].weight;
		neighbors[pos_b] = a;
		weights[pos_b] = edges[i].weight;
	}
	free(cursor);
	*out_offsets = offsets;
	*out_neighbors = neighbors;
	*out_weights = weights;
	return 0;
}

static int cluster_weighted_label_propagation(uint32_t n_nodes, const GraphEdge *edges, uint32_t edge_count, int32_t *labels, uint32_t *out_cluster_count)
{
	uint32_t *offsets;
	uint32_t *neighbors;
	float *weights;
	uint32_t max_degree = 0;
	uint32_t *cluster_sizes;
	int32_t *candidate_labels;
	float *candidate_scores;
	uint32_t i;
	uint32_t iter;
	int rc;

	if (build_adjacency(n_nodes, edges, edge_count, &offsets, &neighbors, &weights) != 0) {
		return -1;
	}
	for (i = 0; i < n_nodes; ++i) {
		uint32_t degree = offsets[i + 1u] - offsets[i];

		labels[i] = (int32_t)i;
		if (degree > max_degree) {
			max_degree = degree;
		}
	}
	candidate_labels = (int32_t *)malloc((size_t)(max_degree == 0u ? 1u : max_degree) * sizeof(*candidate_labels));
	candidate_scores = (float *)malloc((size_t)(max_degree == 0u ? 1u : max_degree) * sizeof(*candidate_scores));
	cluster_sizes = (uint32_t *)malloc((size_t)n_nodes * sizeof(*cluster_sizes));
	if (candidate_labels == NULL || candidate_scores == NULL || cluster_sizes == NULL) {
		free(offsets);
		free(neighbors);
		free(weights);
		free(candidate_labels);
		free(candidate_scores);
		free(cluster_sizes);
		return -2;
	}

	for (iter = 0; iter < LABEL_PROP_MAX_ITER; ++iter) {
		int changed = 0;

		for (i = 0; i < n_nodes; ++i) {
			uint32_t begin = offsets[i];
			uint32_t end = offsets[i + 1u];
			uint32_t candidate_count = 0;
			int32_t best_label = labels[i];
			float best_score = -1.0f;
			uint32_t pos;

			if (begin == end) {
				continue;
			}
			for (pos = begin; pos < end; ++pos) {
				int32_t label = labels[neighbors[pos]];
				float weight = weights[pos];
				uint32_t c;

				for (c = 0; c < candidate_count; ++c) {
					if (candidate_labels[c] == label) {
						candidate_scores[c] += weight;
						break;
					}
				}
				if (c == candidate_count) {
					candidate_labels[candidate_count] = label;
					candidate_scores[candidate_count] = weight;
					candidate_count += 1u;
				}
			}
			for (pos = 0; pos < candidate_count; ++pos) {
				float score = candidate_scores[pos];
				int32_t label = candidate_labels[pos];

				if (score > best_score || (score == best_score && label < best_label)) {
					best_score = score;
					best_label = label;
				}
			}
			if (best_label != labels[i]) {
				labels[i] = best_label;
				changed = 1;
			}
		}
		if (!changed) {
			break;
		}
	}

	rc = normalize_cluster_labels(n_nodes, labels, out_cluster_count, cluster_sizes);

	free(offsets);
	free(neighbors);
	free(weights);
	free(candidate_labels);
	free(candidate_scores);
	free(cluster_sizes);
	return rc;
}

static int cluster_sparse_graph(uint32_t n_nodes,
	const GraphEdge *edges,
	uint32_t edge_count,
	uint32_t cluster_mode,
	int32_t *labels,
	uint32_t *out_cluster_count)
{
	if (cluster_mode == GRAPH_CLUSTER_MODE_CONNECTED_COMPONENTS || cluster_mode == GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS) {
		return cluster_connected_components(n_nodes, edges, edge_count, labels, out_cluster_count);
	}
	if (cluster_mode == GRAPH_CLUSTER_MODE_LOUVAIN) {
		trainer_log_info("graph clustering mode=louvain requested; falling back to weighted label propagation");
	}
	return cluster_weighted_label_propagation(n_nodes, edges, edge_count, labels, out_cluster_count);
}

static void finalize_cluster_histogram(GraphDiagnostics *diag, uint32_t cluster_count, const uint32_t *cluster_sizes)
{
	uint32_t i;

	for (i = 0; i < cluster_count; ++i) {
		histogram_add(&diag->cluster_size_hist, cluster_sizes[i]);
	}
}

static int store_cluster_output(DatasetRecord *rec,
	uint32_t n_nodes,
	const GraphEdge *edges,
	uint32_t edge_count,
	int32_t *labels,
	uint32_t cluster_count,
	const GraphDiagnostics *diag,
	uint32_t cluster_mode,
	char *err,
	size_t err_sz)
{
	uint32_t *cluster_sizes;
	float *cluster_weight_sum;
	uint32_t *cluster_edge_count;
	float *node_strength;
	int32_t *new_cluster_ids;
	ClusterSummary *new_summaries;
	int32_t *old_cluster_ids;
	ClusterSummary *old_summaries;
	uint32_t i;

	cluster_sizes = (uint32_t *)calloc((size_t)cluster_count, sizeof(*cluster_sizes));
	cluster_weight_sum = (float *)calloc((size_t)cluster_count, sizeof(*cluster_weight_sum));
	cluster_edge_count = (uint32_t *)calloc((size_t)cluster_count, sizeof(*cluster_edge_count));
	node_strength = (float *)calloc((size_t)n_nodes, sizeof(*node_strength));
	new_cluster_ids = (int32_t *)malloc((size_t)n_nodes * sizeof(*new_cluster_ids));
	new_summaries = (ClusterSummary *)malloc((size_t)cluster_count * sizeof(*new_summaries));
	if (cluster_sizes == NULL || cluster_weight_sum == NULL || cluster_edge_count == NULL || node_strength == NULL ||
		new_cluster_ids == NULL || (cluster_count > 0u && new_summaries == NULL)) {
		free(cluster_sizes);
		free(cluster_weight_sum);
		free(cluster_edge_count);
		free(node_strength);
		free(new_cluster_ids);
		free(new_summaries);
		set_err(err, err_sz, "cluster output allocation failed");
		return -1;
	}

	for (i = 0; i < n_nodes; ++i) {
		uint32_t cid = (uint32_t)labels[i];

		new_cluster_ids[i] = labels[i];
		cluster_sizes[cid] += 1u;
	}
	for (i = 0; i < cluster_count; ++i) {
		new_summaries[i].cluster_id = i;
		new_summaries[i].size = cluster_sizes[i];
		new_summaries[i].representative_index = UINT32_MAX;
		new_summaries[i].score = 0.0f;
	}
	for (i = 0; i < edge_count; ++i) {
		uint32_t a = edges[i].src;
		uint32_t b = edges[i].dst;
		uint32_t cid_a = (uint32_t)labels[a];
		uint32_t cid_b = (uint32_t)labels[b];

		if (cid_a != cid_b) {
			continue;
		}
		node_strength[a] += edges[i].weight;
		node_strength[b] += edges[i].weight;
		cluster_weight_sum[cid_a] += edges[i].weight;
		cluster_edge_count[cid_a] += 1u;
	}
	for (i = 0; i < n_nodes; ++i) {
		uint32_t cid = (uint32_t)labels[i];
		uint32_t rep = new_summaries[cid].representative_index;

		if (rep == UINT32_MAX || node_strength[i] > node_strength[rep] || (node_strength[i] == node_strength[rep] && i < rep)) {
			new_summaries[cid].representative_index = i;
		}
	}
	for (i = 0; i < cluster_count; ++i) {
		if (cluster_edge_count[i] > 0u) {
			new_summaries[i].score = cluster_weight_sum[i] / (float)cluster_edge_count[i];
		}
	}

	old_cluster_ids = rec->clustering.cluster_ids;
	old_summaries = rec->clustering.summaries;
	rec->clustering.cluster_ids = new_cluster_ids;
	rec->clustering.summaries = new_summaries;
	rec->clustering.algorithm_type = JOB_TYPE_GRAPH_CLUSTERING;
	rec->clustering.cluster_count = cluster_count;
	rec->clustering.noise_count = 0;
	rec->clustering.graph_cluster_mode = cluster_mode;
	rec->clustering.graph_diag = *diag;
	finalize_cluster_histogram(&rec->clustering.graph_diag, cluster_count, cluster_sizes);

	free(old_cluster_ids);
	free(old_summaries);
	free(cluster_sizes);
	free(cluster_weight_sum);
	free(cluster_edge_count);
	free(node_strength);
	return 0;
}

static int store_cluster_output_precomputed(DatasetRecord *rec,
	uint32_t n_nodes,
	int32_t *labels,
	uint32_t cluster_count,
	const GraphDiagnostics *diag,
	uint32_t cluster_mode,
	const float *node_strength,
	const float *cluster_scores,
	char *err,
	size_t err_sz)
{
	uint32_t *cluster_sizes;
	int32_t *new_cluster_ids;
	ClusterSummary *new_summaries;
	int32_t *old_cluster_ids;
	ClusterSummary *old_summaries;
	uint32_t i;

	cluster_sizes = (uint32_t *)calloc((size_t)cluster_count, sizeof(*cluster_sizes));
	new_cluster_ids = (int32_t *)malloc((size_t)n_nodes * sizeof(*new_cluster_ids));
	new_summaries = (ClusterSummary *)malloc((size_t)cluster_count * sizeof(*new_summaries));
	if (cluster_sizes == NULL || new_cluster_ids == NULL || (cluster_count > 0u && new_summaries == NULL)) {
		free(cluster_sizes);
		free(new_cluster_ids);
		free(new_summaries);
		set_err(err, err_sz, "cluster output allocation failed");
		return -1;
	}

	for (i = 0; i < n_nodes; ++i) {
		uint32_t cid = (uint32_t)labels[i];

		new_cluster_ids[i] = labels[i];
		cluster_sizes[cid] += 1u;
	}
	for (i = 0; i < cluster_count; ++i) {
		new_summaries[i].cluster_id = i;
		new_summaries[i].size = cluster_sizes[i];
		new_summaries[i].representative_index = UINT32_MAX;
		new_summaries[i].score = (cluster_scores != NULL) ? cluster_scores[i] : 0.0f;
	}
	for (i = 0; i < n_nodes; ++i) {
		uint32_t cid = (uint32_t)labels[i];
		uint32_t rep = new_summaries[cid].representative_index;
		float rep_strength = (rep == UINT32_MAX || node_strength == NULL) ? -1.0f : node_strength[rep];
		float cur_strength = (node_strength != NULL) ? node_strength[i] : 0.0f;

		if (rep == UINT32_MAX || cur_strength > rep_strength || (cur_strength == rep_strength && i < rep)) {
			new_summaries[cid].representative_index = i;
		}
	}

	old_cluster_ids = rec->clustering.cluster_ids;
	old_summaries = rec->clustering.summaries;
	rec->clustering.algorithm_type = JOB_TYPE_GRAPH_CLUSTERING;
	rec->clustering.cluster_count = cluster_count;
	rec->clustering.noise_count = 0u;
	rec->clustering.graph_cluster_mode = cluster_mode;
	rec->clustering.graph_diag = *diag;
	finalize_cluster_histogram(&rec->clustering.graph_diag, cluster_count, cluster_sizes);
	rec->clustering.cluster_ids = new_cluster_ids;
	rec->clustering.summaries = new_summaries;

	free(old_cluster_ids);
	free(old_summaries);
	free(cluster_sizes);
	return 0;
}

int cluster_graph_run(uint32_t dataset_id, const GraphParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz)
{
	DatasetRecord *rec;
	const CharMatrix *m;
	GraphEdge *edges = NULL;
	uint32_t edge_count = 0;
	GraphDiagnostics diag;
	int32_t *labels = NULL;
	float *node_strength = NULL;
	float *cluster_scores = NULL;
	uint32_t cluster_count = 0;
	uint32_t graph_mode;
	int rc;

	if (params == NULL) {
		set_err(err, err_sz, "missing graph params");
		return -1;
	}
	if (!gpu_dispatch_available()) {
		set_err(err, err_sz, "GPU not available");
		return -2;
	}
	if (ensure_chars(dataset_id, params->q, &rec) != 0) {
		set_err(err, err_sz, "character preparation failed");
		return -3;
	}
	if (rec->chars.n_blocks == 0u || params->k_neighbors == 0u) {
		set_err(err, err_sz, "invalid graph params");
		return -4;
	}

	m = &rec->chars;
	graph_mode = params->cluster_mode;
	if (graph_mode == GRAPH_CLUSTER_MODE_LOUVAIN) {
		trainer_log_info("graph clustering mode=louvain requested; using label propagation baseline");
	}
	trainer_log_info("graph clustering start dataset_id=%u n_blocks=%u q=%u delta=%d body_gate=%d range_gate=%d k=%u mutual=%u max_dist=%.3f mode=%u",
		dataset_id,
		m->n_blocks,
		params->q,
		params->delta,
		params->delta_body_gate,
		params->delta_range_gate,
		params->k_neighbors,
		params->use_mutual_knn,
		params->max_neighbor_distance,
		params->cluster_mode);

	if (progress != NULL) {
		*progress = 0.01f;
	}

	if (graph_mode == GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS) {
		rc = build_legacy_threshold_graph(m, params, progress, cancel_flag, &labels, &cluster_count, &node_strength, &cluster_scores, &diag, err, err_sz);
	} else {
		rc = build_sparse_knn_graph(m, params, progress, cancel_flag, &edges, &edge_count, &diag, err, err_sz);
	}
	if (rc != 0) {
		free(edges);
		free(labels);
		free(node_strength);
		free(cluster_scores);
		return -5;
	}

	if (graph_mode != GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS) {
		labels = (int32_t *)malloc((size_t)m->n_blocks * sizeof(*labels));
		if (labels == NULL) {
			free(edges);
			set_err(err, err_sz, "label allocation failed");
			return -6;
		}
	}

	if (progress != NULL) {
		*progress = 0.60f;
	}
	if (graph_mode != GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS) {
		rc = cluster_sparse_graph(m->n_blocks, edges, edge_count, graph_mode, labels, &cluster_count);
		if (rc != 0) {
			free(edges);
			free(labels);
			set_err(err, err_sz, "graph clustering stage failed");
			return -7;
		}
	}

	if (progress != NULL) {
		*progress = 0.85f;
	}
	if (graph_mode == GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS) {
		rc = store_cluster_output_precomputed(rec, m->n_blocks, labels, cluster_count, &diag, graph_mode, node_strength, cluster_scores, err, err_sz);
	} else {
		rc = store_cluster_output(rec, m->n_blocks, edges, edge_count, labels, cluster_count, &diag, graph_mode, err, err_sz);
	}
	if (rc != 0) {
		free(edges);
		free(labels);
		free(node_strength);
		free(cluster_scores);
		return -5;
	}

	trainer_log_info("graph clustering complete dataset_id=%u clusters=%u edges=%u largest_component_fraction=%.3f percolated=%d",
		dataset_id,
		cluster_count,
		rec->clustering.graph_diag.n_edges,
		rec->clustering.graph_diag.largest_component_fraction,
		rec->clustering.graph_diag.percolated);

	free(edges);
	free(labels);
	free(node_strength);
	free(cluster_scores);
	if (progress != NULL) {
		*progress = 1.0f;
	}
	set_err(err, err_sz, "");
	return 0;
}
