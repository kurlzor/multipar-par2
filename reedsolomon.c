// reedsolomon.c
// Copyright : 2016-05-01 Yutaka Sawada
// License : GPL

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <malloc.h>
#include <process.h>
#include <stdio.h>

#include <windows.h>

#include "common2.h"
#include "crc.h"
#include "gf16.h"
#include "phmd5.h"
#include "lib_opencl.h"
#include "rs_encode.h"
#include "rs_decode.h"
#include "reedsolomon.h"


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// chunk がキャッシュに収まるようにすれば速くなる！ (Cache Blocking という最適化手法)
int try_cache_blocking(int unit_size)
{
	int limit_size, chunk_count, chunk_size;

	// CPUキャッシュをどのくらいまで使うか
	limit_size = cpu_flag & 0x7FFF8000;	// 最低でも 32KB になる
	if (limit_size == 0)	// キャッシュ・サイズを取得できなかった場合は最適化しない
		limit_size = unit_size;

	// キャッシュにうまく収まるように chunk のサイズを決める
	chunk_count = 1;
	chunk_size = unit_size;
	while (chunk_size - sse_unit > limit_size){	// 制限サイズより大きいなら
		// 分割数を増やして chunk のサイズを試算してみる
		chunk_count++;
		chunk_size = (unit_size + chunk_count - 1) / chunk_count;
	}
	chunk_size = (chunk_size + (sse_unit - 1)) & ~(sse_unit - 1);	// sse_unit の倍数にする

	return chunk_size;
}

// 空きメモリー量からファイル・アクセスのバッファー・サイズを計算する
unsigned int get_io_size(
	unsigned int buf_num,	// 何ブロック分の領域を確保するのか
	unsigned int *part_num,	// 部分的なエンコード用の作業領域
	size_t trial_alloc)		// 確保できるか確認するのか
{
	unsigned int unit_size, io_size, part_max, part_min;
	size_t mem_size, io_size64;

	part_max = *part_num;	// 初期値には最大値をセットする
	part_min = source_num >> PART_MIN_RATE;
	if (part_min < PART_MIN_NUM)
		part_min = PART_MIN_NUM;
	if (part_min > part_max)
		part_min = part_max;
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);	// sse_unit の倍数にする

	if (trial_alloc){
		__int64 possible_size;
		possible_size = (__int64)unit_size * (buf_num + part_max);
#ifndef _WIN64	// 32-bit 版なら
		if (possible_size > MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			possible_size = MAX_MEM_SIZE;
#endif
		trial_alloc = (size_t)possible_size;
		trial_alloc = (trial_alloc + 0xFFFF) & ~0xFFFF;	// 64KB の倍数にしておく
	}
	mem_size = get_mem_size(trial_alloc);
	io_size64 = mem_size / (buf_num + part_max) - HASH_SIZE;	// 何個分必要か

	// ブロック・サイズより大きい、またはブロック・サイズ自体が小さい場合は
	if ((io_size64 >= (size_t)block_size) || (block_size <= 1024)){
		io_size = unit_size - HASH_SIZE;	// ブロック・サイズ - HASH_SIZE

	} else {	// ブロック・サイズを等分割する
		unsigned int num, num2;
		io_size = (unsigned int)io_size64;
		num = (block_size + io_size - 1) / io_size;	// ブロックを何分割するか
		io_size64 = mem_size / (buf_num + part_min) - HASH_SIZE;	// 確保するサイズを最低限にした場合
		io_size = (unsigned int)io_size64;
		num2 = (block_size + io_size - 1) / io_size;
		if (num > num2){	// 確保量を減らしたほうがブロックの分割数が減るなら
			io_size = (block_size + num2 - 1) / num2;
			if (io_size < 1024)
				io_size = 1024;
			num = (unsigned int)(mem_size / (io_size + HASH_SIZE)) - buf_num;
			if (num > part_max)
				num = part_max;
			if (num < part_min)
				num = part_min;
			*part_num = num;
		} else {
			io_size = (block_size + num - 1) / num;
			if (io_size < 1024)
				io_size = 1024;	// 断片化する場合でもブロック数が多いと 32768 KB は使う
		}
		io_size = ((io_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1)) - HASH_SIZE;	// sse_unit の倍数 - HASH_SIZE
	}

	return io_size;
}

