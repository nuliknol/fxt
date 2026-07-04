#include "core_api.h"
#include "live_store.h"
#include "logger.h"
#include "wave_predictor.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int json_encode_dataset_list(char *out, size_t out_cap);
int json_encode_dataset_detail(uint32_t dataset_id, char *out, size_t out_cap);
int json_encode_candle_window(uint32_t dataset_id, uint32_t start, uint32_t count, char *out, size_t out_cap);
int json_encode_feature_window(uint32_t dataset_id, uint32_t start, uint32_t count, char *out, size_t out_cap);
int json_encode_job_list(char *out, size_t out_cap);
int json_encode_job_detail(uint64_t job_id, char *out, size_t out_cap);
int json_encode_cluster_list(uint32_t dataset_id, char *out, size_t out_cap);
int json_encode_cluster_member_list(uint32_t dataset_id, uint32_t cluster_id, uint32_t start, uint32_t count, char *out, size_t out_cap);
int json_encode_word_list(uint32_t dataset_id, char *out, size_t out_cap);
int json_encode_phrase_list(uint32_t dataset_id, char *out, size_t out_cap);
int json_encode_wave_list(uint32_t dataset_id, uint32_t start, uint32_t count, const char *class_name, const char *order_by, char *out, size_t out_cap);
int json_encode_micro_harvest_latest(uint32_t dataset_id, char *out, size_t out_cap);

#define REQ_HEADER_BUF_SZ 16384
#define REQ_MAX_BODY_SZ 4194304
#define RES_BUF_SZ 1048576

static void print_live_feed_event(const char *event_name, const char *symbol, const char *timeframe, const char *detail)
{
	fprintf(stdout,
		"trainer_daemon %s symbol=%s timeframe=%s%s%s\n",
		event_name,
		(symbol != NULL && symbol[0] != '\0') ? symbol : "-",
		(timeframe != NULL && timeframe[0] != '\0') ? timeframe : "-",
		(detail != NULL && detail[0] != '\0') ? " " : "",
		(detail != NULL && detail[0] != '\0') ? detail : "");
	fflush(stdout);
}

static int path_is_live_endpoint(const char *path)
{
	return path != NULL && strncmp(path, "/live/", 6) == 0;
}

static void log_http_request_line(const char *method, const char *path, const char *query)
{
	int is_live = path_is_live_endpoint(path);

	if (method == NULL || path == NULL) {
		return;
	}
	if (query != NULL && query[0] != '\0') {
		if (is_live) {
			trainer_log_live("http request method=%s path=%s query=%s", method, path, query);
		} else {
			trainer_log_info("http request method=%s path=%s query=%s", method, path, query);
		}
	} else {
		if (is_live) {
			trainer_log_live("http request method=%s path=%s", method, path);
		} else {
			trainer_log_info("http request method=%s path=%s", method, path);
		}
	}
}

static void log_http_response_line(int is_live, int status, const char *body)
{
	char preview[257];
	size_t i = 0;

	if (body == NULL) {
		if (is_live) {
			trainer_log_live("http response status=%d body=<null>", status);
		} else {
			trainer_log_info("http response status=%d body=<null>", status);
		}
		return;
	}
	while (body[i] != '\0' && i + 1u < sizeof(preview)) {
		preview[i] = body[i];
		i += 1u;
	}
	preview[i] = '\0';
	if (is_live) {
		trainer_log_live("http response status=%d body=%s%s",
			status,
			preview,
			(body[i] != '\0') ? "..." : "");
	} else {
		trainer_log_info("http response status=%d body=%s%s",
			status,
			preview,
			(body[i] != '\0') ? "..." : "");
	}
}

static int write_all(int fd, const char *buf, size_t n)
{
	size_t off = 0;
	while (off < n) {
		ssize_t w = write(fd, buf + off, n - off);
		if (w <= 0) {
			return -1;
		}
		off += (size_t)w;
	}
	return 0;
}

static void send_json_response(int cfd, int is_live, int status, const char *body)
{
	char head[256];
	size_t body_len = strlen(body);
	const char *status_text = (status == 200) ? "OK" : "ERROR";
	int n = snprintf(head,
		sizeof(head),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n\r\n",
		status,
		status_text,
		body_len);
	if (n > 0) {
		write_all(cfd, head, (size_t)n);
		write_all(cfd, body, body_len);
	}
	log_http_response_line(is_live, status, body);
	if (status >= 400) {
		trainer_log_error("http response status=%d body=%s", status, body);
	}
}

static int query_get_u32(const char *query, const char *key, uint32_t fallback)
{
	const char *p;
	char pattern[64];
	unsigned int v;

	if (query == NULL) {
		return (int)fallback;
	}
	snprintf(pattern, sizeof(pattern), "%s=", key);
	p = strstr(query, pattern);
	if (p == NULL) {
		return (int)fallback;
	}
	p += strlen(pattern);
	if (sscanf(p, "%u", &v) != 1) {
		return (int)fallback;
	}
	return (int)v;
}

static float query_get_f32(const char *query, const char *key, float fallback)
{
	const char *p;
	char pattern[64];
	float v;

	if (query == NULL) {
		return fallback;
	}
	snprintf(pattern, sizeof(pattern), "%s=", key);
	p = strstr(query, pattern);
	if (p == NULL) {
		return fallback;
	}
	p += strlen(pattern);
	if (sscanf(p, "%f", &v) != 1) {
		return fallback;
	}
	return v;
}

static int32_t query_get_i32(const char *query, const char *key, int32_t fallback)
{
	const char *p;
	char pattern[64];
	int v;

	if (query == NULL) {
		return fallback;
	}
	snprintf(pattern, sizeof(pattern), "%s=", key);
	p = strstr(query, pattern);
	if (p == NULL) {
		return fallback;
	}
	p += strlen(pattern);
	if (sscanf(p, "%d", &v) != 1) {
		return fallback;
	}
	return (int32_t)v;
}

static double query_get_f64(const char *query, const char *key, double fallback)
{
	const char *p;
	char pattern[64];
	double v;

	if (query == NULL) {
		return fallback;
	}
	snprintf(pattern, sizeof(pattern), "%s=", key);
	p = strstr(query, pattern);
	if (p == NULL) {
		return fallback;
	}
	p += strlen(pattern);
	if (sscanf(p, "%lf", &v) != 1) {
		return fallback;
	}
	return v;
}

static int hex_value(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return 10 + ch - 'a';
	}
	if (ch >= 'A' && ch <= 'F') {
		return 10 + ch - 'A';
	}
	return -1;
}

static int query_get_str(const char *query, const char *key, char *out, size_t out_sz)
{
	const char *p;
	char pattern[64];
	size_t w = 0u;

	if (query == NULL || out == NULL || out_sz == 0) {
		return -1;
	}
	snprintf(pattern, sizeof(pattern), "%s=", key);
	p = strstr(query, pattern);
	if (p == NULL) {
		return -1;
	}
	p += strlen(pattern);
	while (*p != '\0' && *p != '&' && w + 1u < out_sz) {
		if (*p == '%' && p[1] != '\0' && p[2] != '\0') {
			int hi = hex_value(p[1]);
			int lo = hex_value(p[2]);

			if (hi >= 0 && lo >= 0) {
				out[w++] = (char)((hi << 4) | lo);
				p += 3;
				continue;
			}
		}
		out[w++] = (*p == '+') ? ' ' : *p;
		p += 1;
	}
	out[w] = '\0';
	return (w > 0u) ? 0 : -1;
}

static const char *json_body_find_key(const char *body, const char *key)
{
	char pattern[96];

	if (body == NULL || key == NULL) {
		return NULL;
	}
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	return strstr(body, pattern);
}

