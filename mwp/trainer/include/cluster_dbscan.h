#ifndef CLUSTER_DBSCAN_H
#define CLUSTER_DBSCAN_H

#include "api_types.h"

int cluster_dbscan_run(uint32_t dataset_id, const DBSCANParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz);

#endif
