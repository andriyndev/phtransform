#include <stddef.h>

struct PH_Array {
	size_t size;
	int64_t array[];
};

struct PH_Matrix {
	size_t height;
	size_t width;
	int64_t array[];
};

typedef void* ph_t_platforms;
typedef void* ph_t_devices;

void ph_transform_fini(void);
void ph_transform_free_available_devices_list(ph_t_devices *devices);
void ph_transform_free_available_platforms_list(ph_t_platforms platforms);
void ph_transform_free_device_name(char *name);
void ph_transform_free_platform_name(char *name);
void ph_transform_free_result_mem(struct PH_Array *array);
int ph_transform_get_available_devices(ph_t_platforms platforms, uint32_t i, ph_t_devices **devices, uint32_t *size);
int ph_transform_get_available_platforms(ph_t_platforms *platforms, uint32_t *size);
int ph_transform_get_device_name(ph_t_devices *device, size_t i, char **name);
int ph_transform_get_platform_name(ph_t_platforms platform, size_t i, char **name);
int ph_transform_init(ph_t_devices *devices, size_t device_num);