static int json_body_get_i32(const char *body, const char *key, int32_t fallback)
{
	const char *p = json_body_find_key(body, key);
	int value;

	if (p == NULL) {
		return fallback;
	}
	p = strchr(p, ':');
	if (p == NULL) {
		return fallback;
	}
	p += 1;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p += 1;
	}
	if (sscanf(p, "%d", &value) != 1) {
		return fallback;
	}
	return value;
}

static int json_body_get_str(const char *body, const char *key, char *out, size_t out_sz)
{
	const char *p = json_body_find_key(body, key);
	size_t w = 0u;

	if (p == NULL || out == NULL || out_sz == 0u) {
		return -1;
	}
	p = strchr(p, ':');
	if (p == NULL) {
		return -1;
	}
	p += 1;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p += 1;
	}
	if (*p != '"') {
		return -1;
	}
	p += 1;
	while (*p != '\0' && *p != '"' && w + 1u < out_sz) {
		out[w++] = *p++;
	}
	out[w] = '\0';
	return (w > 0u) ? 0 : -1;
}

static size_t http_content_length(const char *req)
{
	const char *p;
	unsigned long long v;

	if (req == NULL) {
		return 0u;
	}
	p = strstr(req, "Content-Length:");
	if (p == NULL) {
		p = strstr(req, "content-length:");
	}
	if (p == NULL) {
		return 0u;
	}
	p += strlen("Content-Length:");
	while (*p == ' ' || *p == '\t') {
		p += 1;
	}
	if (sscanf(p, "%llu", &v) != 1) {
		return 0u;
	}
	return (size_t)v;
}

static uint32_t query_get_graph_cluster_mode(const char *query, uint32_t fallback)
{
	char value[64];

	if (query_get_str(query, "cluster_mode", value, sizeof(value)) != 0) {
		return fallback;
	}
	if (strcmp(value, "legacy_connected_components") == 0) {
		return GRAPH_CLUSTER_MODE_LEGACY_CONNECTED_COMPONENTS;
	}
	if (strcmp(value, "connected_components") == 0) {
		return GRAPH_CLUSTER_MODE_CONNECTED_COMPONENTS;
	}
	if (strcmp(value, "label_propagation") == 0) {
		return GRAPH_CLUSTER_MODE_LABEL_PROPAGATION;
	}
	if (strcmp(value, "louvain") == 0) {
		return GRAPH_CLUSTER_MODE_LOUVAIN;
	}
	return (uint32_t)query_get_u32(query, "cluster_mode", fallback);
}

static uint32_t query_get_distance_mode(const char *query, uint32_t fallback)
{
	char value[64];

	if (query_get_str(query, "distance_mode", value, sizeof(value)) != 0) {
		return fallback;
	}
	if (strcmp(value, "feature_aware") == 0) {
		return DISTANCE_MODE_FEATURE_AWARE;
	}
	if (strcmp(value, "legacy") == 0) {
		return DISTANCE_MODE_LEGACY;
	}
	if (strcmp(value, "strict_composition") == 0) {
		return DISTANCE_MODE_STRICT_COMPOSITION;
	}
	if (strcmp(value, "wick_aware") == 0) {
		return DISTANCE_MODE_WICK_AWARE;
	}
	return (uint32_t)query_get_u32(query, "distance_mode", fallback);
}

static uint32_t query_get_wave_model(const char *query, uint32_t fallback)
{
	char value[64];

	if (query_get_str(query, "wave_model", value, sizeof(value)) != 0) {
		return fallback;
	}
	if (strcmp(value, "legacy_waves") == 0) {
		return WAVE_MODEL_LEGACY;
	}
	if (strcmp(value, "primitive_waves") == 0) {
		return WAVE_MODEL_PRIMITIVE;
	}
	if (strcmp(value, "relative_waves") == 0) {
		return WAVE_MODEL_RELATIVE;
	}
	if (strcmp(value, "relative_waves_2_stage") == 0) {
		return WAVE_MODEL_RELATIVE_2_STAGE;
	}
	if (strcmp(value, "relative_waves_seeded") == 0) {
		return WAVE_MODEL_RELATIVE_SEEDED;
	}
	if (strcmp(value, "relative_waves_iterative") == 0) {
		return WAVE_MODEL_RELATIVE_ITERATIVE;
	}
	if (strcmp(value, "layered_waves") == 0) {
		return WAVE_MODEL_LAYERED;
	}
	return (uint32_t)query_get_u32(query, "wave_model", fallback);
}

static const char *live_recompute_status_name(int status)
{
	if (status == LIVE_RECOMPUTE_FRESH) {
		return "fresh";
	}
	if (status == LIVE_RECOMPUTE_STALE) {
		return "stale";
	}
	return "empty";
}

static int append_live_summary_json(char *out, size_t out_cap, const LiveStreamSummary *items, uint32_t count)
{
	size_t used = 0u;
	uint32_t i;
	int n;

	n = snprintf(out + used, out_cap - used, "{\"streams\":[");
	if (n < 0 || (size_t)n >= out_cap - used) {
		return -1;
	}
	used += (size_t)n;
	for (i = 0u; i < count; ++i) {
		const LiveStreamSummary *item = &items[i];

		n = snprintf(out + used,
			out_cap - used,
			"%s{\"symbol\":\"%s\",\"timeframe\":\"%s\",\"dataset_id\":%u,\"pip_scale\":%u,"
			"\"candle_count\":%u,\"wave_count\":%u,\"last_ts\":%d,\"last_recompute_ts\":%d,"
			"\"last_update_ms\":%llu,\"last_recompute_ms\":%llu,\"waves_stale\":%d,"
			"\"recompute_status\":\"%s\",\"last_batch_input\":%u,\"last_batch_changed\":%u,"
			"\"class_counts\":{\"trend\":%u,\"range\":%u}}",
			(i == 0u) ? "" : ",",
			item->symbol,
			item->timeframe,
			item->dataset_id,
			item->pip_scale,
			item->candle_count,
			item->wave_count,
			item->last_ts,
			item->last_recompute_ts,
			(unsigned long long)item->last_update_ms,
			(unsigned long long)item->last_recompute_ms,
			item->waves_stale,
			live_recompute_status_name(item->recompute_status),
			item->last_batch_input,
			item->last_batch_changed,
			item->class_counts[RELATIVE_CLASS_TREND],
			item->class_counts[RELATIVE_CLASS_RANGE]);
		if (n < 0 || (size_t)n >= out_cap - used) {
			return -1;
		}
		used += (size_t)n;
	}
	n = snprintf(out + used, out_cap - used, "]}");
	if (n < 0 || (size_t)n >= out_cap - used) {
		return -1;
	}
	return (int)(used + (size_t)n);
}

static int encode_live_single_summary_json(char *out, size_t out_cap, const LiveStreamSummary *item)
{
	int n;

	n = snprintf(out,
		out_cap,
		"{\"symbol\":\"%s\",\"timeframe\":\"%s\",\"dataset_id\":%u,\"pip_scale\":%u,"
		"\"candle_count\":%u,\"wave_count\":%u,\"last_ts\":%d,\"last_recompute_ts\":%d,"
		"\"last_update_ms\":%llu,\"last_recompute_ms\":%llu,\"waves_stale\":%d,"
		"\"recompute_status\":\"%s\",\"last_batch_input\":%u,\"last_batch_changed\":%u,"
		"\"class_counts\":{\"trend\":%u,\"range\":%u}}",
		item->symbol,
		item->timeframe,
		item->dataset_id,
		item->pip_scale,
		item->candle_count,
		item->wave_count,
		item->last_ts,
		item->last_recompute_ts,
		(unsigned long long)item->last_update_ms,
		(unsigned long long)item->last_recompute_ms,
		item->waves_stale,
		live_recompute_status_name(item->recompute_status),
		item->last_batch_input,
		item->last_batch_changed,
		item->class_counts[RELATIVE_CLASS_TREND],
		item->class_counts[RELATIVE_CLASS_RANGE]);
	if (n < 0 || (size_t)n >= out_cap) {
		return -1;
	}
	return n;
}

