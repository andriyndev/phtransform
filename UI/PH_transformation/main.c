#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include <CL/cl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#define CONFIG_CHART

#ifdef CONFIG_CHART
#include "ph_transform_chart.h"
#else
#include "ph_transform.h"
#endif

#ifndef RESULTS_IN_BLOCK
#define RESULTS_IN_BLOCK 10000
#endif

typedef cl_long matrix_el_t;

#pragma pack(push, 1)
struct MAParams {
	cl_ulong h;
	cl_ulong w;
	cl_ulong w_allocated;
	cl_ulong total_allocated;
};
#pragma pack(pop)

struct MatrixArray {
	struct MAParams params;
	cl_mem arr_buf;
};

struct ShiftsArray {
	cl_ulong size;
	cl_ulong allocated;
	cl_mem arr_buf;
	cl_ulong total_shift;
};

struct Result {
	cl_long arr[RESULTS_IN_BLOCK];
	size_t used_size;
	struct Result *next;
};

#ifdef CONFIG_CHART
struct ChartTempArray {
	size_t size;
	size_t allocated;
	cl_ulong *arr;
	cl_mem arr_buf;
};

struct ChartTempArray chart_temp_array;
#endif

size_t total_results = 0;
struct Result results_list_begin;
struct Result *results_list_current = &results_list_begin;

//cl_device_id *devices;
cl_context context;
cl_command_queue command_queue;
cl_program program;
int initialized = 0;
struct MatrixArray matrix1;
struct MatrixArray matrix2;

cl_kernel check_for_zero_cols_kernel;
cl_kernel check_for_zero_diags_kernel;
cl_kernel calc_cols_kernel;
cl_kernel calc_diags_kernel;
cl_kernel calc_chart_block_kernel;
cl_kernel calc_shifts_kernel;
cl_kernel get_trailing_padding_kernel;

cl_mem trailing_padding_buffer;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

/* convert the kernel file into a string */
char *read_to_string(const char *filename)
{
	char *str = NULL;
	cl_ulong file_size;
	FILE *f;
	int res;
	size_t read_elements;

	f = fopen(filename, "rb");
	if (f == NULL) {
		fprintf(stderr, "Failed to open %s\n", filename);
		return NULL;
	}

	res = fseek(f, 0, SEEK_END);
	if (res != 0) {
		goto close_file;
	}
	file_size = (cl_ulong)ftell(f);
	if (file_size == -1L) {
		goto close_file;
	}
	res = fseek(f, 0, SEEK_SET);
	if (res != 0) {
		goto close_file;
	}
	str = (char *)malloc((file_size + 1) * sizeof(*str));
	if (str == NULL) {
		goto close_file;
	}

	read_elements = fread(str, file_size, 1, f);
	if (read_elements != 1) {
		free(str);
		str = NULL;
		goto close_file;
	}
	str[file_size] = '\0';
close_file:
	fclose(f);
	return str;
}

static inline void swap_matrix_arrays(struct MatrixArray **matrix1, struct MatrixArray **matrix2)
{
	struct MatrixArray *temp;
	temp = *matrix1;
	*matrix1 = *matrix2;
	*matrix2 = temp;
}

static int realloc_shifts_array_if_needed(struct ShiftsArray *arr, cl_ulong size)
{
	struct ShiftsArray new_arr;

	if (arr->allocated < size) {
		new_arr.size = size;
		new_arr.allocated = new_arr.size * 12 / 10;
		new_arr.arr_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_ulong) * new_arr.allocated, NULL, NULL);
		if (new_arr.arr_buf == NULL) {
			return -1;
		}
		clReleaseMemObject(arr->arr_buf);
		arr->allocated = new_arr.allocated;
		arr->arr_buf = new_arr.arr_buf;
		arr->total_shift = 0;
	}

	arr->size = size;

	return 0;
}