// GPU 用に unit_size を MEM_UNIT の倍数にする
// io_size = unit_size - HASH_SIZE になることに注意 (MEM_UNIT > HASH_SIZE)
unsigned int get_io_size_gpu(
	unsigned int buf_num,	// 何ブロック分の領域を確保するのか
	size_t trial_alloc)		// 確保できるか確認するのか
{
	unsigned int unit_size, io_size;
	size_t mem_size;

	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);

	if (trial_alloc){
		__int64 possible_size;
		possible_size = (__int64)unit_size * buf_num;
#ifndef _WIN64	// 32-bit 版なら
		if (possible_size > MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			possible_size = MAX_MEM_SIZE;
#endif
		trial_alloc = (size_t)possible_size;
		trial_alloc = (trial_alloc + 0xFFFF) & ~0xFFFF;	// 64KB の倍数にしておく
	}
	mem_size = get_mem_size(trial_alloc) / buf_num - HASH_SIZE;	// 何個分必要か

	// ブロック・サイズより大きい、またはブロック・サイズ自体が小さい場合は
	if ((mem_size >= (size_t)block_size) || (block_size <= 1024)){
		io_size = unit_size - HASH_SIZE;	// ブロック・サイズ - HASH_SIZE

	} else {	// ブロック・サイズを等分割する
		unsigned int split_num;
		io_size = (unsigned int)mem_size;
		split_num = (block_size + io_size - 1) / io_size;	// ブロックを何分割するか
		io_size = (block_size + split_num - 1) / split_num;
		io_size = ((io_size + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;
		if (io_size < 1024 - HASH_SIZE)	// 最低値も MEM_UNIT の倍数にする
			io_size = 1024 - HASH_SIZE;
	}

	return io_size;
}

// 何ブロックまとめてファイルから読み込むかを空きメモリー量から計算する
int read_block_num(
	int keep_num,			// 保持するパリティ・ブロック数
	int add_num,			// 余裕を見るブロック数
	size_t trial_alloc)		// 確保できるか確認するのか
{
	int buf_num, read_min;
	unsigned int unit_size;
	size_t mem_size;

	read_min = keep_num >> READ_MIN_RATE;
	if (read_min < READ_MIN_NUM)
		read_min = READ_MIN_NUM;
	if (read_min > source_num)
		read_min = source_num;
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);

	if (trial_alloc){
		__int64 possible_size;
		possible_size = (__int64)unit_size * (source_num + keep_num + add_num);
#ifndef _WIN64	// 32-bit 版なら
		if (possible_size > MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			possible_size = MAX_MEM_SIZE;
#endif
		trial_alloc = (size_t)possible_size;
		trial_alloc = (trial_alloc + 0xFFFF) & ~0xFFFF;	// 64KB の倍数にしておく
	}
	mem_size = get_mem_size(trial_alloc) / unit_size;	// 何個分確保できるか

	if (mem_size >= (size_t)(source_num + keep_num + add_num)){	// 最大個数より多い
		buf_num = source_num;
	} else if ((int)mem_size < read_min + keep_num + add_num){	// 少なすぎる
		buf_num = 0;	// メモリー不足の印
	} else {	// ソース・ブロック個数を等分割する
		int split_num;
		buf_num = (int)mem_size - (keep_num + add_num);
		split_num = (source_num + buf_num - 1) / buf_num;	// 何回に別けて読み込むか
		buf_num = (source_num + split_num - 1) / split_num;
	}

	return buf_num;
}

int read_block_num_gpu(		// GPU 用
	int keep_num,			// 保持するパリティ・ブロック数
	int add_num,			// 余裕を見るブロック数
	size_t trial_alloc)		// 確保できるか確認するのか
{
	int buf_num, read_min;
	unsigned int unit_size;
	size_t mem_size;

	read_min = keep_num >> READ_MIN_RATE;
	if (read_min < READ_MIN_NUM)
		read_min = READ_MIN_NUM;
	if (read_min > source_num)
		read_min = source_num;
	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);

	if (trial_alloc){
		__int64 possible_size;
		possible_size = (__int64)unit_size * (source_num + keep_num + add_num);
#ifndef _WIN64	// 32-bit 版なら
		if (possible_size > MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			possible_size = MAX_MEM_SIZE;
#endif
		trial_alloc = (size_t)possible_size;
		trial_alloc = (trial_alloc + 0xFFFF) & ~0xFFFF;	// 64KB の倍数にしておく
	}
	mem_size = get_mem_size(trial_alloc) / unit_size;	// 何個分確保できるか

	if (mem_size >= (size_t)(source_num + keep_num + add_num)){	// 最大個数より多い
		buf_num = source_num;
	} else if ((int)mem_size < read_min + keep_num + add_num){	// 少なすぎる
		buf_num = 0;	// メモリー不足の印
	} else {	// ソース・ブロック個数を等分割する
		int split_num;
		buf_num = (int)mem_size - (keep_num + add_num);
		split_num = (source_num + buf_num - 1) / buf_num;	// 何回に別けて読み込むか
		buf_num = (source_num + split_num - 1) / split_num;
	}

	return buf_num;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 戸川 隼人 の「演習と応用FORTRAN77」の逆行列の計算方法を参考にして
// Gaussian Elimination を少し修正して行列の数を一つにしてみた

// 半分のメモリーで逆行列を計算する (利用するパリティ・ブロックの所だけ)
static int invert_matrix_st(unsigned short *mat,
	int rows,				// 横行の数、行列の縦サイズ、失われたソース・ブロックの数 = 利用するパリティ・ブロック数
	int cols,				// 縦列の数、行列の横サイズ、本来のソース・ブロック数
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int i, j, row_start, row_start2, pivot, factor;
	unsigned int time_last = GetTickCount();

	// Gaussian Elimination with 1 matrix
	pivot = 0;
	row_start = 0;	// その行の開始位置
	for (i = 0; i < rows; i++){
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((i * 1000) / rows))
				return 2;
			time_last = GetTickCount();
		}

		// その行 (パリティ・ブロック) がどのソース・ブロックの代用か
		while ((pivot < cols) && (s_blk[pivot].exist != 0))
			pivot++;

		// Divide the row by element i,pivot
		factor = mat[row_start + pivot];	// mat(j, pivot) は 0以外のはず
		//printf("\nparity[ %u ] -> source[ %u ], factor = %u\n", id[col_find], col_find, factor);
		if (factor > 1){	// factor が 1より大きいなら、1にする為に factor で割る
			mat[row_start + pivot] = 1;	// これが行列を一個で済ます手
			galois_region_divide(mat + row_start, cols, factor);
		} else if (factor == 0){	// factor = 0 だと、その行列の逆行列を計算できない
			return (0x00010000 | pivot);	// どのソース・ブロックで問題が発生したのかを返す
		}

		// 別の行の同じ pivot 列が 0以外なら、その値を 0にするために、
		// i 行を何倍かしたものを XOR する
		for (j = rows - 1; j >= 0; j--){
			if (j == i)
				continue;	// 同じ行はとばす
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + pivot];	// j 行の pivot 列の値
			mat[row_start2 + pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + row_start, mat + row_start2, cols, factor);
		}
		row_start += cols;	// 次の行にずらす
		pivot++;
	}

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// マルチ・プロセッサー対応

typedef struct {	// RS threading control struct
	unsigned short *mat;	// 行列
	int cols;	// 横行の長さ
	volatile int start;	// 掛ける行の先頭位置
	volatile int pivot;	// 倍率となる値の位置
	volatile int skip;	// とばす行
	volatile int now;	// 消去する行
	HANDLE h;
	HANDLE run;
	HANDLE end;
} INV_TH;

// サブ・スレッド
static DWORD WINAPI thread_func(LPVOID lpParameter)
{
	unsigned short *mat;
	int j, cols, row_start2, factor;
	INV_TH *th;

	th = (INV_TH *)lpParameter;
	mat = th->mat;
	cols = th->cols;

	WaitForSingleObject(th->run, INFINITE);	// 計算開始の合図を待つ
	while (th->skip >= 0){
		while ((j = InterlockedDecrement(&(th->now))) >= 0){	// j = --th_now
			if (j == th->skip)
				continue;
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + th->pivot];	// j 行の pivot 列の値
			mat[row_start2 + th->pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + th->start, mat + row_start2, cols, factor);
		}
		SetEvent(th->end);	// 計算終了を通知する
		WaitForSingleObject(th->run, INFINITE);	// 計算開始の合図を待つ
	}

	// 終了処理
	CloseHandle(th->run);
	CloseHandle(th->end);
	return 0;
}

