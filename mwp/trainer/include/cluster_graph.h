#ifndef CLUSTER_GRAPH_H
#define CLUSTER_GRAPH_H

#include "api_types.h"

int cluster_graph_run(uint32_t dataset_id, const GraphParams *params, volatile float *progress, volatile int *cancel_flag, char *err, size_t err_sz);

#endif
