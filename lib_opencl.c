#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <stdio.h>

#include <windows.h>
#include <CL/cl.h>

#include "gf16.h"
#include "lib_opencl.h"

//#define DEBUG_OUTPUT // 実験用

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 関数の定義

// Platform API
typedef cl_int (CL_API_CALL *API_clGetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *);
typedef cl_int (CL_API_CALL *API_clGetPlatformInfo)(cl_platform_id, cl_platform_info, size_t, void *, size_t *);

// Device APIs
typedef cl_int (CL_API_CALL *API_clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (CL_API_CALL *API_clGetDeviceInfo)(cl_device_id, cl_device_info, size_t, void *, size_t *);

// Context APIs
typedef cl_context (CL_API_CALL *API_clCreateContext)(const cl_context_properties *, cl_uint, const cl_device_id *, void (CL_CALLBACK *)(const char *, const void *, size_t, void *), void *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseContext)(cl_context);
typedef cl_int (CL_API_CALL *API_clGetContextInfo)(cl_context, cl_context_info, size_t, void *, size_t *);

// Command Queue APIs
typedef cl_command_queue (CL_API_CALL *API_clCreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseCommandQueue)(cl_command_queue);

// Memory Object APIs
typedef cl_mem (CL_API_CALL *API_clCreateBuffer)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseMemObject)(cl_mem);

// Program Object APIs
typedef cl_program (CL_API_CALL *API_clCreateProgramWithSource)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseProgram)(cl_program);
typedef cl_int (CL_API_CALL *API_clBuildProgram)(cl_program, cl_uint, const cl_device_id *,const char *, void (CL_CALLBACK *)(cl_program, void *), void *);
typedef cl_int (CL_API_CALL *API_clUnloadCompiler)(void);
typedef cl_int (CL_API_CALL *API_clUnloadPlatformCompiler)(cl_platform_id);	// OpenCL 1.2 で追加された
typedef cl_int (CL_API_CALL *API_clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void *, size_t *);

// Kernel Object APIs
typedef cl_kernel (CL_API_CALL *API_clCreateKernel)(cl_program, const char *, cl_int *);
typedef cl_int (CL_API_CALL *API_clReleaseKernel)(cl_kernel);
typedef cl_int (CL_API_CALL *API_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void *);
typedef cl_int (CL_API_CALL *API_clGetKernelWorkGroupInfo)(cl_kernel, cl_device_id, cl_kernel_work_group_info, size_t, void *, size_t *);

// Event Object APIs
typedef cl_int (CL_API_CALL *API_clReleaseEvent)(cl_event);
typedef cl_int (CL_API_CALL *API_clGetEventInfo)(cl_event, cl_event_info, size_t, void *, size_t *);

// Flush and Finish APIs
typedef cl_int (CL_API_CALL *API_clFlush)(cl_command_queue);
typedef cl_int (CL_API_CALL *API_clFinish)(cl_command_queue);

