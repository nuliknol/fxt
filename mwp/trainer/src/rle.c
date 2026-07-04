#include "rle.h"
#include "internal_store.h"

#include <stdio.h>
#include <stdlib.h>

static void set_err(char *err, unsigned long err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
}

int rle_build(uint32_t dataset_id, char *err, unsigned long err_sz)
{
	DatasetRecord *rec;
	uint32_t i;
	uint32_t run_start;
	uint32_t out_n;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		set_err(err, err_sz, "dataset not found");
		return -1;
	}
	if (rec->symbol_stream == NULL || rec->symbol_count == 0) {
		set_err(err, err_sz, "symbol stream not available");
		return -2;
	}
	free(rec->rle_tokens);
	rec->rle_tokens = (RLEToken *)malloc((size_t)rec->symbol_count * sizeof(RLEToken));
	if (rec->rle_tokens == NULL) {
		set_err(err, err_sz, "allocation failed");
		return -3;
	}

	out_n = 0;
	run_start = 0;
	for (i = 1; i <= rec->symbol_count; ++i) {
		if (i < rec->symbol_count && rec->symbol_stream[i] == rec->symbol_stream[run_start]) {
			continue;
		}
		rec->rle_tokens[out_n].symbol_id = rec->symbol_stream[run_start];
		rec->rle_tokens[out_n].run_len = i - run_start;
		rec->rle_tokens[out_n].start_char_index = run_start;
		out_n += 1;
		run_start = i;
	}
	rec->rle_count = out_n;
	set_err(err, err_sz, "");
	return 0;
}
