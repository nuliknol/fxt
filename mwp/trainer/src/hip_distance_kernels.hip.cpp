#include "api_types.h"

#include <hip/hip_runtime.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

typedef struct {
	const CharMatrix *matrix;
	const int32_t *host_data;
	const BlockSummary *host_summaries;
	uint32_t n_blocks;
	uint32_t dim;
	size_t data_bytes;
	size_t summaries_bytes;
	int32_t *d_data;
	BlockSummary *d_summaries;
	int32_t *d_out;
	size_t out_capacity;
	hipStream_t stream;
	int stream_ready;
} HipDistanceCache;

static HipDistanceCache g_cache = {};
static std::mutex g_cache_mutex;

static void release_cache_locked(void)
{
	if (g_cache.d_out != nullptr) {
		(void)hipFree(g_cache.d_out);
		g_cache.d_out = nullptr;
	}
	if (g_cache.d_summaries != nullptr) {
		(void)hipFree(g_cache.d_summaries);
		g_cache.d_summaries = nullptr;
	}
	if (g_cache.d_data != nullptr) {
		(void)hipFree(g_cache.d_data);
		g_cache.d_data = nullptr;
	}
	if (g_cache.stream_ready) {
		(void)hipStreamDestroy(g_cache.stream);
		g_cache.stream_ready = 0;
	}
	g_cache.matrix = nullptr;
	g_cache.host_data = nullptr;
	g_cache.host_summaries = nullptr;
	g_cache.n_blocks = 0u;
	g_cache.dim = 0u;
	g_cache.data_bytes = 0u;
	g_cache.summaries_bytes = 0u;
	g_cache.out_capacity = 0u;
}

static int ensure_stream_locked(void)
{
	if (g_cache.stream_ready) {
		return 0;
	}
	if (hipStreamCreate(&g_cache.stream) != hipSuccess) {
		return -1;
	}
	g_cache.stream_ready = 1;
	return 0;
}

static int ensure_dataset_locked(const CharMatrix *m)
{
	size_t data_bytes;
	size_t summaries_bytes;

	data_bytes = (size_t)m->n_blocks * m->dim * sizeof(int32_t);
	summaries_bytes = (size_t)m->n_blocks * sizeof(BlockSummary);
	if (g_cache.matrix == m &&
		g_cache.host_data == m->data &&
		g_cache.host_summaries == m->summaries &&
		g_cache.n_blocks == m->n_blocks &&
		g_cache.dim == m->dim) {
		return 0;
	}

	release_cache_locked();
	if (ensure_stream_locked() != 0) {
		return -1;
	}
	if (hipMalloc((void **)&g_cache.d_data, data_bytes) != hipSuccess) {
		release_cache_locked();
		return -2;
	}
	if (hipMalloc((void **)&g_cache.d_summaries, summaries_bytes) != hipSuccess) {
		release_cache_locked();
		return -3;
	}
	if (hipMemcpyAsync(g_cache.d_data, m->data, data_bytes, hipMemcpyHostToDevice, g_cache.stream) != hipSuccess) {
		release_cache_locked();
		return -4;
	}
	if (hipMemcpyAsync(g_cache.d_summaries, m->summaries, summaries_bytes, hipMemcpyHostToDevice, g_cache.stream) != hipSuccess) {
		release_cache_locked();
		return -5;
	}
	if (hipStreamSynchronize(g_cache.stream) != hipSuccess) {
		release_cache_locked();
		return -6;
	}

	g_cache.matrix = m;
	g_cache.host_data = m->data;
	g_cache.host_summaries = m->summaries;
	g_cache.n_blocks = m->n_blocks;
	g_cache.dim = m->dim;
	g_cache.data_bytes = data_bytes;
	g_cache.summaries_bytes = summaries_bytes;
	return 0;
}

static int ensure_output_locked(size_t out_count)
{
	if (g_cache.out_capacity >= out_count) {
		return 0;
	}
	if (g_cache.d_out != nullptr) {
		(void)hipFree(g_cache.d_out);
		g_cache.d_out = nullptr;
		g_cache.out_capacity = 0u;
	}
	if (hipMalloc((void **)&g_cache.d_out, out_count * sizeof(int32_t)) != hipSuccess) {
		return -1;
	}
	g_cache.out_capacity = out_count;
	return 0;
}

