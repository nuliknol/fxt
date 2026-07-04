#ifndef PHRASE_MINING_H
#define PHRASE_MINING_H

#include "api_types.h"

int phrase_mining_run(uint32_t dataset_id, const PhraseParams *params, volatile float *progress, volatile int *cancel_flag, char *err, unsigned long err_sz);

#endif
