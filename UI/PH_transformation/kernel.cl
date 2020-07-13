__kernel void helloworld(__global int* in, __global int* out)
{
	int num1 = get_global_id(0);
	int num2 = get_global_id(1);
	out[num1*5+num2] = in[num1*5+num2] + 1;
}

struct MAParams {
	ulong h;
	ulong w;
	ulong w_allocated;
	ulong total_allocated;
} __attribute__((packed));

__kernel void check_for_zero_cols(__global long *in, __global long *out, struct MAParams params)
{
	int cur_col = get_global_id(0);

	out[cur_col] = 1;
	for (int i = 0; i < params.h; i++) {
		if (in[i * params.w_allocated + cur_col] != 0) {
			out[cur_col] = 0;
			break;
		}
	}
}

__kernel void check_for_zero_diags(__global long *in, __global long *out, struct MAParams params)
{
	int cur_col = get_global_id(0);
	int begX, begY;
	int i, j;
	
	out[cur_col] = 1;

	if (cur_col < params.w - 1) {
		begX = cur_col;
		begY = 0;
	} else {
		begX = params.w - 1;
		begY = cur_col + 1 - params.w;
	}

	for (i = begX, j = begY; i != -1 && j < params.h; i--, j++) {
		if (in[j * params.w_allocated + i] != 0) {
			out[cur_col] = 0;
			break;
		}
	}
}

__kernel void calc_cols(__global long *in, __global long *out,
		struct MAParams params_in, struct MAParams params_out, __global int *shift)
{
	int cur_col = get_global_id(0);
	int cur_el = 0;
	int i, j;
	long min_el;
	long temp;

	if (cur_col < shift[cur_col] || (cur_col != 0 && shift[cur_col] != shift[cur_col - 1])) {
		return;
	}

	for (i = 0; i < params_in.h; i++) {
		for (j = i + 1; j < params_in.h; j++) {
			if (in[i * params_in.w_allocated + cur_col] > in[j * params_in.w_allocated + cur_col] || (in[i * params_in.w_allocated + cur_col] == 0 && in[j * params_in.w_allocated + cur_col] != 0)) {
				temp = in[i * params_in.w_allocated + cur_col];
				in[i * params_in.w_allocated + cur_col] = in[j * params_in.w_allocated + cur_col];
				in[j * params_in.w_allocated + cur_col] = temp;
			}
		}
	}

	for (i = 0; i < params_in.h; i++) {
		if (in[i * params_in.w_allocated + cur_col] != 0) {
			min_el = in[i * params_in.w_allocated + cur_col];
			for (j = i; j < params_in.h; j++) {
				if (in[j * params_in.w_allocated + cur_col] == 0) {
					break;
				}
				in[j * params_in.w_allocated + cur_col] -= min_el;
			}
			out[(cur_col - shift[cur_col]) * params_out.w_allocated + cur_el] = min_el * (j - i);
			cur_el++;
		}
	}

	for (i = cur_el; i < params_out.w; i++) {
		out[(cur_col - shift[cur_col]) * params_out.w_allocated + i] = 0;
	}
}

__kernel void calc_diags(__global long *in, __global long *out,
		struct MAParams params_in, struct MAParams params_out, __global long *shift)
{
	int min_el;
	int cur_col = get_global_id(0);
	int cur_el = 0;
	int begX, begY;
	int i, k, j, l;
	long temp;

	if (cur_col < shift[cur_col] || (cur_col != 0 && shift[cur_col] != shift[cur_col - 1])) {
		return;
	}

	if (cur_col < params_in.w - 1) {
		begX = cur_col;
		begY = 0;
	} else {
		begX = params_in.w - 1;
		begY = cur_col + 1 - params_in.w;
	}

	for (i = begX, k = begY; i != -1 && k < params_in.h; i--, k++) {
		for (j = i - 1, l = k + 1; j != -1 && l < params_in.h; j--, l++) {
			if (in[k * params_in.w_allocated + i] > in[l * params_in.w_allocated + j] || (in[k * params_in.w_allocated + i] == 0 && in[l * params_in.w_allocated + j] != 0)) {
				temp = in[k * params_in.w_allocated + i];
				in[k * params_in.w_allocated + i] = in[l * params_in.w_allocated + j];
				in[l * params_in.w_allocated + j] = temp;
			}
		}
	}

	for (i = begX, k = begY; i != -1 && k < params_in.h; i--, k++) {
		if (in[k * params_in.w_allocated + i] != 0) {
			min_el = in[k * params_in.w_allocated + i];
			for (j = i, l = k; j != -1 && l < params_in.h; j--, l++) {
				if (in[l * params_in.w_allocated + j] == 0) {
					break;
				}
				in[l * params_in.w_allocated + j] -= min_el;
			}
			out[(cur_col - shift[cur_col]) * params_out.w_allocated + cur_el] = min_el * (l - k);
			cur_el++;
		}
	}

	/*while (1) {
		min_el = 0;
		not_zero = 0;
		for (i = begX, j = begY; i != -1 && j < params_in.h; i--, j++) {
			if (in[j * params_in.w_allocated + i] != 0 && (min_el == 0 || in[j * params_in.w_allocated + i] <= min_el)) {
				min_el = in[j * params_in.w_allocated + i];
			}
		}
		if (min_el == 0) {
			break;
		}
		for (i = begX, j = begY; i != -1 && j < params_in.h; i--, j++) {
			if (in[j * params_in.w_allocated + i] != 0) {
				not_zero++;
				in[j * params_in.w_allocated + i] -= min_el;
			}
		}
		out[(cur_col - shift[cur_col]) * params_out.w_allocated + cur_el] = min_el * not_zero;
		cur_el++;
	}*/

	for (i = cur_el; i < params_out.w; i++) {
		out[(cur_col - shift[cur_col]) * params_out.w_allocated + i] = 0;
	}
}

__kernel void calc_chart_block(__global long *in, __global ulong *out, struct MAParams params)
{
	int cur_col = get_global_id(0);
	long i;

	for (i = 0; i < params.w; i++) {
		if (in[cur_col * params.w_allocated + i] == 0) {
			out[cur_col] = i;
			return;
		}
	}

	out[cur_col] = params.w;
}