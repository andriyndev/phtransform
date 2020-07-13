#pragma once

#include <CL/cl.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "__ph_transform_common.h"

struct PH_Array *ph_transform_calculate(struct PH_Matrix *matrix);

#ifdef __cplusplus
}
#endif