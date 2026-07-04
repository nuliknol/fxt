#ifndef WORD_MINING_H
#define WORD_MINING_H

#include "api_types.h"

int word_mining_run(uint32_t dataset_id, const WordParams *params, volatile float *progress, volatile int *cancel_flag, char *err, unsigned long err_sz);

#endif
