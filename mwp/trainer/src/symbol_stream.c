#include "symbol_stream.h"
#include "internal_store.h"

#include <stdio.h>
#include <stdlib.h>

static void set_err(char *err, unsigned long err_sz, const char *msg)
{
	if (err != NULL && err_sz > 0) {
		snprintf(err, err_sz, "%s", msg);
	}
}

int symbol_stream_build(uint32_t dataset_id, char *err, unsigned long err_sz)
{
	DatasetRecord *rec;
	uint32_t i;

	rec = store_find_dataset(dataset_id);
	if (rec == NULL) {
		set_err(err, err_sz, "dataset not found");
		return -1;
	}
	if (rec->clustering.cluster_ids == NULL || rec->chars.n_blocks == 0) {
		set_err(err, err_sz, "clustering not available");
		return -2;
	}
	free(rec->symbol_stream);
	rec->symbol_stream = (int32_t *)malloc((size_t)rec->chars.n_blocks * sizeof(int32_t));
	if (rec->symbol_stream == NULL) {
		set_err(err, err_sz, "allocation failed");
		return -3;
	}
	for (i = 0; i < rec->chars.n_blocks; ++i) {
		rec->symbol_stream[i] = rec->clustering.cluster_ids[i];
	}
	rec->symbol_count = rec->chars.n_blocks;
	set_err(err, err_sz, "");
	return 0;
}