// Enqueued Commands APIs
typedef cl_int (CL_API_CALL *API_clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void *, cl_uint, const cl_event *, cl_event *);
typedef void * (CL_API_CALL *API_clEnqueueMapBuffer)(cl_command_queue, cl_mem, cl_bool, cl_map_flags, size_t, size_t, cl_uint, const cl_event *, cl_event *, cl_int *);
typedef cl_int (CL_API_CALL *API_clEnqueueUnmapMemObject)(cl_command_queue, cl_mem, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (CL_API_CALL *API_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t *, const size_t *, const size_t *, cl_uint, const cl_event *, cl_event *);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// グローバル変数

extern unsigned int cpu_flag;	// declared in common2.h

#define MAX_DEVICE 3
#define MAX_GROUP_NUM 32
#define MAX_ITEM_NUM 2048

HMODULE hLibOpenCL = NULL;

cl_context OpenCL_context = NULL;
cl_command_queue OpenCL_command = NULL;
cl_kernel OpenCL_kernel = NULL;
cl_mem OpenCL_src = NULL, OpenCL_dst = NULL, OpenCL_buf = NULL;
size_t OpenCL_group_num, OpenCL_item_num;
int OpenCL_method = 0;	// 正=速い機器を選ぶ, 負=遅い機器を選ぶ

API_clCreateBuffer gfn_clCreateBuffer;
API_clReleaseMemObject gfn_clReleaseMemObject;
API_clSetKernelArg gfn_clSetKernelArg;
API_clReleaseEvent gfn_clReleaseEvent;
API_clGetEventInfo gfn_clGetEventInfo;
API_clFlush gfn_clFlush;
API_clEnqueueWriteBuffer gfn_clEnqueueWriteBuffer;
API_clEnqueueMapBuffer gfn_clEnqueueMapBuffer;
API_clEnqueueUnmapMemObject gfn_clEnqueueUnmapMemObject;
API_clEnqueueNDRangeKernel gfn_clEnqueueNDRangeKernel;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
入力
OpenCL_method : どのデバイスを選ぶか
unit_size : ブロックの単位サイズ
src_max : ソース・ブロック個数
dst_max : パリティ・ブロック個数

出力
return : エラー番号
src_max : 最大で何ブロックまでソースを読み込めるか
dst_max : 最大で何ブロックまでパリティを計算するか

*/

// 0=成功, 1〜エラー番号
int init_OpenCL(int unit_size, int *src_max, int *dst_max)
{
	char buf[2048], *p_source;
	int err = 0, i, j;
	int gpu_power, limit_size, chunk_size, count;
	size_t data_size, alloc_max;
	//FILE *fp;
	HRSRC res;
	HGLOBAL glob;
	API_clGetPlatformIDs fn_clGetPlatformIDs;
#ifdef DEBUG_OUTPUT
	API_clGetPlatformInfo fn_clGetPlatformInfo;
#endif
	API_clGetDeviceIDs fn_clGetDeviceIDs;
	API_clGetDeviceInfo fn_clGetDeviceInfo;
	API_clCreateContext fn_clCreateContext;
	API_clCreateCommandQueue fn_clCreateCommandQueue;
	API_clCreateProgramWithSource fn_clCreateProgramWithSource;
	API_clBuildProgram fn_clBuildProgram;
	API_clUnloadCompiler fn_clUnloadCompiler;
	API_clUnloadPlatformCompiler fn_clUnloadPlatformCompiler;
	API_clGetProgramBuildInfo fn_clGetProgramBuildInfo;
	API_clReleaseProgram fn_clReleaseProgram;
	API_clCreateKernel fn_clCreateKernel;
	API_clGetKernelWorkGroupInfo fn_clGetKernelWorkGroupInfo;
	cl_int ret;
	cl_uint num_platforms = 0, num_devices = 0, num_groups, param_value;
	cl_ulong param_value8, cache_size;
	cl_platform_id platform_id[MAX_DEVICE], selected_platform;	// Intel, AMD, Nvidia などドライバーの提供元
	cl_device_id device_id[MAX_DEVICE], selected_device;	// CPU や GPU など
	cl_program program;

	// ライブラリーを読み込む
	hLibOpenCL = LoadLibraryA("OpenCL.DLL");
	if (hLibOpenCL == NULL)
		return 1;

	// 関数のエントリー取得
	err = 2;
	fn_clGetPlatformIDs = (API_clGetPlatformIDs)GetProcAddress(hLibOpenCL, "clGetPlatformIDs");
	if (fn_clGetPlatformIDs == NULL)
		return err;
#ifdef DEBUG_OUTPUT
	fn_clGetPlatformInfo = (API_clGetPlatformInfo)GetProcAddress(hLibOpenCL, "clGetPlatformInfo");
	if (fn_clGetPlatformInfo == NULL)
		return err;
#endif
	fn_clGetDeviceIDs = (API_clGetDeviceIDs)GetProcAddress(hLibOpenCL, "clGetDeviceIDs");
	if (fn_clGetDeviceIDs == NULL)
		return err;
	fn_clGetDeviceInfo = (API_clGetDeviceInfo)GetProcAddress(hLibOpenCL, "clGetDeviceInfo");
	if (fn_clGetDeviceInfo == NULL)
		return err;
	fn_clCreateContext = (API_clCreateContext)GetProcAddress(hLibOpenCL, "clCreateContext");
	if (fn_clCreateContext == NULL)
		return err;
	fn_clCreateCommandQueue = (API_clCreateCommandQueue)GetProcAddress(hLibOpenCL, "clCreateCommandQueue");
	if (fn_clCreateCommandQueue == NULL)
		return err;
	gfn_clCreateBuffer = (API_clCreateBuffer)GetProcAddress(hLibOpenCL, "clCreateBuffer");
	if (gfn_clCreateBuffer == NULL)
		return err;
	gfn_clReleaseMemObject = (API_clReleaseMemObject)GetProcAddress(hLibOpenCL, "clReleaseMemObject");
	if (gfn_clReleaseMemObject == NULL)
		return err;
	gfn_clEnqueueWriteBuffer = (API_clEnqueueWriteBuffer)GetProcAddress(hLibOpenCL, "clEnqueueWriteBuffer");
	if (gfn_clEnqueueWriteBuffer == NULL)
		return err;
	gfn_clEnqueueMapBuffer = (API_clEnqueueMapBuffer)GetProcAddress(hLibOpenCL, "clEnqueueMapBuffer");
	if (gfn_clEnqueueMapBuffer == NULL)
		return err;
	gfn_clEnqueueUnmapMemObject = (API_clEnqueueUnmapMemObject)GetProcAddress(hLibOpenCL, "clEnqueueUnmapMemObject");
	if (gfn_clEnqueueUnmapMemObject == NULL)
		return err;
	fn_clCreateProgramWithSource = (API_clCreateProgramWithSource)GetProcAddress(hLibOpenCL, "clCreateProgramWithSource");
	if (fn_clCreateProgramWithSource == NULL)
		return err;
	fn_clBuildProgram = (API_clBuildProgram)GetProcAddress(hLibOpenCL, "clBuildProgram");
	if (fn_clBuildProgram == NULL)
		return err;
	fn_clGetProgramBuildInfo = (API_clGetProgramBuildInfo)GetProcAddress(hLibOpenCL, "clGetProgramBuildInfo");
	if (fn_clGetProgramBuildInfo == NULL)
		return err;
	fn_clReleaseProgram = (API_clReleaseProgram)GetProcAddress(hLibOpenCL, "clReleaseProgram");
	if (fn_clReleaseProgram == NULL)
		return err;
	fn_clUnloadPlatformCompiler = (API_clUnloadPlatformCompiler)GetProcAddress(hLibOpenCL, "clUnloadPlatformCompiler");
	if (fn_clUnloadPlatformCompiler == NULL){	// OpenCL 1.1 なら clUnloadCompiler を使う
		fn_clUnloadCompiler = (API_clUnloadCompiler)GetProcAddress(hLibOpenCL, "clUnloadCompiler");
		if (fn_clUnloadCompiler == NULL)
			return err;
	}
	fn_clCreateKernel = (API_clCreateKernel)GetProcAddress(hLibOpenCL, "clCreateKernel");
	if (fn_clCreateKernel == NULL)
		return err;
	gfn_clSetKernelArg = (API_clSetKernelArg)GetProcAddress(hLibOpenCL, "clSetKernelArg");
	if (gfn_clSetKernelArg == NULL)
		return err;
	fn_clGetKernelWorkGroupInfo = (API_clGetKernelWorkGroupInfo)GetProcAddress(hLibOpenCL, "clGetKernelWorkGroupInfo");
	if (fn_clGetKernelWorkGroupInfo == NULL)
		return err;
	gfn_clReleaseEvent = (API_clReleaseEvent)GetProcAddress(hLibOpenCL, "clReleaseEvent");
	if (gfn_clReleaseEvent == NULL)
		return err;
	gfn_clGetEventInfo = (API_clGetEventInfo)GetProcAddress(hLibOpenCL, "clGetEventInfo");
	if (gfn_clGetEventInfo == NULL)
		return err;
	gfn_clFlush = (API_clFlush)GetProcAddress(hLibOpenCL, "clFlush");
	if (gfn_clFlush == NULL)
		return err;
	gfn_clEnqueueNDRangeKernel = (API_clEnqueueNDRangeKernel)GetProcAddress(hLibOpenCL, "clEnqueueNDRangeKernel");
	if (gfn_clEnqueueNDRangeKernel == NULL)
		return err;

	// OpenCL 環境の数
	ret = fn_clGetPlatformIDs(MAX_DEVICE, platform_id, &num_platforms);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 10;
	if (OpenCL_method >= 0){	// 選択する順序と初期値を変える
		OpenCL_method = 1;
		gpu_power = 0;
	} else {
		OpenCL_method = -1;
		gpu_power = INT_MIN;
	}
	alloc_max = 0;

	for (i = 0; i < (int)num_platforms; i++){
#ifdef DEBUG_OUTPUT
		// 環境の情報表示
		if (fn_clGetPlatformInfo(platform_id[i], CL_PLATFORM_NAME, sizeof(buf), buf, NULL) == CL_SUCCESS)
			printf("\nPlatform[%d] = %s\n", i, buf);
		if (fn_clGetPlatformInfo(platform_id[i], CL_PLATFORM_VERSION, sizeof(buf), buf, NULL) == CL_SUCCESS)
			printf("VERSION = %s\n", buf);
#endif

		// 環境内の OpenCL 対応機器の数
		if (fn_clGetDeviceIDs(platform_id[i], CL_DEVICE_TYPE_GPU, MAX_DEVICE, device_id, &num_devices) != CL_SUCCESS)
			continue;

		for (j = 0; j < (int)num_devices; j++){
			// デバイスが利用可能か確かめる
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_AVAILABLE, sizeof(cl_uint), &param_value, NULL);
			if ((ret != CL_SUCCESS) || (param_value == CL_FALSE))
				continue;
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_COMPILER_AVAILABLE, sizeof(cl_uint), &param_value, NULL);
			if ((ret != CL_SUCCESS) || (param_value == CL_FALSE))
				continue;

#ifdef DEBUG_OUTPUT
			// 機器の情報表示
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_NAME, sizeof(buf), buf, NULL);
			if (ret == CL_SUCCESS)
				printf("\nDevice[%d] = %s\n", j, buf);
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_LOCAL_MEM_SIZE, sizeof(cl_ulong), &param_value8, NULL);
			if (ret == CL_SUCCESS)
				printf("LOCAL_MEM_SIZE = %I64d KB\n", param_value8 >> 10);