static int realloc_matrix_array_if_needed(struct MatrixArray *matrix, cl_ulong h, cl_ulong w)
{
	struct MatrixArray new_matrix;

	if (matrix->params.total_allocated < h * w) {
		new_matrix.params.h = h;
		new_matrix.params.w = w;
		new_matrix.params.w_allocated = w;
		new_matrix.params.total_allocated = h * w * 12 / 10;
		new_matrix.arr_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(matrix_el_t) * new_matrix.params.total_allocated, NULL, NULL);
		if (new_matrix.arr_buf == NULL) {
			return -1;
		}
		clReleaseMemObject(matrix->arr_buf);
		matrix->params.total_allocated = new_matrix.params.total_allocated;
		matrix->arr_buf = new_matrix.arr_buf;
	}

	matrix->params.h = h;
	matrix->params.w = w;
	matrix->params.w_allocated = w;

	return 0;
}

static int calculate_shifts(struct ShiftsArray *shifts)
{
	cl_int status;

	status = clSetKernelArg(calc_shifts_kernel, 0, sizeof(cl_mem), (void *)&shifts->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clSetKernelArg(calc_shifts_kernel, 1, sizeof(cl_ulong), (void *)&shifts->size);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clEnqueueTask(command_queue, calc_shifts_kernel, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clEnqueueReadBuffer(command_queue, shifts->arr_buf, CL_TRUE, sizeof(cl_ulong) * (shifts->size - 1), sizeof(shifts->total_shift), &shifts->total_shift, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}

	return 0;
}

static int check_for_zero_cols(struct MatrixArray *matrix, struct ShiftsArray *shifts)
{
	cl_int status;
	size_t work_size[1];

	work_size[0] = shifts->size;

	status = clSetKernelArg(check_for_zero_cols_kernel, 0, sizeof(cl_mem), (void *)&matrix->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(check_for_zero_cols_kernel, 1, sizeof(cl_mem), (void *)&shifts->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(check_for_zero_cols_kernel, 2, sizeof(matrix->params), (void *)&matrix->params);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clEnqueueNDRangeKernel(command_queue, check_for_zero_cols_kernel, 1, NULL, work_size, NULL, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}

	return 0;
}

static int calculate_cols_shifts(struct MatrixArray *matrix, struct ShiftsArray *shifts)
{
	cl_int status;
	int res;

	res = check_for_zero_cols(matrix, shifts);
	if (res < 0) {
		return -1;
	}

	res = calculate_shifts(shifts);
	if (res < 0) {
		return -1;
	}

	return 0;
}

static int check_for_zero_diags(struct MatrixArray *matrix, struct ShiftsArray *shifts)
{
	cl_int status;
	size_t work_size[1];

	work_size[0] = shifts->size;

	status = clSetKernelArg(check_for_zero_diags_kernel, 0, sizeof(matrix->arr_buf), (void *)&matrix->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(check_for_zero_diags_kernel, 1, sizeof(shifts->arr_buf), (void *)&shifts->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(check_for_zero_diags_kernel, 2, sizeof(matrix->params), (void *)&matrix->params);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clEnqueueNDRangeKernel(command_queue, check_for_zero_diags_kernel, 1, NULL, work_size, NULL, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}

	return 0;
}

static int calculate_diagonal_shifts(struct MatrixArray *matrix, struct ShiftsArray *shifts)
{
	cl_int status;
	int res;

	res = check_for_zero_diags(matrix, shifts);
	if (res < 0) {
		return -1;
	}

	res = calculate_shifts(shifts);
	if (res < 0) {
		return -1;
	}

	return 0;
}

static int calculate_cols(struct MatrixArray *matrix_from, struct MatrixArray *matrix_to, struct ShiftsArray *shifts)
{
	cl_int status;
	size_t work_size[1];

	work_size[0] = shifts->size;

	status = clSetKernelArg(calc_cols_kernel, 0, sizeof(matrix_from->arr_buf), (void *)&matrix_from->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_cols_kernel, 1, sizeof(matrix_to->arr_buf), (void *)&matrix_to->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_cols_kernel, 2, sizeof(matrix_from->params), (void *)&matrix_from->params);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_cols_kernel, 3, sizeof(matrix_to->params), (void *)&matrix_to->params);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_cols_kernel, 4, sizeof(shifts->arr_buf), (void *)&shifts->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clEnqueueNDRangeKernel(command_queue, calc_cols_kernel, 1, NULL, work_size, NULL, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}

	return 0;
}

static int calculate_diagonals(struct MatrixArray *matrix_from, struct MatrixArray *matrix_to, struct ShiftsArray *shifts)
{
	cl_int status;
	size_t work_size[1];

	work_size[0] = shifts->size;

	status = clSetKernelArg(calc_diags_kernel, 0, sizeof(matrix_from->arr_buf), (void *)&matrix_from->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_diags_kernel, 1, sizeof(matrix_to->arr_buf), (void *)&matrix_to->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_diags_kernel, 2, sizeof(matrix_from->params), (void *)&matrix_from->params);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_diags_kernel, 3, sizeof(matrix_to->params), (void *)&matrix_to->params);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clSetKernelArg(calc_diags_kernel, 4, sizeof(shifts->arr_buf), (void *)&shifts->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clEnqueueNDRangeKernel(command_queue, calc_diags_kernel, 1, NULL, work_size, NULL, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}

	return 0;
}

static int insert_result(cl_long result)
{
	// If the current block is full, create a new one
	if (results_list_current->used_size == RESULTS_IN_BLOCK) {
		struct Result *new_block = malloc(sizeof(*new_block));
		if (new_block == NULL) {
			return -1;
		}
		new_block->used_size = 0;
		new_block->next = NULL;
		results_list_current->next = new_block;
		results_list_current = results_list_current->next;
	}

	results_list_current->arr[results_list_current->used_size] = result;
	results_list_current->used_size++;
	total_results++;

	return 0;
}

static inline void add_results_to_output_array(struct PH_Array *out_arr)
{
	int i = 0;
	struct Result *results_current = &results_list_begin;

	out_arr->size = total_results;
	while (i < total_results) {
		out_arr->array[i] = results_current->arr[i % RESULTS_IN_BLOCK];
		i++;
		if (i % RESULTS_IN_BLOCK == 0) {
			results_current = results_current->next;
		}
	}
}

static inline void clean_results_list(void)
{
	struct Result *current = results_list_begin.next;
	struct Result *next;

	results_list_begin.next = NULL;
	results_list_begin.used_size = 0;
	total_results = 0;
	results_list_current = &results_list_begin;

	while (current != NULL) {
		next = current->next;
		free(current);
		current = next;
	}
}

#ifdef CONFIG_CHART
static struct PH_Chart *alloc_and_set_first_chart_element(struct MatrixArray *matrix)
{
	struct PH_Chart *chart;
	size_t i;

	chart = malloc(sizeof(*chart) + sizeof(chart->array[0]) * matrix->params.h);
	if (chart == NULL) {
		return NULL;
	}
	chart->size = matrix->params.h;
	chart->next = NULL;
	for (i = 0; i < chart->size; i++) {
		chart->array[i] = matrix->params.w;
	}

	return chart;
}

static int realloc_chart_temp_array_if_needed(size_t size)
{
	struct ChartTempArray new_array;

	if (size <= chart_temp_array.allocated) {
		chart_temp_array.size = size;
		return 0;
	}
	new_array.size = size;
	new_array.allocated = size * 12 / 10;
	new_array.arr = malloc(sizeof(*new_array.arr) * new_array.allocated);
	if (new_array.arr == NULL) {
		return -1;
	}
	new_array.arr_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR, sizeof(*new_array.arr) * new_array.allocated, (void *)new_array.arr, NULL);
	if (new_array.arr_buf == NULL) {
		free(new_array.arr);
		return -1;
	}

	clReleaseMemObject(chart_temp_array.arr_buf);
	free(chart_temp_array.arr);

	chart_temp_array = new_array;

	return 0;
}

static int add_chart_element(struct PH_Chart *chart_current, struct MatrixArray *matrix)
{
	struct PH_Chart *chart;
	cl_int status;
	size_t work_size[1];
	int res;
	size_t i;

	chart = malloc(sizeof(*chart) + sizeof(chart->array[0]) * matrix->params.h);
	if (chart == NULL) {
		return -1;
	}
	chart->size = matrix->params.h;
	chart->next = NULL;

	res = realloc_chart_temp_array_if_needed(chart->size);
	if (res < 0) {
		goto err_free_chart;
	}

	work_size[0] = chart->size;

	status = clSetKernelArg(calc_chart_block_kernel, 0, sizeof(matrix->arr_buf), (void *)&matrix->arr_buf);
	if (status != CL_SUCCESS) {
		goto err_free_chart;
	}
	status = clSetKernelArg(calc_chart_block_kernel, 1, sizeof(chart_temp_array.arr_buf), (void *)&chart_temp_array.arr_buf);
	if (status != CL_SUCCESS) {
		goto err_free_chart;
	}
	status = clSetKernelArg(calc_chart_block_kernel, 2, sizeof(matrix->params), (void *)&matrix->params);
	if (status != CL_SUCCESS) {
		goto err_free_chart;
	}

	status = clEnqueueNDRangeKernel(command_queue, calc_chart_block_kernel, 1, NULL, work_size, NULL, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		goto err_free_chart;
	}
	status = clEnqueueReadBuffer(command_queue, chart_temp_array.arr_buf, CL_TRUE, 0, sizeof(*chart_temp_array.arr) * chart_temp_array.size, chart_temp_array.arr, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		goto err_free_chart;
	}

	for (i = 0; i < chart->size; i++) {
		chart->array[i] = chart_temp_array.arr[i];
	}

	chart_current->next = chart;

	return 0;
err_free_chart:
	free(chart);
	return -1;
}

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

int get_trailing_padding(struct ShiftsArray *shifts, cl_ulong *padding)
{
	cl_int status;

	status = clSetKernelArg(get_trailing_padding_kernel, 0, sizeof(cl_mem), (void *)&shifts->arr_buf);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clSetKernelArg(get_trailing_padding_kernel, 1, sizeof(cl_ulong), (void *)&shifts->size);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clSetKernelArg(get_trailing_padding_kernel, 2, sizeof(cl_mem), (void *)&trailing_padding_buffer);
	if (status != CL_SUCCESS) {
		return -1;
	}

	status = clEnqueueTask(command_queue, get_trailing_padding_kernel, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}
	status = clEnqueueReadBuffer(command_queue, trailing_padding_buffer, CL_TRUE, 0, sizeof(*padding), padding, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		return -1;
	}

	return 0;
}

int debug_write_matrix_array_to_file(const char *filename, struct MatrixArray *array, int space)
{
	FILE *f;
	int res;
	cl_int status;
	matrix_el_t *arr;
	int ret = 0;
	size_t i, j;

	f = fopen(filename, "w");
	if (f == NULL) {
		return -1;
	}

	arr = malloc(array->params.total_allocated * sizeof(matrix_el_t));
	if (arr == NULL) {
		ret = -1;
		goto close_file;
	}

	status = clEnqueueReadBuffer(command_queue, array->arr_buf, CL_TRUE, 0, array->params.total_allocated * sizeof(matrix_el_t), (void *)arr, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		ret = -1;
		goto free_arr;
	}

	for (j = 0; j < array->params.w; j++) {
		res = fprintf(f, "%*zu", space, j);
		if (res < 0) {
			ret = -1;
			goto free_arr;
		}
	}
	res = fprintf(f, "\n");
	if (res < 0) {
		ret = -1;
		goto free_arr;
	}
	for (i = 0; i < array->params.h; i++) {
		for (j = 0; j < array->params.w; j++) {
			res = fprintf(f, "%*"PRId64, space, arr[i * array->params.w_allocated + j]);
			if (res < 0) {
				ret = -1;
				goto free_arr;
			}
		}
		res = fprintf(f, "\n");
		if (res < 0) {
			ret = -1;
			goto free_arr;
		}
	}

free_arr:
	free(arr);
close_file:
	fclose(f);
	return ret;
}

int debug_write_shifts_array_to_file(const char *filename, struct ShiftsArray *shifts, int space)
{
	FILE *f;
	int res;
	cl_int status;
	cl_long *arr;
	int ret = 0;
	size_t i;

	f = fopen(filename, "w");
	if (f == NULL) {
		return -1;
	}

	arr = malloc(shifts->allocated * sizeof(cl_long));
	if (arr == NULL) {
		ret = -1;
		goto close_file;
	}

	status = clEnqueueReadBuffer(command_queue, shifts->arr_buf, CL_TRUE, 0, shifts->allocated * sizeof(cl_long), (void *)arr, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		ret = -1;
		goto free_arr;
	}

	for (i = 0; i < shifts->size; i++) {
		res = fprintf(f, "%*"PRId64, space, arr[i]);
		if (res < 0) {
			ret = -1;
			goto free_arr;
		}
	}

free_arr:
	free(arr);
close_file:
	fclose(f);
	return ret;
}

#ifdef CONFIG_CHART
struct PH_Array *ph_transform_calculate_with_chart(struct PH_Matrix *matrix, struct PH_Chart **chart)
#else
struct PH_Array *ph_transform_calculate(struct PH_Matrix *matrix)
#endif
{
	cl_int status;
	struct MatrixArray *matrix_to, *matrix_from;
	struct ShiftsArray shifts_array;
	size_t work_size[1];
	int res;
	struct PH_Array *results = NULL;
	matrix_el_t *matrix_array;
	matrix_el_t matrix_element;
	cl_ulong padding;
#ifdef CONFIG_CHART
	struct PH_Chart *chart_begin = NULL;
	struct PH_Chart *chart_current = NULL;
#endif

	results_list_current = &results_list_begin;

	if (matrix->height == 0 || matrix->width == 0 || !initialized) {
		return NULL;
	}

	matrix_from = &matrix1;
	matrix_to = &matrix2;

	matrix_from->params.h = matrix->height;
	matrix_from->params.w = matrix->width;
	matrix_from->params.w_allocated = matrix_from->params.w;
	matrix_from->params.total_allocated = matrix_from->params.h * matrix_from->params.w_allocated * 12 / 10;
	matrix_array = malloc(sizeof(*matrix_array) * matrix_from->params.total_allocated);
	if (matrix_array == NULL) {
		return NULL;
	}

	for (int i = 0; i < matrix->height; i++) {
		for (int j = 0; j < matrix->width; j++) {
			matrix_array[i * matrix_from->params.w_allocated + j] = matrix->array[i * matrix->width + j];
		}
	}

#ifdef CONFIG_CHART
	chart_begin = alloc_and_set_first_chart_element(matrix_from);
	if (chart_begin == NULL) {
		free(matrix_array);
		return NULL;
	}
	chart_current = chart_begin;
#endif

	matrix_from->arr_buf = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
		sizeof(matrix_el_t) * matrix_from->params.total_allocated, (void *)matrix_array, NULL);
	free(matrix_array);
	if (matrix_from->arr_buf == NULL) {
		goto clean_chart_buffer_if_enabled;
	}

	shifts_array.size = matrix_from->params.w;
	shifts_array.allocated = shifts_array.size * 12 / 10;
	shifts_array.arr_buf = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_ulong) * shifts_array.allocated, NULL, NULL);
	if (shifts_array.arr_buf == NULL) {
		goto release_matrix_from_arr_buf;
	}

	res = calculate_cols_shifts(matrix_from, &shifts_array);
	if (res < 0) {
		goto release_shifts_array_buf;
	}

	matrix_to->params.h = matrix->width - shifts_array.total_shift;
	matrix_to->params.w = matrix->height;
	matrix_to->params.w_allocated = matrix_to->params.w;
	matrix_to->params.total_allocated = matrix_to->params.h * matrix_to->params.w_allocated * 12 / 10;
	if (matrix_to->params.h == 0) {
		matrix_to->params.total_allocated = 1;
	}

	matrix_to->arr_buf = clCreateBuffer(context, CL_MEM_READ_WRITE,
		sizeof(matrix_el_t) * matrix_to->params.total_allocated, NULL, NULL);
	if (matrix_to->arr_buf == NULL) {
		goto release_shifts_array_buf;
	}

	if (matrix_to->params.h == 0) {
		res = insert_result(0);
		if (res < 0) {
			goto release_matrix_to_array_buf;
		}
		goto add_results_to_output_array;
	}
	
	res = calculate_cols(matrix_from, matrix_to, &shifts_array);
	if (res < 0) {
		goto clean_results_list;
	}

	res = realloc_shifts_array_if_needed(&shifts_array, matrix_to->params.w);
	if (res < 0) {
		goto clean_results_list;
	}

	res = check_for_zero_cols(matrix_to, &shifts_array);
	if (res < 0) {
		goto clean_results_list;
	}

	get_trailing_padding(&shifts_array, &padding);
	matrix_to->params.w -= padding;

	swap_matrix_arrays(&matrix_from, &matrix_to);

#ifdef CONFIG_CHART
	chart_temp_array.size = matrix_from->params.h;
	chart_temp_array.allocated = chart_temp_array.size * 12 / 10;
	chart_temp_array.arr = malloc(sizeof(*chart_temp_array.arr) * chart_temp_array.allocated);
	if (chart_temp_array.arr == NULL) {
		goto clean_results_list;
	}
	chart_temp_array.arr_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR, sizeof(*chart_temp_array.arr) * chart_temp_array.allocated, (void *)chart_temp_array.arr, NULL);
	if (chart_temp_array.arr_buf == NULL) {
		goto free_chart_temp_array_if_enabled;
	}
#endif

	while (1) {

#ifdef CONFIG_CHART
		res = add_chart_element(chart_current, matrix_from);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}
		chart_current = chart_current->next;
#endif

		// Read the first element only
		status = clEnqueueReadBuffer(command_queue, matrix_from->arr_buf, CL_TRUE, 0, sizeof(matrix_el_t), &matrix_element, 0, NULL, NULL);
		if (status != CL_SUCCESS) {
			goto release_chart_temp_buffer_if_enabled;
		}
		res = insert_result(matrix_element);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}
		matrix_element = 0;
		status = clEnqueueWriteBuffer(command_queue, matrix_from->arr_buf, CL_TRUE, 0, sizeof(matrix_el_t), &matrix_element, 0, NULL, NULL);
		if (status != CL_SUCCESS) {
			goto release_chart_temp_buffer_if_enabled;
		}

		res = realloc_shifts_array_if_needed(&shifts_array, matrix_from->params.h + matrix_from->params.w - 1);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}
		
		res = calculate_diagonal_shifts(matrix_from, &shifts_array);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}
		if (matrix_from->params.h + matrix_from->params.w - 1 - shifts_array.total_shift == 0) {
			break;
		}

		res = realloc_matrix_array_if_needed(matrix_to,
			matrix_from->params.h + matrix_from->params.w - 1 - shifts_array.total_shift,
			(matrix_from->params.h < matrix_from->params.w) ? matrix_from->params.h : matrix_from->params.w);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}

		res = calculate_diagonals(matrix_from, matrix_to, &shifts_array);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}

		res = realloc_shifts_array_if_needed(&shifts_array, matrix_to->params.w);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}

		res = check_for_zero_cols(matrix_to, &shifts_array);
		if (res < 0) {
			goto release_chart_temp_buffer_if_enabled;
		}

		/*for (int i = matrix_to->params.w_allocated - 1; i >= 0; i--) {
			if (shifts_array.arr[i] == 1) {
				matrix_to->params.w--;
			} else {
				break;
			}
		}*/

		get_trailing_padding(&shifts_array, &padding);
		matrix_to->params.w -= padding;

		swap_matrix_arrays(&matrix_from, &matrix_to);
	}

