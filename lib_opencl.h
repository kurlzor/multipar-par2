#ifndef _OPENCL_H_
#define _OPENCL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_UNIT	256	// 128 が最小値、JIT SSE2 の ALTMAP 対応で 256 にする
#define GPU_MAX_NUM	4	// GPU が同時に計算する最大ブロック数

extern int OpenCL_method;

int init_OpenCL(int unit_size, int *src_max, int *dst_max);
int free_OpenCL(void);
void info_OpenCL(char *buf, int buf_size);

int gpu_copy_blocks(
	unsigned char *data,
	int unit_size,
	int src_start,
	int src_end);

int gpu_multiply_blocks(
	int source_num,
	int src_num,
	int dst_num,
	unsigned short *mat);

int gpu_xor_blocks(
	unsigned char *dst,
	int unit_size,
	int dst_num);

#ifdef __cplusplus
}
#endif

#endif
