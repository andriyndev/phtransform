#pragma once

#include <CL/cl.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "__ph_transform_common.h"

struct PH_Chart {
	size_t size;
	struct PH_Chart *next;
	uint64_t array[];
};

struct PH_Array *ph_transform_calculate_with_chart(struct PH_Matrix *matrix, struct PH_Chart **chart);
void ph_transform_free_chart(struct PH_Chart *chart_begin);

#ifdef __cplusplus
}
#endif