add_results_to_output_array:
	results = malloc(sizeof(*results) + sizeof(results->array[0]) * total_results);
	if (results != NULL) {
		add_results_to_output_array(results);
	}

#ifdef CONFIG_CHART
	*chart = chart_begin;
#endif

release_chart_temp_buffer_if_enabled:
#ifdef CONFIG_CHART
	clReleaseMemObject(chart_temp_array.arr_buf);
#endif
free_chart_temp_array_if_enabled:
#ifdef CONFIG_CHART
	free(chart_temp_array.arr);
#endif
clean_results_list:
	clean_results_list();
release_matrix_to_array_buf:
	clReleaseMemObject(matrix_to->arr_buf);
release_shifts_array_buf:
	clReleaseMemObject(shifts_array.arr_buf);
release_matrix_from_arr_buf:
	clReleaseMemObject(matrix_from->arr_buf);
clean_chart_buffer_if_enabled:
#ifdef CONFIG_CHART
	if (results == NULL) {
		ph_transform_free_chart(chart_begin);
	}
#endif

	return results;
}

void ph_transform_free_result_mem(struct PH_Array *array)
{
	free(array);
}

int ph_transform_get_available_platforms(ph_t_platforms *platforms, uint32_t *size)
{
	cl_int status;
	cl_uint num_platforms;

	status = clGetPlatformIDs(0, NULL, &num_platforms);
	if (status != CL_SUCCESS || num_platforms == 0)
	{
		return -1;
	}

	cl_platform_id *_platforms = malloc(num_platforms * sizeof(*_platforms));
	if (_platforms == NULL) {
		return -1;
	}
	status = clGetPlatformIDs(num_platforms, _platforms, NULL);
	if (status != CL_SUCCESS) {
		free(_platforms);
		return -1;
	}

	*platforms = (ph_t_platforms)_platforms;
	*size = num_platforms;

	return 0;
}

