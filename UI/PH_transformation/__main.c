#include <malloc.h>
#include <stdio.h>

#define CONFIG_CHART

#ifdef CONFIG_CHART
#include "ph_transform_chart.h"
#else
#include "ph_transform.h"
#endif

#ifndef RESULTS_IN_BLOCK
#define RESULTS_IN_BLOCK 100
#endif

struct MatrixArray {
	size_t h;
	size_t w;
	size_t w_allocated;
	size_t total_allocated;
	long long *arr;
};

struct Result {
	long long arr[RESULTS_IN_BLOCK];
	struct Result *next;
};

static inline void __ph_transform_clean_results_list(struct Result *results_begin)
{
	struct Result *results_current;
	struct Result *results_next;

	results_current = results_begin;
	while (results_current != NULL) {
		results_next = results_current->next;
		free(results_current);
		results_current = results_next;
	}
}

#ifdef CONFIG_CHART
void ph_transform_free_chart(struct PH_Chart *chart_begin)
{
	struct PH_Chart *chart_current = chart_begin;
	struct PH_Chart *chart_next;

	while (chart_current != NULL) {
		chart_next = chart_current->next;
		free(chart_current);
		chart_current = chart_next;
	}
}
#endif

#ifdef CONFIG_CHART
struct PH_Array *ph_transform_calculate_with_chart(struct PH_Matrix *matrix, struct PH_Chart **chart)
#else
struct PH_Array *ph_transform_calculate(struct PH_Matrix *matrix)
#endif
{
	size_t i, j, k;
	struct MatrixArray matrix_in;
	struct MatrixArray matrix_out;
	size_t realW;
	size_t realHDiff;
	size_t begX, begY;
	struct Result *results_begin, *results_current;
	size_t results_inserted = 0;
	struct PH_Array *results;
#ifdef CONFIG_CHART
	struct PH_Chart *chart_begin = NULL;
	struct PH_Chart *chart_current = NULL;
#endif

	matrix_in.h = matrix->width;
	matrix_in.w = matrix->height;
	matrix_in.w_allocated = matrix_in.w;
	matrix_in.total_allocated = matrix_in.h * matrix_in.w_allocated * 12 / 10;
	matrix_in.arr = malloc(sizeof(*matrix_in.arr) * matrix_in.total_allocated);
	if (matrix_in.arr == NULL) {
		return NULL;
	}
#ifdef CONFIG_CHART
	chart_begin = malloc(sizeof(*chart_begin) + sizeof(chart_begin->array[0]) * matrix->height);
	if (chart_begin == NULL) {
		free(matrix_in.arr);
		return NULL;
	}
	chart_begin->size = matrix->height;
	chart_begin->next = NULL;
	chart_current = chart_begin;
	for (i = 0; i < chart_current->size; i++) {
		chart_current->array[i] = matrix->width;
	}
	chart_current->next = malloc(sizeof(*chart_current->next) + sizeof(chart_current->next->array[0]) * matrix_in.h);
	if (chart_current->next == NULL) {
		ph_transform_free_chart(chart_begin);
		free(matrix_in.arr);
		return NULL;
	}
	chart_current->next->size = matrix_in.h;
	chart_current->next->next = NULL;
	chart_current = chart_current->next;
#endif
	realW = 0;
	realHDiff = 0;
	for (i = 0; i < matrix->width; i++) {
		size_t cur_el = 0;
		while (1) {
			size_t min_el = 0;
			size_t not_zero = 0;
			for (j = 0; j < matrix->height; j++) {
				if (matrix->array[j * matrix->width + i] != 0 && (min_el == 0 || matrix->array[j * matrix->width + i] <= min_el)) {
					min_el = matrix->array[j * matrix->width + i];
				}
			}
			if (min_el == 0) {
				break;
			}
			for (j = 0; j < matrix->height; j++) {
				if (matrix->array[j * matrix->width + i] != 0) {
					not_zero++;
					matrix->array[j * matrix->width + i] -= min_el;
				}
			}
			matrix_in.arr[(i - realHDiff) * matrix_in.w_allocated + cur_el] = min_el * not_zero;
			cur_el++;
		}
		if (realW < cur_el) {
			realW = cur_el;
		}
		if (cur_el != 0) {
			for (j = cur_el; j < matrix_in.w; j++) {
				matrix_in.arr[(i - realHDiff) * matrix_in.w_allocated + j] = 0;
			}
		} else {
			realHDiff++;
		}
#ifdef CONFIG_CHART
		if (cur_el != 0) {
			chart_current->array[i - realHDiff] = cur_el;
		} else {
			chart_current->size--;
		}
#endif
	}
	matrix_in.w = realW;
	matrix_in.h -= realHDiff;

	matrix_out = matrix_in;

	results_begin = malloc(sizeof(*results_begin));
	if (results_begin == NULL) {
#ifdef CONFIG_CHART
		ph_transform_free_chart(chart_begin);
#endif
		free(matrix_out.arr);
		return NULL;
	}
	results_begin->next = NULL;
	results_current = results_begin;
	results_current->arr[results_inserted % RESULTS_IN_BLOCK] = matrix_out.arr[0];
	results_inserted++;

	matrix_in.h = matrix_out.h + matrix_out.w - 2;
	matrix_in.w = matrix_out.h;
	matrix_in.w_allocated = matrix_in.w;
	matrix_in.total_allocated = matrix_in.h * matrix_in.w_allocated * 12 / 10;
	matrix_in.arr = malloc(sizeof(long long) * matrix_in.total_allocated);
	if (matrix_in.arr == NULL) {
#ifdef CONFIG_CHART
		ph_transform_free_chart(chart_begin);
#endif
		__ph_transform_clean_results_list(results_begin);
		free(matrix_out.arr);
		return NULL;
	}

	while (matrix_out.h != 1 || matrix_out.w != 1) {
		matrix_in.h = matrix_out.h + matrix_out.w - 2;
		matrix_in.w = matrix_out.h;
		matrix_in.w_allocated = matrix_in.w;
		if (matrix_in.h * matrix_in.w_allocated > matrix_in.total_allocated) {
			free(matrix_in.arr);
			matrix_in.total_allocated = matrix_in.h * matrix_in.w_allocated * 12 / 10;
			matrix_in.arr = malloc(sizeof(long long) * matrix_in.total_allocated);
			if (matrix_in.arr == NULL) {
#ifdef CONFIG_CHART
				ph_transform_free_chart(chart_begin);
#endif
				__ph_transform_clean_results_list(results_begin);
				free(matrix_out.arr);
				return NULL;
			}
		}
#ifdef CONFIG_CHART
		chart_current->next = malloc(sizeof(*chart_current->next) + sizeof(chart_current->next->array[0]) * matrix_in.h);
		if (chart_current->next == NULL) {
			ph_transform_free_chart(chart_begin);
			__ph_transform_clean_results_list(results_begin);
			free(matrix_out.arr);
			free(matrix_in.arr);
			return NULL;
		}
		chart_current->next->size = matrix_in.h;
		chart_current->next->next = NULL;
		chart_current = chart_current->next;
#endif
		realW = 0;
		realHDiff = 0;
		for (i = 1; i < matrix_out.w + matrix_out.h - 1; i++) {
			size_t cur_el = 0;
			if (i < matrix_out.w - 1) {
				begX = i;
				begY = 0;
			} else {
				begX = matrix_out.w - 1;
				begY = i + 1 - matrix_out.w;
			}
			while (1) {
				size_t min_el = 0;
				size_t not_zero = 0;
				for (j = begX, k = begY; j != -1 && k < matrix_out.h; j--, k++) {
					if (matrix_out.arr[k * matrix_out.w_allocated + j] != 0 && (min_el == 0 || matrix_out.arr[k * matrix_out.w_allocated + j] <= min_el)) {
						min_el = matrix_out.arr[k * matrix_out.w_allocated + j];
					}
				}
				if (min_el == 0) {
					break;
				}
				for (j = begX, k = begY; j != -1 && k < matrix_out.h; j--, k++) {
					if (matrix_out.arr[k * matrix_out.w_allocated + j] != 0) {
						not_zero++;
						matrix_out.arr[k * matrix_out.w_allocated + j] -= min_el;
					}
				}
				matrix_in.arr[(i - realHDiff - 1) * matrix_in.w_allocated + cur_el] = min_el * not_zero;
				cur_el++;
			}
			if (realW < cur_el) {
				realW = cur_el;
			}
			if (cur_el != 0) {
				for (j = cur_el; j < matrix_in.w; j++) {
					matrix_in.arr[(i - realHDiff - 1) * matrix_in.w_allocated + j] = 0;
				}
			} else {
				realHDiff++;
			}
#ifdef CONFIG_CHART
			if (cur_el != 0) {
				chart_current->array[i - realHDiff - 1] = cur_el;
			} else {
				chart_current->size--;
			}
#endif
		}
		matrix_in.w = realW;
		matrix_in.h -= realHDiff;

		free(matrix_out.arr);
		matrix_out = matrix_in;

		if (results_inserted % RESULTS_IN_BLOCK == 0) {
			results_current->next = malloc(sizeof(*results_current->next));
			if (results_current->next == NULL) {
#ifdef CONFIG_CHART
				ph_transform_free_chart(chart_begin);
#endif
				__ph_transform_clean_results_list(results_begin);
				free(matrix_out.arr);
				return NULL;
			}
			results_current->next->next = NULL;
			results_current = results_current->next;
		}
		results_current->arr[results_inserted % RESULTS_IN_BLOCK] = matrix_out.arr[0];
		results_inserted++;
	}
	free(matrix_out.arr);

	results = malloc(sizeof(*results) + sizeof(results->array[0]) * results_inserted);
	if (results == NULL) {
#ifdef CONFIG_CHART
		ph_transform_free_chart(chart_begin);
#endif
		__ph_transform_clean_results_list(results_begin);
		return NULL;
	}
	results_current = results_begin;
	i = 0;
	while (i < results_inserted) {
		results->array[i] = results_current->arr[i % RESULTS_IN_BLOCK];
		i++;
		if (i % RESULTS_IN_BLOCK == 0) {
			results_current = results_current->next;
		}
	}
	results->size = results_inserted;

	__ph_transform_clean_results_list(results_begin);

#ifdef CONFIG_CHART
	if (chart != NULL) {
		*chart = chart_begin;
	} else {
		ph_transform_free_chart(chart_begin);
	}
#endif

	return results;
}

void ph_transform_free_result_mem(struct PH_Array *array) 
{
	free(array);
}