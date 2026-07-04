#include "word_mining.h"
#include "rle.h"
#include "symbol_stream.h"
#include "internal_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCAL_WORDS 200000

typedef struct {
	uint32_t start;
	uint32_t len;
	uint32_t freq;
} WordEntry;

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

int word_mining_run(uint32_t dataset_id, const WordParams *params, volatile float *progress, volatile int *cancel_flag, char *err, unsigned long err_sz)
{
	DatasetRecord *rec;
	WordParams p;
	const int32_t *stream;
	int32_t *rle_stream = NULL;
	uint32_t stream_n;
	WordEntry *entries;
	uint32_t entry_count = 0;
	uint32_t l;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		set_err(err, err_sz, "dataset not found");
		return -1;
	}
	if (params == NULL) {
		p.min_len = 2;
		p.max_len = 12;
		p.use_rle = 0;
		params = &p;
	}
	if (params->min_len < 2 || params->max_len < params->min_len) {
		set_err(err, err_sz, "invalid word lengths");
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

	entries = (WordEntry *)malloc((size_t)MAX_LOCAL_WORDS * sizeof(WordEntry));
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
			if (cancel_flag != NULL && *cancel_flag) {
				free(entries);
				free(rle_stream);
				set_err(err, err_sz, "cancelled");
				return -8;
			}
			for (e = 0; e < entry_count; ++e) {
				if (entries[e].len != l) {
					continue;
				}
				if (seq_equal(stream, entries[e].start, stream, i, l)) {
					entries[e].freq += 1;
					found = 1;
					break;
				}
			}
			if (!found && entry_count < MAX_LOCAL_WORDS) {
				entries[entry_count].start = i;
				entries[entry_count].len = l;
				entries[entry_count].freq = 1;
				entry_count += 1;
			}
		}
		if (progress != NULL) {
			*progress = 0.10f + 0.85f * ((float)(l - params->min_len + 1) / (float)(params->max_len - params->min_len + 1));
		}
	}

	free(rec->words);
	rec->words = (WordSummary *)malloc((size_t)entry_count * sizeof(WordSummary));
	if (rec->words == NULL && entry_count > 0) {
		free(entries);
		free(rle_stream);
		set_err(err, err_sz, "result allocation failed");
		return -9;
	}
	rec->word_count = 0;
	for (uint32_t i = 0; i < entry_count; ++i) {
		if (entries[i].freq < 2) {
			continue;
		}
		rec->words[rec->word_count].word_id = rec->word_count;
		rec->words[rec->word_count].length_chars = entries[i].len;
		rec->words[rec->word_count].frequency = entries[i].freq;
		rec->word_count += 1;
	}
	free(entries);
	free(rle_stream);
	if (progress != NULL) {
		*progress = 1.0f;
	}
	set_err(err, err_sz, "");
	return 0;
}