__device__ static int32_t mirrored_coord(const int32_t *v, uint32_t idx)
{
	switch (idx % 4u) {
	case 0:
		return v[idx + 1u];
	case 1:
		return v[idx - 1u];
	case 2:
		return -v[idx];
	default:
		return v[idx];
	}
}

__device__ static float phi_excess(int32_t diff, int32_t delta)
{
	int32_t excess = abs(diff) - delta;

	return (excess > 0) ? (float)excess : 0.0f;
}

__device__ static int body_distance_value(int32_t body_a, int32_t body_b, const DistanceConfig *cfg, int use_mirror)
{
	int32_t adjusted_b = use_mirror ? -body_b : body_b;

	if (cfg->body_mode == 1) {
		return abs(abs(body_a) - abs(adjusted_b));
	}
	if (cfg->body_mode == 2) {
		int32_t mag_diff = abs(abs(body_a) - abs(adjusted_b));
		int32_t sign_penalty = 0;

		if ((body_a > 0 && adjusted_b <= 0) || (body_a < 0 && adjusted_b >= 0)) {
			sign_penalty = (int32_t)cfg->body_sign_penalty;
		}
		return mag_diff + sign_penalty;
	}
	return abs(body_a - adjusted_b);
}

__device__ static int majority_sign(const BlockSummary *summary)
{
	if (summary->bull_count > summary->bear_count) {
		return 1;
	}
	if (summary->bear_count > summary->bull_count) {
		return -1;
	}
	return 0;
}

__device__ static int threshold_regime_group_device(const BlockSummary *summary, uint32_t q)
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