// マルチ・スレッドで逆行列を計算する (利用するパリティ・ブロックの所だけ)
static int invert_matrix_mt(unsigned short *mat,
	int rows,				// 横行の数、行列の縦サイズ、失われたソース・ブロックの数 = 利用するパリティ・ブロック数
	int cols,				// 縦列の数、行列の横サイズ、本来のソース・ブロック数
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報
{
	int j, row_start2, factor;
	unsigned int time_last = GetTickCount();
	INV_TH th[1];

	memset(th, 0, sizeof(INV_TH));

	// イベントを作成する
	th->run = CreateEvent(NULL, FALSE, FALSE, NULL);	// 両方とも Auto Reset にする
	if (th->run == NULL){
		print_win32_err();
		printf("error, inv-thread\n");
		return 1;
	}
	th->end = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (th->end == NULL){
		print_win32_err();
		CloseHandle(th->run);
		printf("error, inv-thread\n");
		return 1;
	}
	// サブ・スレッドを起動する
	th->mat = mat;
	th->cols = cols;
	th->h = (HANDLE)_beginthreadex(NULL, 65536, thread_func, (LPVOID)th, 0, NULL);
	if (th->h == NULL){
		print_win32_err();
		CloseHandle(th->run);
		CloseHandle(th->end);
		printf("error, inv-thread\n");
		return 1;
	}

	// Gaussian Elimination with 1 matrix
	th->pivot = 0;
	th->start = 0;	// その行の開始位置
	for (th->skip = 0; th->skip < rows; th->skip++){
		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((th->skip * 1000) / rows)){
				th->skip = -1;	// 終了指示
				SetEvent(th->run);
				WaitForSingleObject(th->h, INFINITE);
				CloseHandle(th->h);
				return 2;
			}
			time_last = GetTickCount();
		}

		// その行 (パリティ・ブロック) がどのソース・ブロックの代用か
		while ((th->pivot < cols) && (s_blk[th->pivot].exist != 0))
			th->pivot++;

		// Divide the row by element i,pivot
		factor = mat[th->start + th->pivot];
		if (factor > 1){
			mat[th->start + th->pivot] = 1;	// これが行列を一個で済ます手
			galois_region_divide(mat + th->start, cols, factor);
		} else if (factor == 0){	// factor = 0 だと、その行列の逆行列を計算できない
			th->skip = -1;	// 終了指示
			SetEvent(th->run);
			WaitForSingleObject(th->h, INFINITE);
			CloseHandle(th->h);
			return (0x00010000 | th->pivot);	// どのソース・ブロックで問題が発生したのかを返す
		}

		// 別の行の同じ pivot 列が 0以外なら、その値を 0にするために、
		// i 行を何倍かしたものを XOR する
		th->now = rows;	// 初期値 + 1
		SetEvent(th->run);	// サブ・スレッドに計算を開始させる
		while ((j = InterlockedDecrement(&(th->now))) >= 0){	// j = --th_now
			if (j == th->skip)	// 同じ行はとばす
				continue;
			row_start2 = cols * j;	// その行の開始位置
			factor = mat[row_start2 + th->pivot];	// j 行の pivot 列の値
			mat[row_start2 + th->pivot] = 0;	// これが行列を一個で済ます手
			// 先の計算により、i 行の pivot 列の値は必ず 1なので、この factor が倍率になる
			galois_region_multiply(mat + th->start, mat + row_start2, cols, factor);
		}

		WaitForSingleObject(th->end, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->start += cols;
		th->pivot++;
	}

	// サブ・スレッドを終了させる
	th->skip = -1;	// 終了指示
	SetEvent(th->run);
	WaitForSingleObject(th->h, INFINITE);
	CloseHandle(th->h);
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*
gflib の行列作成用関数や行列の逆変換用の関数を元にして、
計算のやり方を PAR 2.0 用に修正する。

par-v1.1.tar.gz に含まれる rs.doc
Dummies guide to Reed-Solomon coding. を参考にする
*/

/*
5 * 5 なら
 1   1    1     1     1     constant の 0乗
 2   4   16   128   256  <- この行の値を constant とする
 4  16  256 16384  4107     constant の 2乗
 8  64 4096  8566  7099     constant の 3乗
16 256 4107 43963  7166     constant の 4乗

par2-specifications.pdf によると、constant は 2の乗数で、
その指数は (n%3 != 0 && n%5 != 0 && n%17 != 0 && n%257 != 0) になる。
*/

// PAR 2.0 のパリティ検査行列はエンコード中にその場で生成する
// constant と facter の 2個のベクトルで表現する
// パリティ・ブロックごとに facter *= constant で更新していく
static void make_encode_constant(
	unsigned short *constant)	// constant を収めた配列
{
	unsigned short temp;
	int n, i;

	// constant は 2の乗数で、係数が3,5,17,257の倍数になるものは除く
	// 定数 2, 4, 16, 128, 256, 2048, 8192, ...
	n = 0;
	temp = 1;
	for (i = 0; i < source_num; i++){
		while (n <= 65535){
			temp = galois_multiply_fix(temp, 1);	// galois_multiply(temp, 2);
			n++;
			if ((n % 3 != 0) && (n % 5 != 0) && (n % 17 != 0) && (n % 257 != 0))
				break;
		}
		constant[i] = temp;
	}
}

// 復元用の行列を作る、十分な数のパリティ・ブロックが必要
static int make_decode_matrix(
	unsigned short *mat,	// 復元用の行列
	int block_lost,			// 横行、行列の縦サイズ、失われたソース・ブロックの数 = 必要なパリティ・ブロック数
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	parity_ctx_r *p_blk)	// 各パリティ・ブロックの情報
{
	unsigned short *id;		// 失われたソース・ブロックをどのパリティ・ブロックで代用したか
	unsigned short constant;
	int i, j, k, n;

	// printf("\n parity_num = %d, rows = %d, cols = %d \n", parity_num, block_lost, source_num);
	// 失われたソース・ブロックをどのパリティ・ブロックで代用するか
	id = mat + (block_lost * source_num);
	j = 0;
	for (i = 0; (i < parity_num) && (j < block_lost); i++){
		if (p_blk[i].exist == 1)	// 利用不可の印が付いてるブロックは無視する
			id[j++] = (unsigned short)i;
	}
	if (j < block_lost){	// パリティ・ブロックの数が足りなければ
		printf("need more recovery slice\n");
		return 1;
	}

	// 存在して利用するパリティ・ブロックだけの行列を作る
	n = 0;
	constant = 1;
	for (i = 0; i < source_num; i++){	// 一列ずつ縦に値をセットしていく
		while (n <= 65535){
			constant = galois_multiply_fix(constant, 1);	// galois_multiply(constant, 2);
			n++;
			if ((n % 3 != 0) && (n % 5 != 0) && (n % 17 != 0) && (n % 257 != 0))
				break;
		}
//		printf("\n[%5d], 2 pow %5d = %5d", i, n, constant);

		k = 0;
		for (j = 0; j < source_num; j++){	// j 行の i 列
			if (s_blk[j].exist == 0){	// 該当部分はパリティ・ブロックで補うのなら
				mat[source_num * k + i] = galois_power(constant, id[k]);
				k++;
			}
		}
	}

	if ((cpu_num == 1) || (source_num < 10) || (block_lost < 4)){	// 小さすぎる行列はマルチ・スレッドにしない
		k = invert_matrix_st(mat, block_lost, source_num, s_blk);
	} else {
		k = invert_matrix_mt(mat, block_lost, source_num, s_blk);
	}
	return k;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// リード・ソロモン符号を使ってエンコードする
int rs_encode(
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk)		// パリティ・ブロックの情報
{
	unsigned short *constant = NULL;
	int err = 0;
	unsigned int len;
#ifdef TIMER
unsigned int time_total = GetTickCount();
#endif

	if (galois_create_table()){
		printf("galois_create_table\n");
		return 1;
	}

	if (source_num == 1){	// ソース・ブロックが一個だけなら
		err = encode_method1(file_path, header_buf, rcv_hFile, files, s_blk, p_blk);
		goto error_end;
	}

	// パリティ計算用の行列演算の準備をする
	if (parity_num > source_num){
		len = sizeof(unsigned short) * (source_num + parity_num);
	} else {
		len = sizeof(unsigned short) * source_num * 2;
	}
	constant = malloc(len);
	if (constant == NULL){
		printf("malloc, %d\n", len);
		err = 1;
		goto error_end;
	}
#ifdef TIMER
	printf("\nmatrix size = %d.%d KB\n", len >> 10, (len >> 10) % 10);
#endif
	// パリティ検査行列の基になる定数
	make_encode_constant(constant);
//	for (len = 0; (int)len < source_num; len++)
//		printf("constant[%5d] = %5d\n", len, constant[len]);

#ifdef TIMER
	err = 0;	// IO method : 0=Auto, -2=Read all, -3=Read some, -4=GPU all, -5=GPU some
	if (err == 0){
#endif
	if ((OpenCL_method != 0) && (block_size >= 65536) && (source_num >= 256) && (parity_num >= 32) &&
			((source_num + parity_num) * (__int64)block_size > 1048576 * 256)){
		// ブロック数が多いなら、ブロックごとにスレッドを割り当てる (GPU を使う)
		if (read_block_num(parity_num, GPU_MAX_NUM + 1, 0) != 0){
			err = -5;	// メモリーが足りてるなら Read some 方式を使う
		} else {
			err = -4;	// メモリー不足なら Read all 方式でブロックを断片化させる
		}
	} else {
		// ソース・ブロックを全て断片的に読み込むか、いくつかを丸ごと読み込むかを決める
		if (read_block_num(parity_num, cpu_num - 1, 0) != 0){
			err = -3;	// メモリーが足りてるなら Read some 方式を使う
		} else {
			err = -2;	// メモリー不足なら Read all 方式でブロックを断片化させる
		}
	}
#ifdef TIMER
	}
#endif

	// ファイル・アクセスの方式によって分岐する
	if (err == -5)	// 最初の二つは GPUを使い、無理なら次に移る
		err = encode_method5(file_path, header_buf, rcv_hFile, files, s_blk, p_blk, constant);
	if (err == -4)
		err = encode_method4(file_path, header_buf, rcv_hFile, files, s_blk, p_blk, constant);
	if (err == -3)	// ソース・データをいくつか読み込む場合
		err = encode_method3(file_path, header_buf, rcv_hFile, files, s_blk, p_blk, constant);
	if (err == -2)	// ソース・データを全て読み込む場合
		err = encode_method2(file_path, header_buf, rcv_hFile, files, s_blk, p_blk, constant);
#ifdef TIMER
	if (err != 1){
		time_total = GetTickCount() - time_total;
		printf("total  %d.%03d sec\n", time_total / 1000, time_total % 1000);
	}
#endif

error_end:
	if (constant)
		free(constant);
	galois_free_table();	// Galois Field のテーブルを解放する
	return err;
}

// リード・ソロモン符号を使ってデコードする
int rs_decode(
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk)	// パリティ・ブロックの情報
{
	unsigned short *mat = NULL, *id;
	int err = 0, i, j, k;
	unsigned int len;
#ifdef TIMER
unsigned int time_matrix = 0, time_total = GetTickCount();
#endif

	if (galois_create_table()){
		printf("galois_create_table\n");
		return 1;
	}

	if (source_num == 1){	// ソース・ブロックが一個だけなら
		err = decode_method1(file_path, rcv_hFile, files, s_blk, p_blk);
		goto error_end;
	}

	// 復元用の行列演算の準備をする
	len = sizeof(unsigned short) * block_lost * (source_num + 1);
	mat = malloc(len);
	if (mat == NULL){
		printf("malloc, %d\n", len);
		printf("matrix for recovery is too large\n");
		err = 1;
		goto error_end;
	}
#ifdef TIMER
	if (len & 0xFFF00000){
		printf("\nmatrix size = %d.%d MB\n", len >> 20, (len >> 20) % 10);
	} else {
		printf("\nmatrix size = %d.%d KB\n", len >> 10, (len >> 10) % 10);
	}
#endif
	// 何番目の消失ソース・ブロックがどのパリティで代替されるか
	id = mat + (block_lost * source_num);

#ifdef TIMER
time_matrix = GetTickCount();
#endif
	// 復元用の行列を計算する
	print_progress_text(0, "Computing matrix");
	err = make_decode_matrix(mat, block_lost, s_blk, p_blk);
	while (err >= 0x00010000){	// 逆行列を計算できなかった場合 ( Petr Matas の修正案を参考に実装)
		printf("\n");
		err ^= 0x00010000;	// エラーが起きた行 (ソース・ブロックの番号)
		printf("fail at input slice %d\n", err);
		k = 0;
		for (i = 0; i < err; i++){
			if (s_blk[i].exist == 0)
				k++;
		}
		// id[k] エラーが起きた行に対応するパリティ・ブロックの番号
		p_blk[id[k]].exist = 0x100;	// そのパリティ・ブロックを使わないようにする
		printf("disable recovery slice %d\n", id[k]);
		j = 0;
		for (i = 0; i < parity_num; i++){
			if (p_blk[i].exist == 1)
				j++;	// 利用可能なパリティ・ブロックの数
		}
		if (j >= block_lost){	// 使えるパリティ・ブロックの数が破損ブロックの数以上なら
			print_progress_text(0, "Computing matrix");
			err = make_decode_matrix(mat, block_lost, s_blk, p_blk);
		} else {	// 代替するパリティ・ブロックの数が足りなければ
			printf("fail at recovery slice");
			for (i = 0; i < parity_num; i++){
				if (p_blk[i].exist == 0x100)
					printf(" %d", i);
			}
			printf("\n");
			err = 1;
		}
	}
	if (err)	// それ以外のエラーなら
		goto error_end;
	print_progress_done();	// 改行して行の先頭に戻しておく
	//for (i = 0; i < block_lost; i++)
	//	printf("id[%d] = %d\n", i, id[i]);
#ifdef TIMER
time_matrix = GetTickCount() - time_matrix;
#endif

#ifdef TIMER
	err = 0;	// IO method : 0=Auto, -2=Read all, -3=Read some, -4=GPU all, -5=GPU some
	if (err == 0){
#endif
	if ((OpenCL_method != 0) && (block_size >= 65536) && (source_num >= 256) && (block_lost >= 32) &&
			((source_num + block_lost) * (__int64)block_size > 1048576 * 256)){
		// ブロック数が多いなら、ブロックごとにスレッドを割り当てる (GPU を使う)
		if (read_block_num(block_lost, GPU_MAX_NUM + 1, 0) != 0){
			err = -5;	// メモリーが足りてるなら Read some 方式を使う
		} else {
			err = -4;	// メモリー不足なら Read all 方式でブロックを断片化させる
		}
	} else {
		// ソース・ブロックを全て断片的に読み込むか、いくつかを丸ごと読み込むかを決める
		if (read_block_num(block_lost, cpu_num - 1, 0) != 0){
			err = -3;	// メモリーが足りてるなら Read some 方式を使う
		} else {
			err = -2;	// メモリー不足なら Read all 方式でブロックを断片化させる
		}
	}
#ifdef TIMER
	}
#endif

	// ファイル・アクセスの方式によって分岐する
	if (err == -5)
		err = decode_method5(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
	if (err == -4)
		err = decode_method4(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
	if (err == -3)	// ソース・データをいくつか読み込む場合
		err = decode_method3(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
	if (err == -2)	// ソース・データを全て読み込む場合
		err = decode_method2(file_path, block_lost, rcv_hFile, files, s_blk, p_blk, mat);
#ifdef TIMER
	if (err != 1){
		time_total = GetTickCount() - time_total;
		printf("total  %d.%03d sec\n", time_total / 1000, time_total % 1000);
		printf("matrix %d.%03d sec\n", time_matrix / 1000, time_matrix % 1000);
	}
#endif

error_end:
	if (mat)
		free(mat);
	galois_free_table();	// Galois Field のテーブルを解放する
	return err;
}

