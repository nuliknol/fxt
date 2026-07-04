#include "phrase_mining.h"
#include "rle.h"
#include "symbol_stream.h"
#include "internal_store.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_LOCAL_PHRASES 200000

typedef struct {
	uint32_t start;
	uint32_t len;
	uint32_t freq;
	uint64_t dur_sum_chars;
	uint64_t dur_sum_candles;
} PhraseEntry;

static void set_err(char *err, unsigned long err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
}

static int seq_equal(const int32_t *a, uint32_t a_start, const int32_t *b, uint32_t b_start, uint32_t len)
{
	uint32_t i;
	for (i = 0; i < len; ++i) {
		if (a[a_start + i] != b[b_start + i]) {
			return 0;
		}
	}
	return 1;
}

int phrase_mining_run(uint32_t dataset_id, const PhraseParams *params, volatile float *progress, volatile int *cancel_flag, char *err, unsigned long err_sz)
{
	DatasetRecord *rec;
	PhraseParams p;
	const int32_t *stream;
	int32_t *rle_stream = NULL;
	uint32_t stream_n;
	PhraseEntry *entries;
	uint32_t entry_count = 0;
	uint32_t l;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		set_err(err, err_sz, "dataset not found");
		return -1;
	}
	if (params == NULL) {
		p.min_len = 2;
		p.max_len = 8;
		p.use_rle = 1;
		params = &p;
	}
	if (params->min_len < 2 || params->max_len < params->min_len) {
		set_err(err, err_sz, "invalid phrase lengths");
		return -2;
	}

	if (symbol_stream_build(dataset_id, err, err_sz) != 0) {
		return -3;
	}
	if (rle_build(dataset_id, err, err_sz) != 0) {
		return -4;
	}

	if (params->use_rle) {
		rle_stream = (int32_t *)malloc((size_t)rec->rle_count * sizeof(int32_t));
		if (rle_stream == NULL && rec->rle_count > 0) {
			set_err(err, err_sz, "allocation failed");
			return -5;
		}
		for (uint32_t i = 0; i < rec->rle_count; ++i) {
			rle_stream[i] = rec->rle_tokens[i].symbol_id;
		}
		stream = rle_stream;
		stream_n = rec->rle_count;
	} else {
		stream = rec->symbol_stream;
		stream_n = rec->symbol_count;
	}
	if (stream == NULL || stream_n == 0) {
		free(rle_stream);
		set_err(err, err_sz, "empty symbol stream");
		return -6;
	}

	entries = (PhraseEntry *)malloc((size_t)MAX_LOCAL_PHRASES * sizeof(PhraseEntry));
	if (entries == NULL) {
		free(rle_stream);
		set_err(err, err_sz, "allocation failed");
		return -7;
	}

	for (l = params->min_len; l <= params->max_len; ++l) {
		uint32_t i;
		if (l > stream_n) {
			continue;
		}
		for (i = 0; i + l <= stream_n; ++i) {
			uint32_t e;
			int found = 0;
			uint32_t dur_chars = 0;
			uint32_t dur_candles;

			if (cancel_flag != NULL && *cancel_flag) {
				free(entries);
				free(rle_stream);
				set_err(err, err_sz, "cancelled");
				return -8;
			}

			if (params->use_rle) {
				for (uint32_t t = 0; t < l; ++t) {
					dur_chars += rec->rle_tokens[i + t].run_len;
				}
			} else {
				dur_chars = l;
			}
			dur_candles = dur_chars + (rec->meta.q > 0 ? (rec->meta.q - 1) : 0);

			for (e = 0; e < entry_count; ++e) {
				if (entries[e].len != l) {
					continue;
				}
				if (seq_equal(stream, entries[e].start, stream, i, l)) {
					entries[e].freq += 1;
					entries[e].dur_sum_chars += dur_chars;
					entries[e].dur_sum_candles += dur_candles;
					found = 1;
					break;
				}
			}
			if (!found && entry_count < MAX_LOCAL_PHRASES) {
				entries[entry_count].start = i;
				entries[entry_count].len = l;
				entries[entry_count].freq = 1;
				entries[entry_count].dur_sum_chars = dur_chars;
				entries[entry_count].dur_sum_candles = dur_candles;
				entry_count += 1;
			}
		}
		if (progress != NULL) {
			*progress = 0.10f + 0.85f * ((float)(l - params->min_len + 1) / (float)(params->max_len - params->min_len + 1));
		}
	}

	free(rec->phrases);
	rec->phrases = (PhraseSummary *)malloc((size_t)entry_count * sizeof(PhraseSummary));
	if (rec->phrases == NULL && entry_count > 0) {
		free(entries);
		free(rle_stream);
		set_err(err, err_sz, "result allocation failed");
		return -9;
	}
	rec->phrase_count = 0;
	for (uint32_t i = 0; i < entry_count; ++i) {
		if (entries[i].freq < 2) {
			continue;
		}
		rec->phrases[rec->phrase_count].phrase_id = rec->phrase_count;
		rec->phrases[rec->phrase_count].token_len = entries[i].len;
		rec->phrases[rec->phrase_count].frequency = entries[i].freq;
		rec->phrases[rec->phrase_count].avg_duration_chars = (float)entries[i].dur_sum_chars / (float)entries[i].freq;
		rec->phrases[rec->phrase_count].avg_duration_candles = (float)entries[i].dur_sum_candles / (float)entries[i].freq;
		rec->phrase_count += 1;
	}

	free(entries);
	free(rle_stream);
	if (progress != NULL) {
		*progress = 1.0f;
	}
	set_err(err, err_sz, "");
	return 0;
}