__device__ static int threshold_composition_match_device(const BlockSummary *a, const BlockSummary *b, uint32_t q)
{
	int group_a = threshold_regime_group_device(a, q);
	int group_b = threshold_regime_group_device(b, q);

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

__device__ static int ratio_pct(int32_t numerator, int32_t denominator)
{
	int32_t denom = (denominator > 0) ? denominator : 1;

	return (int)(((int64_t)100 * (int64_t)numerator) / (int64_t)denom);
}

__device__ static BlockSummary mirrored_summary(const BlockSummary *summary)
{
	BlockSummary out = *summary;

	out.bull_count = summary->bear_count;
	out.bear_count = summary->bull_count;
	out.max_bull_run = summary->max_bear_run;
	out.max_bear_run = summary->max_bull_run;
	out.net_body = -summary->net_body;
	out.upper_sum = summary->lower_sum;
	out.lower_sum = summary->upper_sum;
	return out;
}

__device__ static int passes_gate(const BlockSummary *a, const BlockSummary *b, const DistanceConfig *cfg, int use_mirror)
{
	BlockSummary compare = use_mirror ? mirrored_summary(b) : *b;
	int lower_ratio_a;
	int lower_ratio_b;
	int upper_ratio_a;
	int upper_ratio_b;
	int total_ratio_a;
	int total_ratio_b;

	if (abs(a->net_body - compare.net_body) > cfg->gate_body_diff) {
		return 0;
	}
	if (abs(a->avg_range - compare.avg_range) > cfg->gate_range_diff) {
		return 0;
	}
	if (abs(a->bull_count - compare.bull_count) > cfg->gate_bullcount_diff) {
		return 0;
	}
	if (abs(a->sign_changes - compare.sign_changes) > cfg->gate_signchange_diff) {
		return 0;
	}
	if (cfg->require_same_majority_sign && majority_sign(a) != majority_sign(&compare)) {
		return 0;
	}
	if ((cfg->distance_mode == DISTANCE_MODE_STRICT_COMPOSITION || cfg->distance_mode == DISTANCE_MODE_WICK_AWARE) &&
		!threshold_composition_match_device(a, &compare, cfg->block_q)) {
		return 0;
	}
	if (cfg->distance_mode != DISTANCE_MODE_WICK_AWARE) {
		return 1;
	}
	if (abs(a->upper_sum - compare.upper_sum) > cfg->gate_upper_wick_diff) {
		return 0;
	}
	if (abs(a->lower_sum - compare.lower_sum) > cfg->gate_lower_wick_diff) {
		return 0;
	}
	lower_ratio_a = ratio_pct(a->lower_sum, a->gross_body);
	lower_ratio_b = ratio_pct(compare.lower_sum, compare.gross_body);
	upper_ratio_a = ratio_pct(a->upper_sum, a->gross_body);
	upper_ratio_b = ratio_pct(compare.upper_sum, compare.gross_body);
	total_ratio_a = ratio_pct(a->lower_sum + a->upper_sum, a->gross_body);
	total_ratio_b = ratio_pct(compare.lower_sum + compare.upper_sum, compare.gross_body);
	if (abs(lower_ratio_a - lower_ratio_b) > cfg->gate_lower_ratio_diff) {
		return 0;
	}
	if (abs(upper_ratio_a - upper_ratio_b) > cfg->gate_upper_ratio_diff) {
		return 0;
	}
	if (abs(total_ratio_a - total_ratio_b) > cfg->gate_total_wick_ratio_diff) {
		return 0;
	}
	return 1;
}

__global__ static void distance_kernel(const int32_t *data,
	const BlockSummary *summaries,
	uint32_t dim,
	uint32_t row_start,
	uint32_t row_count,
	uint32_t col_start,
	uint32_t col_count,
	DistanceConfig cfg,
	int32_t *out_dist)
{
	uint32_t ridx = blockIdx.y * blockDim.y + threadIdx.y;
	uint32_t cidx = blockIdx.x * blockDim.x + threadIdx.x;
	const int32_t *a;
	const int32_t *b;
	const BlockSummary *sa;
	const BlockSummary *sb;
	float direct = 0.0f;
	float mirrored = 0.0f;
	uint32_t k;

	if (ridx >= row_count || cidx >= col_count) {
		return;
	}
	a = data + ((size_t)(row_start + ridx) * dim);
	b = data + ((size_t)(col_start + cidx) * dim);
	sa = summaries + row_start + ridx;
	sb = summaries + col_start + cidx;

	if (cfg.distance_mode == DISTANCE_MODE_LEGACY ||
		cfg.distance_mode == DISTANCE_MODE_STRICT_COMPOSITION ||
		cfg.distance_mode == DISTANCE_MODE_WICK_AWARE) {
		if ((cfg.distance_mode == DISTANCE_MODE_STRICT_COMPOSITION || cfg.distance_mode == DISTANCE_MODE_WICK_AWARE) &&
			!passes_gate(sa, sb, &cfg, 0)) {
			out_dist[ridx * col_count + cidx] = INT32_MAX;
			return;
		}
		for (k = 0; k < dim; ++k) {
			int mismatch = (abs(a[k] - b[k]) > cfg.delta) ? 1 : 0;
			int mirror_mismatch = (abs(a[k] - mirrored_coord(b, k)) > cfg.delta) ? 1 : 0;
			float w = 1.0f;

			switch (k % 4u) {
			case 0:
				w = cfg.w_upper;
				break;
			case 1:
				w = cfg.w_lower;
				break;
			case 2:
				w = cfg.w_body;
				break;
			default:
				w = cfg.w_range;
				break;
			}
			direct += cfg.weighted ? (w * (float)mismatch) : (float)mismatch;
			mirrored += cfg.weighted ? (w * (float)mirror_mismatch) : (float)mirror_mismatch;
		}
		if (cfg.mirrored_mode &&
			(cfg.distance_mode == DISTANCE_MODE_STRICT_COMPOSITION || cfg.distance_mode == DISTANCE_MODE_WICK_AWARE) &&
			!passes_gate(sa, sb, &cfg, 1)) {
			mirrored = 2147483647.0f;
		}
		out_dist[ridx * col_count + cidx] = (int32_t)(cfg.mirrored_mode ? ((direct < mirrored) ? direct : mirrored) : direct);
		return;
	}

	if (!passes_gate(sa, sb, &cfg, 0)) {
		direct = -1.0f;
	} else {
		for (k = 0; k < dim; ++k) {
			uint32_t slot = k % 4u;

			if (slot == 0u && cfg.use_upper) {
				direct += cfg.w_upper * phi_excess(a[k] - b[k], cfg.delta);
			} else if (slot == 1u && cfg.use_lower) {
				direct += cfg.w_lower * phi_excess(a[k] - b[k], cfg.delta);
			} else if (slot == 2u && cfg.use_body) {
				direct += cfg.w_body * phi_excess(body_distance_value(a[k], b[k], &cfg, 0), cfg.delta);
			} else if (slot == 3u && cfg.use_range) {
				direct += cfg.w_range * phi_excess(a[k] - b[k], cfg.delta);
			}
		}
		if (cfg.use_composition) {
			direct += cfg.w_bullcount * (float)abs(sa->bull_count - sb->bull_count);
			direct += cfg.w_signchanges * (float)abs(sa->sign_changes - sb->sign_changes);
			direct += cfg.w_runbull * (float)abs(sa->max_bull_run - sb->max_bull_run);
			direct += cfg.w_runbear * (float)abs(sa->max_bear_run - sb->max_bear_run);
		}
	}

	if (!cfg.mirrored_mode) {
		out_dist[ridx * col_count + cidx] = (direct < 0.0f) ? INT32_MAX : (int32_t)direct;
		return;
	}

	if (!passes_gate(sa, sb, &cfg, 1)) {
		mirrored = -1.0f;
	} else {
		BlockSummary ms = mirrored_summary(sb);

		for (k = 0; k < dim; ++k) {
			uint32_t slot = k % 4u;
			int32_t bv = mirrored_coord(b, k);

			if (slot == 0u && cfg.use_upper) {
				mirrored += cfg.w_upper * phi_excess(a[k] - bv, cfg.delta);
			} else if (slot == 1u && cfg.use_lower) {
				mirrored += cfg.w_lower * phi_excess(a[k] - bv, cfg.delta);
			} else if (slot == 2u && cfg.use_body) {
				mirrored += cfg.w_body * phi_excess(body_distance_value(a[k], b[k], &cfg, 1), cfg.delta);
			} else if (slot == 3u && cfg.use_range) {
				mirrored += cfg.w_range * phi_excess(a[k] - bv, cfg.delta);
			}
		}
		if (cfg.use_composition) {
			mirrored += cfg.w_bullcount * (float)abs(sa->bull_count - ms.bull_count);
			mirrored += cfg.w_signchanges * (float)abs(sa->sign_changes - ms.sign_changes);
			mirrored += cfg.w_runbull * (float)abs(sa->max_bull_run - ms.max_bull_run);
			mirrored += cfg.w_runbear * (float)abs(sa->max_bear_run - ms.max_bear_run);
		}
	}

	if (direct < 0.0f && mirrored < 0.0f) {
		out_dist[ridx * col_count + cidx] = INT32_MAX;
	} else if (direct < 0.0f) {
		out_dist[ridx * col_count + cidx] = (int32_t)mirrored;
	} else if (mirrored < 0.0f) {
		out_dist[ridx * col_count + cidx] = (int32_t)direct;
	} else {
		out_dist[ridx * col_count + cidx] = (int32_t)((direct < mirrored) ? direct : mirrored);
	}
}

extern "C" int hip_distance_tile_impl(const CharMatrix *m,
	uint32_t row_start,
	uint32_t row_count,
	uint32_t col_start,
	uint32_t col_count,
	const DistanceConfig *cfg,
	int32_t *out_dist)
{
	dim3 block(16, 16);
	dim3 grid((col_count + block.x - 1) / block.x, (row_count + block.y - 1) / block.y);
	size_t out_count;
	std::lock_guard<std::mutex> lock(g_cache_mutex);

	if (m == nullptr || cfg == nullptr || out_dist == nullptr || m->data == nullptr || m->summaries == nullptr) {
		return -1;
	}
	out_count = (size_t)row_count * col_count;
	if (ensure_dataset_locked(m) != 0) {
		return -2;
	}
	if (ensure_output_locked(out_count) != 0) {
		return -3;
	}

	hipLaunchKernelGGL(distance_kernel,
		grid,
		block,
		0,
		g_cache.stream,
		g_cache.d_data,
		g_cache.d_summaries,
		m->dim,
		row_start,
		row_count,
		col_start,
		col_count,
		*cfg,
		g_cache.d_out);

	if (hipGetLastError() != hipSuccess) {
		return -4;
	}
	if (hipMemcpyAsync(out_dist, g_cache.d_out, out_count * sizeof(int32_t), hipMemcpyDeviceToHost, g_cache.stream) != hipSuccess) {
		return -5;
	}
	if (hipStreamSynchronize(g_cache.stream) != hipSuccess) {
		return -6;
	}
	return 0;
}

extern "C" void hip_distance_shutdown_impl(void)
{
	std::lock_guard<std::mutex> lock(g_cache_mutex);

	release_cache_locked();
	(void)hipDeviceReset();
}