#endif
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(cl_ulong), &param_value8, NULL);
			if (ret != CL_SUCCESS)
				continue;
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &num_groups, NULL);
			if (ret != CL_SUCCESS)
				continue;
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &data_size, NULL);
			if (ret != CL_SUCCESS)
				continue;
			ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl_uint), &param_value, NULL);
			if (ret != CL_SUCCESS)
				continue;
			if (param_value != 0)
				param_value = 1;
#ifdef DEBUG_OUTPUT
			printf("MAX_MEM_ALLOC_SIZE = %I64d MB\n", param_value8 >> 20);
			printf("MAX_COMPUTE_UNITS = %d\n", num_groups);
			printf("MAX_WORK_GROUP_SIZE = %d\n", data_size);
			printf("HOST_UNIFIED_MEMORY = %d\n", param_value);
#endif
			// MAX_COMPUTE_UNITS * MAX_WORK_GROUP_SIZE で計算力を測る
			count = ((1 - param_value) << 29) + (int)(data_size * num_groups);
			count *= OpenCL_method;	// 符号を変える
			//printf("prev = %d, now = %d\n", gpu_power, count);
			if ((count > gpu_power) && (data_size >= 256) &&	// 256以上ないとテーブルを作れない
					(param_value8 / 4 > (cl_ulong)unit_size)){	// CL_DEVICE_MAX_MEM_ALLOC_SIZE に収まるか
				gpu_power = count;
				selected_device = device_id[j];	// 使うデバイスの ID
				selected_platform = platform_id[i];
				OpenCL_group_num = num_groups;	// ワークグループ数は COMPUTE_UNITS 数にする
				if (OpenCL_group_num > MAX_GROUP_NUM)	// 制限を付けてローカルメモリーの消費を抑える
					OpenCL_group_num = MAX_GROUP_NUM;
				OpenCL_item_num = data_size;
				if (OpenCL_item_num > MAX_ITEM_NUM)	// 制限を付けてプライベートメモリーの消費を抑える
					OpenCL_item_num = MAX_ITEM_NUM;
				// 確保できるメモリー領域の大きさ
				if (param_value8 >= 0x80000000){	// 2GB 以上なら
					alloc_max = 0x7FFFFFC0;
				} else {
					alloc_max = (size_t)param_value8;
				}

				cache_size = 0;	// GPU のキャッシュ (CPU の L3キャッシュ) を活用できるようにする
				if (param_value == 1){
					ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_GLOBAL_MEM_CACHE_TYPE, sizeof(cl_uint), &param_value, NULL);
					if ((ret == CL_SUCCESS) && (param_value == 2)){
						ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, sizeof(cl_ulong), &cache_size, NULL);
						if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
							printf("GLOBAL_MEM_CACHE_SIZE = %I64d KB\n", cache_size >> 10);
#endif
							if (cache_size < 1048576)
								cache_size = 0;	// サイズが小さい場合は分割しない
						}
					}
				}

				// AMD Radeon ではメモリー領域が一個らしい？
				ret = fn_clGetDeviceInfo(device_id[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(cl_ulong), &param_value8, NULL);
				if (ret == CL_SUCCESS){
#ifdef DEBUG_OUTPUT
					printf("GLOBAL_MEM_SIZE = %I64d MB\n", param_value8 >> 20);
#endif
					// 領域一個あたりのサイズは全体の 1/4 までにする
					if ((cl_ulong)alloc_max > param_value8 / 4)
						alloc_max = (size_t)(param_value8 / 4);
				}
			}
		}
	}

	if (alloc_max == 0){
#ifdef DEBUG_OUTPUT
		printf("\nAvailable GPU device was not found.\n");
#endif
		return 3;
	}
	OpenCL_method = ((gpu_power * OpenCL_method) >> 29) ^ 1;	// HOST_UNIFIED_MEMORY かどうか