static void handle_client(int cfd, const char *client_ip, uint16_t client_port)
{
	char header_buf[REQ_HEADER_BUF_SZ];
	char *req = NULL;
	char method[16];
	char target[1024];
	char path[1024];
	char *query = NULL;
	ssize_t rn;
	char json[RES_BUF_SZ];
	int n;
	char *body = NULL;
	size_t body_len = 0u;
	int is_live_request = 0;

#define send_json(CFD, STATUS, BODY) \
	do { \
		send_json_response((CFD), is_live_request, (STATUS), (BODY)); \
		if (client_ip != NULL) { \
			if (is_live_request) { \
				trainer_log_live("http connection closed fd=%d client=%s:%u", (CFD), client_ip, (unsigned int)client_port); \
			} else { \
				trainer_log_info("http connection closed fd=%d client=%s:%u", (CFD), client_ip, (unsigned int)client_port); \
			} \
		} \
		free(req); \
		return; \
	} while (0)

	rn = read(cfd, header_buf, sizeof(header_buf) - 1u);
	if (rn <= 0) {
		return;
	}
	{
		size_t req_len = (size_t)rn;
		char *header_end;
		size_t content_len;
		size_t header_len;
		size_t req_cap;

		header_buf[req_len] = '\0';
		header_end = strstr(header_buf, "\r\n\r\n");
		content_len = http_content_length(header_buf);
		if (content_len > REQ_MAX_BODY_SZ) {
			send_json(cfd, 413, "{\"error\":\"request body too large\"}");
			return;
		}
		if (header_end == NULL) {
			req_cap = req_len + 1u;
			req = (char *)malloc(req_cap);
			if (req == NULL) {
				send_json(cfd, 500, "{\"error\":\"request allocation failed\"}");
				return;
			}
			memcpy(req, header_buf, req_len + 1u);
		} else {
			size_t have_body;

			header_len = (size_t)(header_end + 4 - header_buf);
			req_cap = header_len + content_len + 1u;
			if (req_cap < req_len + 1u) {
				req_cap = req_len + 1u;
			}
			req = (char *)malloc(req_cap);
			if (req == NULL) {
				send_json(cfd, 500, "{\"error\":\"request allocation failed\"}");
				return;
			}
			memcpy(req, header_buf, req_len + 1u);
			header_end = req + header_len - 4u;
			have_body = (req_len > header_len) ? (req_len - header_len) : 0u;

			while (have_body < content_len && req_len + 1u < req_cap) {
				ssize_t more = read(cfd, req + req_len, req_cap - req_len - 1u);

				if (more <= 0) {
					break;
				}
				req_len += (size_t)more;
				req[req_len] = '\0';
				have_body = (req_len > header_len) ? (req_len - header_len) : 0u;
			}
			body = header_end + 4;
			body_len = (have_body < content_len) ? have_body : content_len;
			body[body_len] = '\0';
		}
	}

	if (sscanf(req, "%15s %1023s", method, target) != 2) {
		send_json(cfd, 400, "{\"error\":\"bad request\"}");
		free(req);
		return;
	}

	snprintf(path, sizeof(path), "%s", target);
	query = strchr(path, '?');
	if (query != NULL) {
		*query = '\0';
		query += 1;
	}
	is_live_request = path_is_live_endpoint(path);
	if (client_ip != NULL) {
		if (is_live_request) {
			trainer_log_live("http connection accepted fd=%d client=%s:%u", cfd, client_ip, (unsigned int)client_port);
		} else {
			trainer_log_info("http connection accepted fd=%d client=%s:%u", cfd, client_ip, (unsigned int)client_port);
		}
	}
	log_http_request_line(method, path, query);

	if (strcmp(method, "GET") == 0 && strcmp(path, "/health") == 0) {
		send_json(cfd, 200, "{\"status\":\"ok\"}");
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/live/status") == 0) {
		LiveStreamSummary items[64];
		uint32_t item_count = 0u;

		if (live_store_get_status(items, 64u, &item_count) != 0 ||
			append_live_summary_json(json, sizeof(json), items, item_count) < 0) {
			send_json(cfd, 500, "{\"error\":\"live status unavailable\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/live/summary") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		LiveStreamSummary summary;

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0) {
			send_json(cfd, 400, "{\"error\":\"missing symbol or timeframe\"}");
			return;
		}
		if (live_store_get_summary(symbol, timeframe, &summary) != 0) {
			send_json(cfd, 404, "{\"error\":\"live stream not found\"}");
			return;
		}
		if (encode_live_single_summary_json(json, sizeof(json), &summary) < 0) {
			send_json(cfd, 500, "{\"error\":\"encode failed\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/live/candle") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		uint32_t pip_scale;
		Candle candle;
		LiveStreamSummary summary;
		char err[256];
		double open_v;
		double high_v;
		double low_v;
		double close_v;
		int recompute;

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0) {
			send_json(cfd, 400, "{\"error\":\"missing symbol or timeframe\"}");
			return;
		}
		pip_scale = (uint32_t)query_get_u32(query, "pip_scale", 10000u);
		open_v = query_get_f64(query, "open", 0.0);
		high_v = query_get_f64(query, "high", 0.0);
		low_v = query_get_f64(query, "low", 0.0);
		close_v = query_get_f64(query, "close", 0.0);
		candle.ts_epoch = query_get_i32(query, "ts", 0);
		candle.open_pips = (int32_t)lround(open_v * (double)pip_scale);
		candle.high_pips = (int32_t)lround(high_v * (double)pip_scale);
		candle.low_pips = (int32_t)lround(low_v * (double)pip_scale);
		candle.close_pips = (int32_t)lround(close_v * (double)pip_scale);
		candle.volume = query_get_i32(query, "volume", 0);
		recompute = query_get_u32(query, "recompute", 1) ? 1 : 0;
		if (candle.ts_epoch <= 0 || high_v == 0.0 || low_v == 0.0) {
			send_json(cfd, 400, "{\"error\":\"missing candle values\"}");
			return;
		}
		if (live_store_upsert_candle_ex(symbol, timeframe, pip_scale, &candle, recompute, &summary, err, sizeof(err)) != 0) {
			snprintf(json, sizeof(json), "{\"error\":\"live candle failed\",\"detail\":\"%s\"}", err);
			send_json(cfd, 500, json);
			return;
		}
		snprintf(json,
			sizeof(json),
			"ts=%d close_pips=%d volume=%d recompute=%d",
			candle.ts_epoch,
			candle.close_pips,
			candle.volume,
			recompute);
		print_live_feed_event("live_feed_candle", symbol, timeframe, json);
		if (encode_live_single_summary_json(json, sizeof(json), &summary) < 0) {
			send_json(cfd, 500, "{\"error\":\"encode failed\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/live/candles_batch") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		uint32_t pip_scale;
		uint32_t max_items = 0u;
		uint32_t candle_count = 0u;
		uint32_t changed_count = 0u;
		Candle *candles;
		LiveStreamSummary summary;
		char err[256];
		char *cursor;
		int recompute;

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0) {
			send_json(cfd, 400, "{\"error\":\"missing symbol or timeframe\"}");
			return;
		}
		if (body == NULL || body_len == 0u) {
			send_json(cfd, 400, "{\"error\":\"missing candle batch body\"}");
			return;
		}
		pip_scale = (uint32_t)query_get_u32(query, "pip_scale", 10000u);
		recompute = query_get_u32(query, "recompute", 0) ? 1 : 0;
		for (cursor = body; *cursor != '\0'; ++cursor) {
			if (*cursor == '\n') {
				max_items += 1u;
			}
		}
		max_items += 1u;
		candles = (Candle *)malloc((size_t)max_items * sizeof(*candles));
		if (candles == NULL) {
			send_json(cfd, 500, "{\"error\":\"batch allocation failed\"}");
			return;
		}
		cursor = body;
		while (*cursor != '\0' && candle_count < max_items) {
			int ts;
			double open_v;
			double high_v;
			double low_v;
			double close_v;
			int volume;
			int consumed = 0;

			if (sscanf(cursor, "%d,%lf,%lf,%lf,%lf,%d%n", &ts, &open_v, &high_v, &low_v, &close_v, &volume, &consumed) == 6) {
				candles[candle_count].ts_epoch = (int32_t)ts;
				candles[candle_count].open_pips = (int32_t)lround(open_v * (double)pip_scale);
				candles[candle_count].high_pips = (int32_t)lround(high_v * (double)pip_scale);
				candles[candle_count].low_pips = (int32_t)lround(low_v * (double)pip_scale);
				candles[candle_count].close_pips = (int32_t)lround(close_v * (double)pip_scale);
				candles[candle_count].volume = volume;
				candle_count += 1u;
				cursor += consumed;
			}
			while (*cursor == '\r' || *cursor == '\n') {
				cursor += 1;
			}
			if (consumed == 0) {
				break;
			}
		}
		if (candle_count == 0u) {
			free(candles);
			send_json(cfd, 400, "{\"error\":\"empty candle batch\"}");
			return;
		}
			if (live_store_upsert_candles_ex(symbol,
					timeframe,
					pip_scale,
					candles,
				candle_count,
				recompute,
				&changed_count,
				&summary,
				err,
				sizeof(err)) != 0) {
			free(candles);
			snprintf(json, sizeof(json), "{\"error\":\"live candle batch failed\",\"detail\":\"%s\"}", err);
				send_json(cfd, 500, json);
				return;
			}
			snprintf(json,
				sizeof(json),
				"candles=%u changed=%u recompute=%d first_ts=%d last_ts=%d",
				candle_count,
				changed_count,
				recompute,
				(int)candles[0].ts_epoch,
				(int)candles[candle_count - 1u].ts_epoch);
			print_live_feed_event("live_feed_batch", symbol, timeframe, json);
			free(candles);
			if (encode_live_single_summary_json(json, sizeof(json), &summary) < 0) {
				send_json(cfd, 500, "{\"error\":\"encode failed\"}");
				return;
			}
		{
			size_t used = strlen(json);

			if (used > 0u && json[used - 1u] == '}') {
				snprintf(json + used - 1u,
					sizeof(json) - used + 1u,
					",\"batch_input\":%u,\"batch_changed\":%u}",
					candle_count,
					changed_count);
			}
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/live/recompute") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		LiveStreamSummary summary;
		char err[256];

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0) {
			send_json(cfd, 400, "{\"error\":\"missing symbol or timeframe\"}");
			return;
		}
		if (live_store_recompute(symbol, timeframe, &summary, err, sizeof(err)) != 0) {
			snprintf(json, sizeof(json), "{\"error\":\"live recompute failed\",\"detail\":\"%s\"}", err);
			send_json(cfd, 500, json);
			return;
		}
		print_live_feed_event("live_feed_recompute", symbol, timeframe, "");
		if (encode_live_single_summary_json(json, sizeof(json), &summary) < 0) {
			send_json(cfd, 500, "{\"error\":\"encode failed\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/live/candles") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		uint32_t dataset_id;
		uint32_t start;
		uint32_t count;

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0 ||
			live_store_get_dataset_id(symbol, timeframe, &dataset_id) != 0) {
			send_json(cfd, 404, "{\"error\":\"live stream not found\"}");
			return;
		}
		start = (uint32_t)query_get_u32(query, "start", 0);
		count = (uint32_t)query_get_u32(query, "count", 512);
		n = json_encode_candle_window(dataset_id, start, count, json, sizeof(json));
		if (n < 0) {
			send_json(cfd, 404, "{\"error\":\"live candles not found\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/live/waves") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		char class_name[64];
		char order_by[64];
		const char *class_filter = "all";
		const char *order_by_value = "";
		uint32_t dataset_id;
		uint32_t start;
		uint32_t count;

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0 ||
			live_store_get_dataset_id(symbol, timeframe, &dataset_id) != 0) {
			send_json(cfd, 404, "{\"error\":\"live stream not found\"}");
			return;
		}
		start = (uint32_t)query_get_u32(query, "start", 0);
		count = (uint32_t)query_get_u32(query, "count", 256);
		if (query_get_str(query, "class_name", class_name, sizeof(class_name)) == 0) {
			class_filter = class_name;
		}
		if (query_get_str(query, "order_by", order_by, sizeof(order_by)) == 0) {
			order_by_value = order_by;
		}
		n = json_encode_wave_list(dataset_id, start, count, class_filter, order_by_value, json, sizeof(json));
		if (n < 0) {
			send_json(cfd, 404, "{\"error\":\"live waves not found\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/live/snapshot") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];
		char *summary_json;
		char *candles_json;
		char *waves_json;
		LiveStreamSummary summary;
		uint32_t candle_count;
		uint32_t wave_count;
		uint32_t candle_start = 0u;
		uint32_t wave_start = 0u;
		int summary_n;
		int candles_n;
		int waves_n;

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0 ||
			live_store_get_summary(symbol, timeframe, &summary) != 0) {
			send_json(cfd, 404, "{\"error\":\"live stream not found\"}");
			return;
		}
		candle_count = (uint32_t)query_get_u32(query, "candle_count", 512);
		wave_count = (uint32_t)query_get_u32(query, "wave_count", 128);
		if (summary.candle_count > candle_count) {
			candle_start = summary.candle_count - candle_count;
		}
		if (summary.wave_count > wave_count) {
			wave_start = summary.wave_count - wave_count;
		}
		summary_json = (char *)malloc(RES_BUF_SZ);
		candles_json = (char *)malloc(RES_BUF_SZ);
		waves_json = (char *)malloc(RES_BUF_SZ);
		if (summary_json == NULL || candles_json == NULL || waves_json == NULL) {
			free(summary_json);
			free(candles_json);
			free(waves_json);
			send_json(cfd, 500, "{\"error\":\"snapshot allocation failed\"}");
			return;
		}
		summary_n = encode_live_single_summary_json(summary_json, RES_BUF_SZ, &summary);
		candles_n = json_encode_candle_window(summary.dataset_id, candle_start, candle_count, candles_json, RES_BUF_SZ);
		if (summary.wave_count == 0u) {
			waves_n = snprintf(waves_json,
				RES_BUF_SZ,
				"{\"dataset_id\":%u,\"start\":0,\"selected_class\":\"all\",\"count\":0,\"total\":0,\"waves\":[]}",
				summary.dataset_id);
			if (waves_n < 0 || (size_t)waves_n >= RES_BUF_SZ) {
				waves_n = -1;
			}
		} else {
			waves_n = json_encode_wave_list(summary.dataset_id, wave_start, wave_count, "all", "", waves_json, RES_BUF_SZ);
		}
		if (summary_n < 0 || candles_n < 0 || waves_n < 0) {
			free(summary_json);
			free(candles_json);
			free(waves_json);
			send_json(cfd, 500, "{\"error\":\"snapshot encode failed\"}");
			return;
		}
		n = snprintf(json,
			sizeof(json),
			"{\"summary\":%s,\"candles\":%s,\"waves\":%s}",
			summary_json,
			candles_json,
			waves_json);
		free(summary_json);
		free(candles_json);
		free(waves_json);
		if (n < 0 || (size_t)n >= sizeof(json)) {
			send_json(cfd, 500, "{\"error\":\"snapshot too large\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/live/reset") == 0) {
		char symbol[LIVE_SYMBOL_MAX];
		char timeframe[LIVE_TIMEFRAME_MAX];

		if (query_get_str(query, "symbol", symbol, sizeof(symbol)) != 0 ||
			query_get_str(query, "timeframe", timeframe, sizeof(timeframe)) != 0) {
			send_json(cfd, 400, "{\"error\":\"missing symbol or timeframe\"}");
			return;
		}
		if (live_store_reset(symbol, timeframe) != 0) {
			send_json(cfd, 500, "{\"error\":\"live reset failed\"}");
			return;
		}
		send_json(cfd, 200, "{\"status\":\"reset\"}");
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/datasets") == 0) {
		n = json_encode_dataset_list(json, sizeof(json));
		if (n < 0) {
			send_json(cfd, 500, "{\"error\":\"encode failed\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/datasets/import") == 0) {
		char csv_path[768];
		LoadOptions opt;
		uint32_t id;

		if (query_get_str(query, "path", csv_path, sizeof(csv_path)) != 0) {
			send_json(cfd, 400, "{\"error\":\"missing query path\"}");
			return;
		}
		opt.pip_scale = query_get_u32(query, "pip_scale", 10000);
		opt.sort_on_load = 1;
		opt.dedupe_timestamps = 1;
		if (core_import_mt4_csv(csv_path, &opt, &id) != 0) {
			send_json(cfd, 500, "{\"error\":\"import failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"dataset_id\":%u}", id);
		send_json(cfd, 200, json);
		return;
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/character-clustering") == 0) {
		uint32_t dataset_id;
		uint64_t job_id;
		GraphParams p;

		dataset_id = (uint32_t)query_get_u32(query, "dataset_id", 0);
		p.q = (uint32_t)query_get_u32(query, "q", 4);
		p.distance_mode = query_get_distance_mode(query, DISTANCE_MODE_FEATURE_AWARE);
		p.delta = (int32_t)query_get_u32(query, "delta", 4);
		p.theta = (uint32_t)query_get_u32(query, "theta", 4);
		p.mirrored_mode = query_get_u32(query, "mirrored", 0) ? 1 : 0;
		p.weighted = query_get_u32(query, "weighted", 1) ? 1 : 0;
		p.delta_body_gate = (int32_t)query_get_u32(query, "delta_body_gate", 8);
		p.delta_range_gate = (int32_t)query_get_u32(query, "delta_range_gate", 8);
		p.delta_bullcount_gate = (int32_t)query_get_u32(query, "delta_bullcount_gate", 2);
		p.delta_signchange_gate = (int32_t)query_get_u32(query, "delta_signchange_gate", 2);
		p.delta_upper_wick_gate = (int32_t)query_get_u32(query, "delta_upper_wick_gate", 8);
		p.delta_lower_wick_gate = (int32_t)query_get_u32(query, "delta_lower_wick_gate", 8);
		p.delta_lower_ratio_gate = (int32_t)query_get_u32(query, "delta_lower_ratio_gate", 25);
		p.delta_upper_ratio_gate = (int32_t)query_get_u32(query, "delta_upper_ratio_gate", 25);
		p.delta_total_wick_ratio_gate = (int32_t)query_get_u32(query, "delta_total_wick_ratio_gate", 30);
		p.k_neighbors = (uint32_t)query_get_u32(query, "k_neighbors", 8);
		p.use_mutual_knn = query_get_u32(query, "use_mutual_knn", 1) ? 1u : 0u;
		p.max_neighbor_distance = query_get_f32(query, "max_neighbor_distance", 0.0f);
		p.cluster_mode = query_get_graph_cluster_mode(query, GRAPH_CLUSTER_MODE_LABEL_PROPAGATION);
		p.body_mode = (int32_t)query_get_u32(query, "body_mode", 0);
		p.require_same_majority_sign = query_get_u32(query, "require_same_majority_sign", 0) ? 1u : 0u;
		p.use_upper = query_get_u32(query, "use_upper", 1) ? 1u : 0u;
		p.use_lower = query_get_u32(query, "use_lower", 1) ? 1u : 0u;
		p.use_range = query_get_u32(query, "use_range", 1) ? 1u : 0u;
		p.use_body = query_get_u32(query, "use_body", 1) ? 1u : 0u;
		p.use_composition = query_get_u32(query, "use_composition", 1) ? 1u : 0u;
		p.w_upper = query_get_f32(query, "w_upper", 1.0f);
		p.w_lower = query_get_f32(query, "w_lower", 1.0f);
		p.w_body = query_get_f32(query, "w_body", 1.0f);
		p.w_range = query_get_f32(query, "w_range", 1.0f);
		p.body_sign_penalty = query_get_f32(query, "body_sign_penalty", 2.0f);
		p.w_bullcount = query_get_f32(query, "w_bullcount", 1.0f);
		p.w_signchanges = query_get_f32(query, "w_signchanges", 1.0f);
		p.w_runbull = query_get_f32(query, "w_runbull", 1.0f);
		p.w_runbear = query_get_f32(query, "w_runbear", 1.0f);

		if (dataset_id == 0 || core_run_graph_clustering(dataset_id, &p, &job_id) != 0) {
			send_json(cfd, 500, "{\"error\":\"job create failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
		send_json(cfd, 200, json);
		return;
	}
	if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/kmedoids") == 0) {
		uint32_t dataset_id;
		uint64_t job_id;
		KMedoidsParams p;

		dataset_id = (uint32_t)query_get_u32(query, "dataset_id", 0);
		p.q = (uint32_t)query_get_u32(query, "q", 4);
		p.delta = (int32_t)query_get_u32(query, "delta", 4);
		p.k = (uint32_t)query_get_u32(query, "k", 8);
		p.max_iter = (uint32_t)query_get_u32(query, "max_iter", 20);
		p.restarts = (uint32_t)query_get_u32(query, "restarts", 1);
		p.mirrored_mode = query_get_u32(query, "mirrored", 0) ? 1 : 0;
		p.weighted = query_get_u32(query, "weighted", 0) ? 1 : 0;
		p.w_upper = query_get_f32(query, "w_upper", 1.0f);
		p.w_lower = query_get_f32(query, "w_lower", 1.0f);
		p.w_body = query_get_f32(query, "w_body", 1.0f);
		p.w_range = query_get_f32(query, "w_range", 1.0f);
		if (dataset_id == 0 || core_run_kmedoids(dataset_id, &p, &job_id) != 0) {
			send_json(cfd, 500, "{\"error\":\"job create failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
		send_json(cfd, 200, json);
		return;
	}
	if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/dbscan") == 0) {
		uint32_t dataset_id;
		uint64_t job_id;
		DBSCANParams p;

		dataset_id = (uint32_t)query_get_u32(query, "dataset_id", 0);
		p.q = (uint32_t)query_get_u32(query, "q", 4);
		p.delta = (int32_t)query_get_u32(query, "delta", 4);
		p.eps = (uint32_t)query_get_u32(query, "eps", 4);
		p.min_pts = (uint32_t)query_get_u32(query, "min_pts", 4);
		p.mirrored_mode = query_get_u32(query, "mirrored", 0) ? 1 : 0;
		p.weighted = query_get_u32(query, "weighted", 0) ? 1 : 0;
		p.w_upper = query_get_f32(query, "w_upper", 1.0f);
		p.w_lower = query_get_f32(query, "w_lower", 1.0f);
		p.w_body = query_get_f32(query, "w_body", 1.0f);
		p.w_range = query_get_f32(query, "w_range", 1.0f);
		if (dataset_id == 0 || core_run_dbscan(dataset_id, &p, &job_id) != 0) {
			send_json(cfd, 500, "{\"error\":\"job create failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
		send_json(cfd, 200, json);
		return;
	}
	if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/word-mining") == 0) {
		uint32_t dataset_id;
		uint64_t job_id;
		WordParams p;

		dataset_id = (uint32_t)query_get_u32(query, "dataset_id", 0);
		p.min_len = (uint32_t)query_get_u32(query, "min_len", 2);
		p.max_len = (uint32_t)query_get_u32(query, "max_len", 12);
		p.use_rle = query_get_u32(query, "use_rle", 0) ? 1 : 0;
		if (dataset_id == 0 || core_run_word_mining(dataset_id, &p, &job_id) != 0) {
			send_json(cfd, 500, "{\"error\":\"job create failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
		send_json(cfd, 200, json);
		return;
	}
	if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/phrase-mining") == 0) {
		uint32_t dataset_id;
		uint64_t job_id;
		PhraseParams p;

		dataset_id = (uint32_t)query_get_u32(query, "dataset_id", 0);
		p.min_len = (uint32_t)query_get_u32(query, "min_len", 2);
		p.max_len = (uint32_t)query_get_u32(query, "max_len", 8);
		p.use_rle = query_get_u32(query, "use_rle", 1) ? 1 : 0;
		if (dataset_id == 0 || core_run_phrase_mining(dataset_id, &p, &job_id) != 0) {
			send_json(cfd, 500, "{\"error\":\"job create failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
		send_json(cfd, 200, json);
		return;
	}
		if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/wave-inference") == 0) {
		uint32_t dataset_id;
		uint64_t job_id;
		WaveParams p;

		memset(&p, 0, sizeof(p));
		dataset_id = (uint32_t)query_get_u32(query, "dataset_id", 0);
		p.wave_model = (int32_t)query_get_wave_model(query, WAVE_MODEL_LEGACY);
		p.lmin_candles = (uint32_t)query_get_u32(query, "lmin_candles", 1);
		p.lmax_candles = (uint32_t)query_get_u32(query, "lmax_candles", 60);
		p.regime_context_window = (uint32_t)query_get_u32(query, "regime_context_window", 512);
		p.regime_context_mode = (uint32_t)query_get_u32(query, "regime_context_mode", WAVE_CONTEXT_TRAILING);
		p.min_wave_move_pips = (int32_t)query_get_u32(query, "min_wave_move_pips", 0);
		p.minimum_pips_for_trend = (int32_t)query_get_u32(query, "minimum_pips_for_trend", 7);
		p.norm_scale = (int32_t)query_get_u32(query, "norm_scale", 1000);
		p.context_range_floor = (int32_t)query_get_u32(query, "context_range_floor", 1);
		p.epsilon = (int32_t)query_get_u32(query, "epsilon", 1);
		p.segment_penalty_lambda = (int32_t)query_get_u32(query, "segment_penalty_lambda", 220);
		p.weak_fit_threshold = (int32_t)query_get_u32(query, "weak_fit_threshold", 140);
		p.boundary_low_conf_threshold = (int32_t)query_get_u32(query, "boundary_low_conf_threshold", 30);
		p.trend_priority_mode = (int32_t)query_get_u32(query, "trend_priority_mode", 1);
		p.retrace_break_threshold = (int32_t)query_get_u32(query, "retrace_break_threshold", 200);
		p.trend_split_gain_min = (int32_t)query_get_u32(query, "trend_split_gain_min", 80);
		p.compression_min_duration = (int32_t)query_get_u32(query, "compression_min_duration", 4);
		p.expansion_min_duration = (int32_t)query_get_u32(query, "expansion_min_duration", 4);
		p.v_reversal_min_trend_strength = (int32_t)query_get_u32(query, "v_reversal_min_trend_strength", 420);
		p.v_reversal_max_bridge_chop = (int32_t)query_get_u32(query, "v_reversal_max_bridge_chop", 520);
		p.v_reversal_max_duration = (int32_t)query_get_u32(query, "v_reversal_max_duration", 12);
		p.relative_trend_retracement_max = (int32_t)query_get_u32(query, "relative_trend_retracement_max", 300);
		p.relative_strong_trend_retracement_max = (int32_t)query_get_u32(query, "relative_strong_trend_retracement_max", 180);
		p.relative_trend_der_step_min = (int32_t)query_get_u32(query, "relative_trend_der_step_min", 550);
		p.relative_trend_der_body_min = (int32_t)query_get_u32(query, "relative_trend_der_body_min", 500);
		p.relative_trend_body_eff_min = (int32_t)query_get_u32(query, "relative_trend_body_eff_min", 450);
		p.relative_trend_overlap_max = (int32_t)query_get_u32(query, "relative_trend_overlap_max", 350);
		p.relative_trend_sign_flip_max = (int32_t)query_get_u32(query, "relative_trend_sign_flip_max", 450);
		p.relative_trend_wick_bias_min = (int32_t)query_get_u32(query, "relative_trend_wick_bias_min", 1050);
		p.relative_range_der_step_max = (int32_t)query_get_u32(query, "relative_range_der_step_max", 500);
		p.relative_range_body_eff_max = (int32_t)query_get_u32(query, "relative_range_body_eff_max", 500);
		p.relative_range_overlap_min = (int32_t)query_get_u32(query, "relative_range_overlap_min", 350);
		p.relative_range_sign_flip_min = (int32_t)query_get_u32(query, "relative_range_sign_flip_min", 400);
		if (dataset_id == 0 || core_run_wave_inference(dataset_id, &p, &job_id) != 0) {
			send_json(cfd, 500, "{\"error\":\"job create failed\"}");
			return;
		}
		snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/micro-harvest") == 0) {
			uint32_t dataset_id;
			uint64_t job_id;
			MicroHarvestSearchConfig p;
			char bias_text[16];

			memset(&p, 0, sizeof(p));
			dataset_id = (uint32_t)json_body_get_i32(body, "dataset_id", (int32_t)query_get_u32(query, "dataset_id", 0));
			if (json_body_get_str(body, "bias", bias_text, sizeof(bias_text)) != 0) {
				if (query_get_str(query, "bias", bias_text, sizeof(bias_text)) != 0) {
					snprintf(bias_text, sizeof(bias_text), "%s", "BUY");
				}
			}
			p.bias = (strcmp(bias_text, "SELL") == 0) ? MH_BIAS_SELL : MH_BIAS_BUY;
			p.lookback_days = json_body_get_i32(body, "lookback_days", 14);
			p.spread_pips = json_body_get_i32(body, "spread_pips", 50);
			p.max_orders_min = json_body_get_i32(body, "max_orders_min", 1);
			p.max_orders_max = json_body_get_i32(body, "max_orders_max", 4);
			p.spacing_min_pips = json_body_get_i32(body, "spacing_min_pips", 5);
			p.spacing_max_pips = json_body_get_i32(body, "spacing_max_pips", 25);
			p.spacing_step_pips = json_body_get_i32(body, "spacing_step_pips", 5);
			p.take_profit_min_pips = json_body_get_i32(body, "take_profit_min_pips", 50);
			p.take_profit_max_pips = json_body_get_i32(body, "take_profit_max_pips", 200);
			p.take_profit_step_pips = json_body_get_i32(body, "take_profit_step_pips", 10);
			p.entry_edge_min = json_body_get_i32(body, "entry_edge_min", 600);
			p.entry_edge_max = json_body_get_i32(body, "entry_edge_max", 900);
			p.entry_edge_step = json_body_get_i32(body, "entry_edge_step", 50);
			p.min_island_height_pips = json_body_get_i32(body, "min_island_height_pips", 300);
			p.max_island_height_pips = json_body_get_i32(body, "max_island_height_pips", 10000);
			if (dataset_id == 0 || core_run_micro_harvest(dataset_id, &p, &job_id) != 0) {
				send_json(cfd, 500, "{\"error\":\"micro harvest job create failed\"}");
				return;
			}
			snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "POST") == 0 && strcmp(path, "/jobs/micro-harvest-replay") == 0) {
			uint32_t dataset_id = 0;
			uint64_t job_id;
			MicroHarvestReplayConfig p;
			LoadOptions load_opt;
			char bias_text[16];

			memset(&p, 0, sizeof(p));
			if (json_body_get_str(body, "bias", bias_text, sizeof(bias_text)) != 0) {
				if (query_get_str(query, "bias", bias_text, sizeof(bias_text)) != 0) {
					snprintf(bias_text, sizeof(bias_text), "%s", "BUY");
				}
			}
			if (json_body_get_str(body, "replay_path", p.replay_path, sizeof(p.replay_path)) != 0) {
				query_get_str(query, "replay_path", p.replay_path, sizeof(p.replay_path));
			}
			memset(&load_opt, 0, sizeof(load_opt));
			load_opt.pip_scale = json_body_get_i32(body, "pip_scale", (int32_t)query_get_u32(query, "pip_scale", 10000));
			load_opt.sort_on_load = 1;
			load_opt.dedupe_timestamps = 1;
			p.wave_params.wave_model = (int32_t)json_body_get_i32(body, "wave_model", (int32_t)query_get_wave_model(query, WAVE_MODEL_LAYERED));
			p.wave_params.lmin_candles = (uint32_t)json_body_get_i32(body, "lmin_candles", (int32_t)query_get_u32(query, "lmin_candles", 1));
			p.wave_params.lmax_candles = (uint32_t)json_body_get_i32(body, "lmax_candles", (int32_t)query_get_u32(query, "lmax_candles", 60));
			p.wave_params.regime_context_window = (uint32_t)json_body_get_i32(body, "regime_context_window", (int32_t)query_get_u32(query, "regime_context_window", 512));
			p.wave_params.regime_context_mode = (uint32_t)json_body_get_i32(body, "regime_context_mode", (int32_t)query_get_u32(query, "regime_context_mode", WAVE_CONTEXT_TRAILING));
			p.wave_params.min_wave_move_pips = json_body_get_i32(body, "min_wave_move_pips", (int32_t)query_get_u32(query, "min_wave_move_pips", 0));
			p.wave_params.minimum_pips_for_trend = json_body_get_i32(body, "minimum_pips_for_trend", (int32_t)query_get_u32(query, "minimum_pips_for_trend", 7));
			p.wave_params.norm_scale = json_body_get_i32(body, "norm_scale", (int32_t)query_get_u32(query, "norm_scale", 1000));
			p.wave_params.context_range_floor = json_body_get_i32(body, "context_range_floor", (int32_t)query_get_u32(query, "context_range_floor", 1));
			p.wave_params.epsilon = json_body_get_i32(body, "epsilon", (int32_t)query_get_u32(query, "epsilon", 1));
			p.wave_params.segment_penalty_lambda = json_body_get_i32(body, "segment_penalty_lambda", (int32_t)query_get_u32(query, "segment_penalty_lambda", 220));
			p.wave_params.weak_fit_threshold = json_body_get_i32(body, "weak_fit_threshold", (int32_t)query_get_u32(query, "weak_fit_threshold", 140));
			p.wave_params.boundary_low_conf_threshold = json_body_get_i32(body, "boundary_low_conf_threshold", (int32_t)query_get_u32(query, "boundary_low_conf_threshold", 30));
			p.wave_params.trend_priority_mode = json_body_get_i32(body, "trend_priority_mode", (int32_t)query_get_u32(query, "trend_priority_mode", 1));
			p.wave_params.retrace_break_threshold = json_body_get_i32(body, "retrace_break_threshold", (int32_t)query_get_u32(query, "retrace_break_threshold", 200));
			p.wave_params.trend_split_gain_min = json_body_get_i32(body, "trend_split_gain_min", (int32_t)query_get_u32(query, "trend_split_gain_min", 80));
			p.wave_params.compression_min_duration = json_body_get_i32(body, "compression_min_duration", (int32_t)query_get_u32(query, "compression_min_duration", 4));
			p.wave_params.expansion_min_duration = json_body_get_i32(body, "expansion_min_duration", (int32_t)query_get_u32(query, "expansion_min_duration", 4));
			p.wave_params.v_reversal_min_trend_strength = json_body_get_i32(body, "v_reversal_min_trend_strength", (int32_t)query_get_u32(query, "v_reversal_min_trend_strength", 420));
			p.wave_params.v_reversal_max_bridge_chop = json_body_get_i32(body, "v_reversal_max_bridge_chop", (int32_t)query_get_u32(query, "v_reversal_max_bridge_chop", 520));
			p.wave_params.v_reversal_max_duration = json_body_get_i32(body, "v_reversal_max_duration", (int32_t)query_get_u32(query, "v_reversal_max_duration", 12));
			p.wave_params.relative_trend_retracement_max = json_body_get_i32(body, "relative_trend_retracement_max", (int32_t)query_get_u32(query, "relative_trend_retracement_max", 300));
			p.wave_params.relative_strong_trend_retracement_max = json_body_get_i32(body, "relative_strong_trend_retracement_max", (int32_t)query_get_u32(query, "relative_strong_trend_retracement_max", 180));
			p.wave_params.relative_trend_der_step_min = json_body_get_i32(body, "relative_trend_der_step_min", (int32_t)query_get_u32(query, "relative_trend_der_step_min", 550));
			p.wave_params.relative_trend_der_body_min = json_body_get_i32(body, "relative_trend_der_body_min", (int32_t)query_get_u32(query, "relative_trend_der_body_min", 500));
			p.wave_params.relative_trend_body_eff_min = json_body_get_i32(body, "relative_trend_body_eff_min", (int32_t)query_get_u32(query, "relative_trend_body_eff_min", 450));
			p.wave_params.relative_trend_overlap_max = json_body_get_i32(body, "relative_trend_overlap_max", (int32_t)query_get_u32(query, "relative_trend_overlap_max", 350));
			p.wave_params.relative_trend_sign_flip_max = json_body_get_i32(body, "relative_trend_sign_flip_max", (int32_t)query_get_u32(query, "relative_trend_sign_flip_max", 450));
			p.wave_params.relative_trend_wick_bias_min = json_body_get_i32(body, "relative_trend_wick_bias_min", (int32_t)query_get_u32(query, "relative_trend_wick_bias_min", 1050));
			p.wave_params.relative_range_der_step_max = json_body_get_i32(body, "relative_range_der_step_max", (int32_t)query_get_u32(query, "relative_range_der_step_max", 500));
			p.wave_params.relative_range_body_eff_max = json_body_get_i32(body, "relative_range_body_eff_max", (int32_t)query_get_u32(query, "relative_range_body_eff_max", 500));
			p.wave_params.relative_range_overlap_min = json_body_get_i32(body, "relative_range_overlap_min", (int32_t)query_get_u32(query, "relative_range_overlap_min", 350));
			p.wave_params.relative_range_sign_flip_min = json_body_get_i32(body, "relative_range_sign_flip_min", (int32_t)query_get_u32(query, "relative_range_sign_flip_min", 400));
			p.search.bias = (strcmp(bias_text, "SELL") == 0) ? MH_BIAS_SELL : MH_BIAS_BUY;
			p.search.lookback_days = json_body_get_i32(body, "lookback_days", 14);
			p.search.spread_pips = json_body_get_i32(body, "spread_pips", 50);
			p.search.max_orders_min = json_body_get_i32(body, "max_orders_min", 1);
			p.search.max_orders_max = json_body_get_i32(body, "max_orders_max", 4);
			p.search.spacing_min_pips = json_body_get_i32(body, "spacing_min_pips", 5);
			p.search.spacing_max_pips = json_body_get_i32(body, "spacing_max_pips", 25);
			p.search.spacing_step_pips = json_body_get_i32(body, "spacing_step_pips", 5);
			p.search.take_profit_min_pips = json_body_get_i32(body, "take_profit_min_pips", 50);
			p.search.take_profit_max_pips = json_body_get_i32(body, "take_profit_max_pips", 200);
			p.search.take_profit_step_pips = json_body_get_i32(body, "take_profit_step_pips", 10);
			p.search.entry_edge_min = json_body_get_i32(body, "entry_edge_min", 600);
			p.search.entry_edge_max = json_body_get_i32(body, "entry_edge_max", 900);
			p.search.entry_edge_step = json_body_get_i32(body, "entry_edge_step", 50);
			p.search.min_island_height_pips = json_body_get_i32(body, "min_island_height_pips", 300);
			p.search.max_island_height_pips = json_body_get_i32(body, "max_island_height_pips", 10000);
			if (p.replay_path[0] == '\0' ||
				core_import_mt4_csv(p.replay_path, &load_opt, &dataset_id) != 0 ||
				core_build_candle_features(dataset_id) != 0 ||
				core_run_micro_harvest_replay(dataset_id, &p, &job_id) != 0) {
				send_json(cfd, 500, "{\"error\":\"micro harvest replay job create failed\"}");
				return;
			}
			snprintf(json, sizeof(json), "{\"job_id\":%llu}", (unsigned long long)job_id);
			send_json(cfd, 200, json);
			return;
		}

		if (strcmp(method, "GET") == 0 && strcmp(path, "/jobs") == 0) {
		n = json_encode_job_list(json, sizeof(json));
		if (n < 0) {
			send_json(cfd, 500, "{\"error\":\"encode failed\"}");
			return;
		}
		send_json(cfd, 200, json);
		return;
	}

	if (strncmp(path, "/jobs/", 6) == 0) {
		unsigned long long job_id = 0;
		if (strcmp(method, "GET") == 0 && sscanf(path, "/jobs/%llu", &job_id) == 1) {
			n = json_encode_job_detail((uint64_t)job_id, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"job not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "POST") == 0 && sscanf(path, "/jobs/%llu/cancel", &job_id) == 1) {
			if (core_cancel_job((uint64_t)job_id) != 0) {
				send_json(cfd, 500, "{\"error\":\"cancel failed\"}");
				return;
			}
			send_json(cfd, 200, "{\"status\":\"cancel_requested\"}");
			return;
		}
		if (strcmp(method, "POST") == 0 && sscanf(path, "/jobs/%llu/delete", &job_id) == 1) {
			if (core_delete_job((uint64_t)job_id) != 0) {
				send_json(cfd, 500, "{\"error\":\"delete failed\"}");
				return;
			}
			send_json(cfd, 200, "{\"status\":\"deleted\"}");
			return;
		}
	}

	if (strncmp(path, "/datasets/", 10) == 0) {
		unsigned int dataset_id = 0;
		int matched = 0;
			if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/candles%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			uint32_t start = (uint32_t)query_get_u32(query, "start", 0);
			uint32_t count = (uint32_t)query_get_u32(query, "count", 512);
			n = json_encode_candle_window(dataset_id, start, count, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"candles not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/features%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			uint32_t start = (uint32_t)query_get_u32(query, "start", 0);
			uint32_t count = (uint32_t)query_get_u32(query, "count", 512);
			n = json_encode_feature_window(dataset_id, start, count, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"features not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/clusters%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			n = json_encode_cluster_list(dataset_id, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"clusters not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		{
			uint32_t cluster_id;

			if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/clusters/%u/members%n", &dataset_id, &cluster_id, &matched) == 2 && path[matched] == '\0') {
				uint32_t start = (uint32_t)query_get_u32(query, "start", 0);
				uint32_t count = (uint32_t)query_get_u32(query, "count", 256);

				n = json_encode_cluster_member_list(dataset_id, cluster_id, start, count, json, sizeof(json));
				if (n < 0) {
					send_json(cfd, 404, "{\"error\":\"cluster members not found\"}");
					return;
				}
				send_json(cfd, 200, json);
				return;
			}
			if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/micro-harvest/latest%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
				n = json_encode_micro_harvest_latest(dataset_id, json, sizeof(json));
				if (n < 0) {
					send_json(cfd, 404, "{\"error\":\"micro harvest result not found\"}");
					return;
				}
				send_json(cfd, 200, json);
				return;
			}
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/words%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			n = json_encode_word_list(dataset_id, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"words not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/phrases%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			n = json_encode_phrase_list(dataset_id, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"phrases not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/waves%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			uint32_t start = (uint32_t)query_get_u32(query, "start", 0);
			uint32_t count = (uint32_t)query_get_u32(query, "count", 256);
			char class_name[64];
			char order_by[64];
			const char *class_filter = "all";
			const char *order_by_value = "";

			if (query_get_str(query, "class_name", class_name, sizeof(class_name)) == 0) {
				class_filter = class_name;
			}
			if (query_get_str(query, "order_by", order_by, sizeof(order_by)) == 0) {
				order_by_value = order_by;
			}
			n = json_encode_wave_list(dataset_id, start, count, class_filter, order_by_value, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"waves not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/wave_predictor%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			uint32_t anchor_token = (uint32_t)query_get_u32(query, "anchor_token", 0);
			uint32_t input_len = (uint32_t)query_get_u32(query, "input_len", 3);
			uint32_t horizon_value = (uint32_t)query_get_u32(query, "horizon", 4);
			int with_eval = query_get_u32(query, "with_eval", 0);

			n = wave_predictor_encode_json(dataset_id,
				anchor_token,
				input_len,
				horizon_value,
				with_eval,
				json,
				sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"wave predictor unavailable\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u/wave_predictor_base_probs%n", &dataset_id, &matched) == 1 && path[matched] == '\0') {
			n = wave_predictor_encode_base_probs_json(dataset_id, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"wave predictor base probabilities unavailable\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
		if (strcmp(method, "GET") == 0 && sscanf(path, "/datasets/%u", &dataset_id) == 1) {
			char tail;
			if (sscanf(path, "/datasets/%u%c", &dataset_id, &tail) != 1) {
				send_json(cfd, 404, "{\"error\":\"not found\"}");
				return;
			}
			n = json_encode_dataset_detail(dataset_id, json, sizeof(json));
			if (n < 0) {
				send_json(cfd, 404, "{\"error\":\"dataset not found\"}");
				return;
			}
			send_json(cfd, 200, json);
			return;
		}
	}

	send_json(cfd, 404, "{\"error\":\"not found\"}");
#undef send_json
}

int core_http_serve(const char *bind_ip, uint16_t port)
{
	int sfd;
	struct sockaddr_in addr;
	int opt = 1;

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		return -1;
	}
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (bind_ip == NULL || bind_ip[0] == '\0') {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	} else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
		close(sfd);
		return -2;
	}
	if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(sfd);
		return -3;
	}
	if (listen(sfd, 64) != 0) {
		close(sfd);
		return -4;
	}
	trainer_log_info("http server accept loop started bind_ip=%s port=%u",
		(bind_ip != NULL && bind_ip[0] != '\0') ? bind_ip : "0.0.0.0",
		(unsigned int)port);
	for (;;) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int cfd;

		memset(&client_addr, 0, sizeof(client_addr));
		cfd = accept(sfd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (cfd < 0) {
			if (errno == EINTR) {
				continue;
			}
			trainer_log_error("http accept failed errno=%d", errno);
			break;
		}
		handle_client(cfd, inet_ntoa(client_addr.sin_addr), (uint16_t)ntohs(client_addr.sin_port));
		close(cfd);
	}
	close(sfd);
	return 0;
}