int ph_transform_get_platform_name(ph_t_platforms platform, size_t i, char **name)
{
	char *str;
	cl_int status;
	size_t _size;
	cl_platform_id *_platform;

	_platform = platform;

	status = clGetPlatformInfo(_platform[i], CL_PLATFORM_NAME, 0, NULL, &_size);
	if (status != CL_SUCCESS) {
		return -1;
	}

	str = malloc(sizeof(*str) * _size);
	if (str == NULL) {
		return -1;
	}

	status = clGetPlatformInfo(_platform[i], CL_PLATFORM_NAME, _size, str, NULL);
	if (status != CL_SUCCESS) {
		free(str);
		return -1;
	}

	*name = str;

	return 0;
}

void ph_transform_free_platform_name(char *name)
{
	free(name);
}

void ph_transform_free_available_platforms_list(ph_t_platforms platforms)
{
	free(platforms);
}

int ph_transform_get_available_devices(ph_t_platforms platforms, uint32_t i, ph_t_devices *devices, uint32_t *size)
{
	cl_uint numDevices = 0;
	cl_int status;
	cl_device_id *_devices;
	cl_platform_id *_platforms;

	_platforms = platforms;

	status = clGetDeviceIDs(_platforms[i], CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices);
	if (status != CL_SUCCESS || numDevices == 0)
	{
		status = clGetDeviceIDs(_platforms[i], CL_DEVICE_TYPE_CPU, 0, NULL, &numDevices);
		if (status != CL_SUCCESS) {
			return -1;
		}
		_devices = (cl_device_id*)malloc(numDevices * sizeof(cl_device_id));
		if (_devices == NULL) {
			return -1;
		}
		status = clGetDeviceIDs(_platforms[i], CL_DEVICE_TYPE_CPU, numDevices, _devices, NULL);
		if (status != CL_SUCCESS) {
			free(_devices);
			return -1;
		}
	}
	else
	{
		_devices = (cl_device_id*)malloc(numDevices * sizeof(cl_device_id));
		if (_devices == NULL) {
			return -1;
		}
		status = clGetDeviceIDs(_platforms[i], CL_DEVICE_TYPE_GPU, numDevices, _devices, NULL);
		if (status != CL_SUCCESS) {
			free(_devices);
			return -1;
		}
	}

	*devices = (ph_t_devices)_devices;
	*size = numDevices;

	return 0;
}