#ifdef DEBUG_OUTPUT
	// デバイスの情報表示
	ret = fn_clGetPlatformInfo(selected_platform, CL_PLATFORM_NAME, sizeof(buf), buf, NULL);
	if (ret == CL_SUCCESS)
		printf("\nSelected platform = %s\n", buf);
	ret = fn_clGetDeviceInfo(selected_device, CL_DEVICE_NAME, sizeof(buf), buf, NULL);
	if (ret == CL_SUCCESS)
		printf("Selected device = %s\n", buf);
#endif

	// OpenCL 利用環境を作成する
	OpenCL_context = fn_clCreateContext(NULL, 1, &selected_device, NULL, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 11;
	OpenCL_command = fn_clCreateCommandQueue(OpenCL_context, selected_device, 0, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 12;

	// 何ブロック分のメモリー領域を確保するのか
	limit_size = *src_max;	// ソース・ブロック個数
	count = (int)(alloc_max / unit_size);	// ブロックを何個まで保管できるか
	if (count > limit_size)
		count = limit_size;
	*src_max = count;
	if (OpenCL_method == 0){	// VRAM 上にメモリー領域を確保する
		data_size = (size_t)unit_size * count;
		OpenCL_src = gfn_clCreateBuffer(OpenCL_context, CL_MEM_READ_ONLY, data_size, NULL, &ret);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 13;
#ifdef DEBUG_OUTPUT
		printf("src buf : %d KB (%d blocks), OK\n", data_size >> 10, count);
#endif
	}

	count = (int)(alloc_max / unit_size);	// ブロックを何個まで保管できるか
	limit_size = *dst_max;	// パリティ・ブロック個数
	if (count > limit_size)
		count = limit_size;
	if (count > (int)OpenCL_group_num)	// Compute Unit 個数までにする
		count = (int)OpenCL_group_num;
	if (count > GPU_MAX_NUM)	// 同時計算ブロック数が多すぎる場合は減らす
		count = GPU_MAX_NUM;

	if (cache_size != 0){
		if (unit_size > (int)cache_size / 8){	// cache size / 4 まで
			if (count > 2)
				count = 2;
		} else {
			i = (int)cache_size / (unit_size * 3);	// cache size / 3 まで
			if (count > i)
				count = i;
		}
/*
		2: 300, 400, 500, 600, 1000, 2000
		3: 200 (1/10)
		4: 150 (1/13), 100 (1/20)
*/
	}

	*dst_max = count;
	// ホストがアクセスできる所にメモリー領域を確保する
	data_size = unit_size * count;
	OpenCL_dst = gfn_clCreateBuffer(OpenCL_context, CL_MEM_ALLOC_HOST_PTR, data_size, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 14;
#ifdef DEBUG_OUTPUT
	printf("dst buf : %d KB (%d blocks), OK\n", data_size >> 10, count);
#endif

	// factor は最大個数分
	// factors (src_max * dst_max)
	data_size = sizeof(unsigned short) * (*src_max) * count;
	OpenCL_buf = gfn_clCreateBuffer(OpenCL_context, CL_MEM_READ_ONLY, data_size, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 16;
#ifdef DEBUG_OUTPUT
	printf("factor buf : %d KB (%d factors), OK\n", data_size >> 10, (*src_max) * count);
#endif

	chunk_size = unit_size;	// unit_size は MEM_UNIT の倍数になってる
	if (cache_size != 0){	// 内蔵 GPU ならキャッシュを利用する
		// CPU と併用ならキャッシュ・サイズの 1/4 を GPU で使う
		// メモリーが Dual Channel だと 1/3 の方が速いかも？
		if (unit_size > (int)cache_size / 3){	// ブロックが cache_size / 3 以上なら
			i = (int)cache_size / 4;	// ブロックの合計がキャッシュの 1/4 になるようにする
		} else {
			i = (int)cache_size / 3;
		}
		if ((size_t)i > alloc_max)
			i = (int)alloc_max;
		j = 1;
		while ((chunk_size - MEM_UNIT) * count > i){	// 制限サイズより大きいなら
			j++;	// 分割数を増やして chunk のサイズを試算してみる
			chunk_size = (unit_size + j - 1) / j;
		}
		chunk_size = (chunk_size + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);	// MEM_UNIT の倍数にする
#ifdef DEBUG_OUTPUT
		printf("limit size = %d, split count = %d, chunk size = %d\n", i, j, chunk_size);
#endif
	}

/*
	// テキスト形式の OpenCL C ソース・コードを読み込む
	err = 4;
	fp = fopen("source.cl", "r");
	if (fp == NULL){
		printf("cannot open source code file\n");
		return err;
	}
	ret = fseek(fp, 0, SEEK_END);
	if (ret != 0){
		printf("cannot read source code file\n");
		fclose(fp);
		return err;
	}
	data_size = ftell(fp);
	ret = fseek(fp, 0, SEEK_SET);
	if (ret != 0){
		printf("cannot read source code file\n");
		fclose(fp);
		return err;
	}
	if (data_size > 102400){ // 100 KB まで
		fclose(fp);
		printf("source code file is too large\n");
		return err;
	}
	p_source = (char *)malloc(data_size + 1);
	if (p_source == NULL){
		fclose(fp);
		printf("malloc error\n");
		return err;
	}
	data_size = fread(p_source, 1, data_size, fp);
	fclose(fp);
	printf("Source code length = %d characters\n", data_size);
	p_source[data_size] = 0;	// 末尾を null 文字にする

	// プログラムを作成する
	program = fn_clCreateProgramWithSource(OpenCL_context, 1, (char **)&p_source, NULL, &ret);
	if (ret != CL_SUCCESS){
		free(p_source);
		return (ret << 8) | 20;
	}
	free(p_source);	// もうテキストは要らないので開放しておく
*/

	// リソースから OpenCL C ソース・コードを読み込む
	err = 4;
	// Referred to "Embedding OpenCL Kernel Files in the Application on Windows"
	res = FindResource(NULL, L"#1", L"RT_STRING");	// find the resource
	if (res == NULL){
		//printf("cannot find resource\n");
		return err;
	}
	glob = LoadResource(NULL, res);	// load the resource.
	if (res == NULL){
		//printf("cannot load resource\n");
		return err;
	}
	p_source = (char *)LockResource(glob);	// lock the resource to get a char*
	if (res == NULL){
		//printf("cannot lock resource\n");
		return err;
	}
	data_size = SizeofResource(NULL, res);
	if (data_size == 0){
		//printf("cannot get size of resource\n");
		return err;
	}
	//printf("Source code length = %d characters\n", data_size);

	// プログラムを作成する
	program = fn_clCreateProgramWithSource(OpenCL_context, 1, (char **)&p_source, &data_size, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 20;
	FreeResource(glob);	// not required ?

	// 定数を指定する
	unit_size /= 4;	// 個数にする
	sprintf(buf, "-D BLK_SIZE=%d -D CHK_SIZE=%d", unit_size, chunk_size / 4);

	// 使用する OpenCL デバイス用にコンパイルする
	ret = fn_clBuildProgram(program, 1, &selected_device, buf, NULL, NULL);
	if (ret != CL_SUCCESS){
#ifdef DEBUG_OUTPUT
		buf[0] = 0;
		printf("clBuildProgram : Failed\n");
		if (fn_clGetProgramBuildInfo(program, selected_device, CL_PROGRAM_BUILD_LOG, sizeof(buf), buf, NULL) == CL_SUCCESS)
			printf("%s\n", buf);
#endif
		return (ret << 8) | 21;
	}

	// カーネル関数を抽出する
	if ((((cpu_flag & 0x101) == 1) || ((cpu_flag & 16) != 0)) && (sse_unit == 32)){
		OpenCL_method |= 2;	// SSSE3 & ALTMAP または AVX2 ならデータの並び替え対応版を使う
	} else if (((cpu_flag & 128) != 0) && (sse_unit == 256)){
		OpenCL_method |= 4;	// JIT(SSE2) は bit ごとに上位から 16バイトずつ並ぶ
	}
	sprintf(buf, "method%d", OpenCL_method);
	OpenCL_kernel = fn_clCreateKernel(program, buf, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 22;
#ifdef DEBUG_OUTPUT
	printf("CreateKernel : %s\n", buf);
#endif

	// カーネルが実行できる work item 数を調べる
	ret = fn_clGetKernelWorkGroupInfo(OpenCL_kernel, NULL, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &data_size, NULL);
	if ((ret == CL_SUCCESS) && (data_size < OpenCL_item_num)){
		OpenCL_item_num = data_size;
#ifdef DEBUG_OUTPUT
		printf("KERNEL_WORK_GROUP_SIZE = %d\n", data_size);
#endif
	}
	// work item 数が必要以上に多い場合は減らす
	ret = fn_clGetKernelWorkGroupInfo(OpenCL_kernel, NULL, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &data_size, NULL);
	if ((ret == CL_SUCCESS) && (unit_size >= 256) && ((int)OpenCL_item_num > unit_size)){
		OpenCL_item_num = unit_size;
		OpenCL_item_num = (OpenCL_item_num + data_size - 1) & ~(data_size - 1);
#ifdef DEBUG_OUTPUT
		printf("KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE = %d\n", data_size);
		printf("Number of work items is reduced to %d\n", OpenCL_item_num);
#endif
	}

	// プログラムを破棄する
	ret = fn_clReleaseProgram(program);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 23;

	// これ以上コンパイルしない
	if (fn_clUnloadPlatformCompiler != NULL){	// OpenCL 1.2 なら
		fn_clUnloadPlatformCompiler(selected_platform);
	} else {	// OpenCL 1.1 なら
		fn_clUnloadCompiler();
	}

	// カーネル引数を指定する
	if ((OpenCL_method & 1) == 0){	// 内蔵 GPU ではメモリー領域を後で指定する
		ret = gfn_clSetKernelArg(OpenCL_kernel, 0, sizeof(cl_mem), &OpenCL_src);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 100;
	}
	ret = gfn_clSetKernelArg(OpenCL_kernel, 1, sizeof(cl_mem), &OpenCL_dst);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 101;
	ret = gfn_clSetKernelArg(OpenCL_kernel, 2, sizeof(cl_mem), &OpenCL_buf);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 102;
	if ((OpenCL_method & 4) == 0){	// XOR 方式以外なら 2KB のテーブルを使う
		ret = gfn_clSetKernelArg(OpenCL_kernel, 3, 2048, NULL);	// ローカルにメモリーを割り当てる
	} else {
		ret = gfn_clSetKernelArg(OpenCL_kernel, 3, 64, NULL);
	}
	if (ret != CL_SUCCESS)
		return (ret << 8) | 103;

#ifdef DEBUG_OUTPUT
	// ワークアイテム数
	printf("\nMax number of work items = %d (%d * %d)\n",
		OpenCL_item_num * OpenCL_group_num, OpenCL_item_num, OpenCL_group_num);
#endif

	return 0;
}

int free_OpenCL(void)
{
	API_clReleaseContext fn_clReleaseContext;
	API_clReleaseCommandQueue fn_clReleaseCommandQueue;
	API_clReleaseKernel fn_clReleaseKernel;
	API_clFinish fn_clFinish;
	int err = 0;	// 最初のエラーだけ記録する
	cl_int ret;

	if (hLibOpenCL == NULL)
		return 0;

	// OpenCL 関連のリソースを開放する
	if (OpenCL_command != NULL){
		// 動作中なら終了するのを待つ
		fn_clFinish = (API_clFinish)GetProcAddress(hLibOpenCL, "clFinish");
		if (fn_clFinish != NULL){
			ret = fn_clFinish(OpenCL_command);
		} else {
			ret = 1;
		}
		if ((err == 0) && (ret != CL_SUCCESS))
			err = (ret << 8) | 1;

		if (OpenCL_buf != NULL){
			ret = gfn_clReleaseMemObject(OpenCL_buf);
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 10;
			OpenCL_buf = NULL;
		}
		if (OpenCL_src != NULL){
			ret = gfn_clReleaseMemObject(OpenCL_src);
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 11;
			OpenCL_src = NULL;
		}
		if (OpenCL_dst != NULL){
			ret = gfn_clReleaseMemObject(OpenCL_dst);
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 12;
			OpenCL_dst = NULL;
		}
		if (OpenCL_kernel != NULL){
			fn_clReleaseKernel = (API_clReleaseKernel)GetProcAddress(hLibOpenCL, "clReleaseKernel");
			if (fn_clReleaseKernel != NULL){
				ret = fn_clReleaseKernel(OpenCL_kernel);
				OpenCL_kernel = NULL;
			} else {
				ret = 1;
			}
			if ((err == 0) && (ret != CL_SUCCESS))
				err = (ret << 8) | 3;
		}
		fn_clReleaseCommandQueue = (API_clReleaseCommandQueue)GetProcAddress(hLibOpenCL, "clReleaseCommandQueue");
		if (fn_clReleaseCommandQueue != NULL){
			ret = fn_clReleaseCommandQueue(OpenCL_command);
			OpenCL_command = NULL;
		} else {
			ret = 1;
		}
		if ((err == 0) && (ret != CL_SUCCESS))
			err = (ret << 8) | 4;
	}
	if (OpenCL_context != NULL){
		fn_clReleaseContext = (API_clReleaseContext)GetProcAddress(hLibOpenCL, "clReleaseContext");
		if (fn_clReleaseContext != NULL){
			ret = fn_clReleaseContext(OpenCL_context);
			OpenCL_context = NULL;
		} else {
			ret = 1;
		}
		if ((err == 0) && (ret != CL_SUCCESS))
			err = (ret << 8) | 5;
	}

	FreeLibrary(hLibOpenCL);
	hLibOpenCL = NULL;

	return err;
}

void info_OpenCL(char *buf, int buf_size)
{
	API_clGetContextInfo fn_clGetContextInfo;
	API_clGetDeviceInfo fn_clGetDeviceInfo;
	cl_int ret;

	if ((hLibOpenCL == NULL) || (OpenCL_context == NULL))
		return;

	fn_clGetContextInfo = (API_clGetContextInfo)GetProcAddress(hLibOpenCL, "clGetContextInfo");
	fn_clGetDeviceInfo = (API_clGetDeviceInfo)GetProcAddress(hLibOpenCL, "clGetDeviceInfo");
	if ((fn_clGetContextInfo != NULL) && (fn_clGetDeviceInfo != NULL)){
		cl_device_id device_id;

		ret = fn_clGetContextInfo(OpenCL_context, CL_CONTEXT_DEVICES, sizeof(cl_device_id), &device_id, NULL);
		if (ret == CL_SUCCESS){
			ret = fn_clGetDeviceInfo(device_id, CL_DEVICE_NAME, buf_size, buf, NULL);
			if (ret == CL_SUCCESS)
				printf("\nOpenCL : %s (%d * %d)\n", buf, OpenCL_item_num, OpenCL_group_num);
		}
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 断片化したブロックを集めてコピーする
int gpu_copy_blocks(
	unsigned char *data,	// ブロックのバッファー (境界は 4096にすること)
	int unit_size,			// 最低でも 64の倍数にすること
	int src_start,			// 何ブロック目からコピーするのか
	int src_num)			// 何ブロックをコピーするのか
{
	int align_off;
	size_t data_size, data_off;
	cl_int ret;

	if ((OpenCL_method & 1) == 0){	// 外付け GPU では VRAM 上にコピーする
		data += (size_t)unit_size * src_start;
		data_size = (size_t)unit_size * src_num;
		ret = gfn_clEnqueueWriteBuffer(OpenCL_command, OpenCL_src, CL_FALSE, 0, data_size, data, 0, NULL, NULL);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 1;
		//printf("copy buf : %d KB (%d blocks), OK\n", data_size >> 10, src_num);

		// コマンドを実行する
		ret = gfn_clFlush(OpenCL_command);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 2;

	} else {	// 内蔵 GPU ではホスト・メモリーを参照する
		data_off = (size_t)unit_size * src_start;	// 参照の開始位置
		align_off = data_off & 4095;
		data_off &= ~4095;	// 4096の倍数にする
		data_size = (size_t)unit_size * src_num + align_off;	// 64の倍数になってる
		if (OpenCL_src != NULL)	// 既に確保されてる場合は解除しておく
			gfn_clReleaseMemObject(OpenCL_src);
		OpenCL_src = gfn_clCreateBuffer(OpenCL_context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, data_size, data + data_off, &ret);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 11;
		//printf("refer buf : %d KB (%d blocks), offset = %d\n", data_size >> 10, src_num, align_off);

		// メモリー領域を指定する
		ret = gfn_clSetKernelArg(OpenCL_kernel, 0, sizeof(cl_mem), &OpenCL_src);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 12;

		// ウインドウ開始位置の差分を指定する
		align_off /= 4;
		ret = gfn_clSetKernelArg(OpenCL_kernel, 5, sizeof(int), &align_off);
		if (ret != CL_SUCCESS)
			return (ret << 8) | 13;
	}

	return 0;
}

// 複数ブロックを掛け算する
int gpu_multiply_blocks(
	int source_num,
	int src_num,
	int dst_num,			// dst_num <= dst_max
	unsigned short *mat)	// Matrix of numbers to multiply by
{
	int i;
	size_t global_size[2], local_size[2];
	cl_int ret;
	cl_event event;

	// 倍率の配列をデバイス側に書き込む
	ret = gfn_clEnqueueWriteBuffer(OpenCL_command, OpenCL_buf, CL_FALSE, 0,
			sizeof(unsigned short) * src_num * dst_num, mat, 0, NULL, NULL);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 1;

	// 引数を指定する
	ret = gfn_clSetKernelArg(OpenCL_kernel, 4, sizeof(int), &src_num);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 104;

	// カーネル並列実行
	ret = (int)OpenCL_group_num / dst_num;
	if (ret >= 3){	// 1ブロックを 3個以上の CU で計算する場合
		i = ((int)OpenCL_group_num * 3) / (dst_num * 4);	// 合計を 75% 以下にする
		if (ret > i)
			ret = i;
	}
	global_size[0] = OpenCL_item_num * ret;
	global_size[1] = dst_num;
	local_size[0] = OpenCL_item_num;
	local_size[1] = 1;
	//printf("group num = %d, global size = %d, local size = %d \n", global_size[1], global_size[0], local_size[0]);
	ret = gfn_clEnqueueNDRangeKernel(OpenCL_command, OpenCL_kernel, 2, NULL, global_size, local_size, 0, NULL, &event);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 2;

	// コマンドを実行する
	ret = gfn_clFlush(OpenCL_command);
	if (ret != CL_SUCCESS){
		gfn_clReleaseEvent(event);
		return (ret << 8) | 3;
	}

	// GPU の処理終了を待つ
	do {
		Sleep(1);	// 他のスレッドを動かす

		ret = gfn_clGetEventInfo(event, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(int), &i, NULL);
		if (ret != CL_SUCCESS){
			gfn_clReleaseEvent(event);
			return (ret << 8) | 4;
		}
	} while (i != CL_COMPLETE);
	gfn_clReleaseEvent(event);

	return 0;
}

// 計算結果を追加する
int gpu_xor_blocks(
	unsigned char *buf,
	int unit_size,
	int dst_num)
{
	int i, max;
	unsigned int *vram, *src, *dst;
	cl_int ret;

	max = unit_size * dst_num;
	vram = gfn_clEnqueueMapBuffer(OpenCL_command, OpenCL_dst, CL_TRUE, CL_MAP_READ, 0, max, 0, NULL, NULL, &ret);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 1;

	max /= 4;	// 4バイト整数の個数にする (SSE2 で XOR しても速くならず)
	src = vram;
	dst = (unsigned int *)buf;
	for (i = 0; i < max; i++)
		dst[i] ^= src[i];

	ret = gfn_clEnqueueUnmapMemObject(OpenCL_command, OpenCL_dst, vram, 0, NULL, NULL);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 2;

	// コマンドを実行する
	ret = gfn_clFlush(OpenCL_command);
	if (ret != CL_SUCCESS)
		return (ret << 8) | 3;

	return 0;
}