int ph_transform_get_device_name(ph_t_devices devices, size_t i, char **name)
{
	cl_int status;
	size_t _size;
	char *str;
	cl_device_id *_devices;

	_devices = devices;

	status = clGetDeviceInfo(_devices[i], CL_DEVICE_NAME, 0, NULL, &_size);
	if (status != CL_SUCCESS) {
		return -1;
	}

	str = malloc(sizeof(*str) * _size);
	if (str == NULL) {
		return -1;
	}

	status = clGetDeviceInfo(_devices[i], CL_DEVICE_NAME, _size, str, NULL);
	if (status != CL_SUCCESS) {
		free(str);
		return -1;
	}

	*name = str;

	return 0;
}

void ph_transform_free_device_name(char *name)
{
	free(name);
}

void ph_transform_free_available_devices_list(ph_t_devices devices)
{
	free(devices);
}

int ph_transform_init(ph_t_devices devices, size_t device_num)
{
	cl_int status;
	cl_platform_id platform;
	cl_uint numDevices = 0;
	char *source;
	size_t source_size[1];
	cl_uint num_platforms;
	cl_device_id *_devices;

	_devices = devices;

	context = clCreateContext(NULL, 1, _devices, NULL, NULL, NULL);
	if (context == NULL) {
		return -1;
	}

	command_queue = clCreateCommandQueue(context, _devices[device_num], 0, NULL);
	if (command_queue == NULL) {
		goto release_context;
	}

	source = read_to_string("kernel.cl");
	if (source == NULL) {
		goto release_command_queue;
	}
	source_size[0] = strlen(source);
	program = clCreateProgramWithSource(context, 1, &source, source_size, NULL);
	if (program == NULL) {
		goto free_source;
	}

	status = clBuildProgram(program, 1, _devices, NULL, NULL, NULL);
	if (status != CL_SUCCESS) {
		goto release_program;
	}

	check_for_zero_cols_kernel = clCreateKernel(program, "check_for_zero_cols", NULL);
	if (check_for_zero_cols_kernel == NULL) {
		goto release_program;
	}
	check_for_zero_diags_kernel = clCreateKernel(program, "check_for_zero_diags", NULL);
	if (check_for_zero_diags_kernel == NULL) {
		goto release_check_for_zero_cols_kernel;
	}
	calc_cols_kernel = clCreateKernel(program, "calc_cols", NULL);
	if (calc_cols_kernel == NULL) {
		goto release_check_for_zero_diags_kernel;
	}
	calc_diags_kernel = clCreateKernel(program, "calc_diags", NULL);
	if (calc_diags_kernel == NULL) {
		goto release_calc_cols_kernel;
	}
	calc_shifts_kernel = clCreateKernel(program, "calc_shifts", NULL);
	if (calc_shifts_kernel == NULL) {
		goto release_calc_diags_kernel;
	}
	get_trailing_padding_kernel = clCreateKernel(program, "get_trailing_padding", NULL);
	if (get_trailing_padding_kernel == NULL) {
		goto release_calc_shifts_kernel;
	}

#ifdef CONFIG_CHART
	calc_chart_block_kernel = clCreateKernel(program, "calc_chart_block", NULL);
	if (calc_chart_block_kernel == NULL) {
		goto release_get_trailing_padding_kernel;
	}
#endif

	trailing_padding_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(cl_ulong), NULL, NULL);
	if (trailing_padding_buffer == NULL) {
		goto release_calc_chart_block_kernel_if_enabled;
	}

	free(source);
	initialized = 1;

	return 0;

release_calc_chart_block_kernel_if_enabled:
#ifdef CONFIG_CHART
	clReleaseKernel(calc_chart_block_kernel);
#endif
release_get_trailing_padding_kernel:
	clReleaseKernel(get_trailing_padding_kernel);
release_calc_shifts_kernel:
	clReleaseKernel(calc_shifts_kernel);
release_calc_diags_kernel:
	clReleaseKernel(calc_diags_kernel);
release_calc_cols_kernel:
	clReleaseKernel(calc_cols_kernel);
release_check_for_zero_diags_kernel:
	clReleaseKernel(check_for_zero_diags_kernel);
release_check_for_zero_cols_kernel:
	clReleaseKernel(check_for_zero_cols_kernel);
release_program:
	clReleaseProgram(program);
free_source:
	free(source);
release_command_queue:
	clReleaseCommandQueue(command_queue);
release_context:
	clReleaseContext(context);
	return -1;
}

void ph_transform_fini(void)
{
	clReleaseMemObject(trailing_padding_buffer);
#ifdef CONFIG_CHART
	clReleaseKernel(calc_chart_block_kernel);
#endif
	clReleaseKernel(get_trailing_padding_kernel);
	clReleaseKernel(calc_shifts_kernel);
	clReleaseKernel(calc_diags_kernel);
	clReleaseKernel(calc_cols_kernel);
	clReleaseKernel(check_for_zero_diags_kernel);
	clReleaseKernel(check_for_zero_cols_kernel);
	clReleaseProgram(program);
	clReleaseCommandQueue(command_queue);
	clReleaseContext(context);

	initialized = 0;
}