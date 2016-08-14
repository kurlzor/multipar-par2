// rs_encode.c
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
#include "reedsolomon.h"
#include "rs_encode.h"


#ifdef TIMER
static unsigned int time_start, time_read = 0, time_write = 0, time_calc = 0;
static unsigned int read_count = 0, write_count = 0, skip_count = 0;
#endif

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// 非同期 IO

typedef struct {	// RS threading control struct
	unsigned short *mat;	// 行列
	unsigned char *buf;
	volatile unsigned int size;		// バイト数
	volatile int count;
	volatile int off;
	volatile int now;
	HANDLE run;
	HANDLE end;
} RS_TH;

static DWORD WINAPI thread_encode2(LPVOID lpParameter)
{
	unsigned char *buf, *p_buf, *work_buf;
	unsigned short *factor;
	int i, j, th_id, part_num, chunk_num, max_num, left_num, left_max;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	factor = th->mat;
	buf = th->buf;
	unit_size = th->size;
	part_num = th->count;
	chunk_size = th->off;
	th_id = th->now;	// スレッド番号
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	p_buf = buf + ((size_t)unit_size * (source_num + cpu_num - 1));
	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * part_num;
	if (th_id > 0)
		work_buf = buf + ((size_t)unit_size * (source_num + th_id - 1));

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		i = th->off;	// ソース・ブロック番号
		len = chunk_size;
		if (th->buf == NULL){	// ソース・ブロック読み込み中
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / part_num;	// chunk の番号
				j = j % part_num;	// parity の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (i == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(buf + ((size_t)unit_size * i + off), p_buf + ((size_t)unit_size * j + off), len, factor[j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// パリティ・ブロック書き込み中
			if (th_id == 0){
				work_buf = th->buf;
				if (i == 0)	// 最初から計算する場合はブロックを 0で埋める
					memset(work_buf, 0, unit_size);
			} else {
				memset(work_buf, 0, unit_size);	// ブロックを 0で埋める
			}
			left_num = source_num - i;	// 残りブロック数
			left_max = chunk_num * left_num;
			// ソース・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < left_max){	// j = ++th_now
				off = j / left_num;	// chunk の番号
				j = j % left_num;	// source の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				galois_align_multiply(buf + ((size_t)unit_size * (i + j) + off), work_buf + off, len, factor[i + j]);
#ifdef TIMER
loop_count2b++;
#endif
			}
#ifdef TIMER
time_encode2b += GetTickCount() - time_start2;
#endif
		}
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
printf("sub-thread[%d] : total loop = %d\n", th_id, loop_count2a + loop_count2b);
printf(" 1st encode %d.%03d sec, %d loop\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a);
printf(" 2nd encode %d.%03d sec, %d loop\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

static DWORD WINAPI thread_encode3(LPVOID lpParameter)
{
	unsigned char *buf, *p_buf, *work_buf;
	unsigned short *factor;
	int i, j, th_id, chunk_num, max_num, left_num, left_max;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	factor = th->mat;
	buf = th->buf;
	left_num = th->count;
	chunk_size = th->off;
	th_id = th->now;	// スレッド番号
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);
	p_buf = buf + (size_t)unit_size * (left_num + cpu_num - 1);
	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * parity_num;
	if (th_id > 0)
		work_buf = buf + (size_t)unit_size * (left_num + th_id - 1);

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		i = th->off;	// ソース・ブロック番号
		len = chunk_size;
		if (th->buf == NULL){	// ソース・ブロック読み込み中
			left_num = th->count + i;
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / parity_num;	// chunk の番号
				j = j % parity_num;		// parity の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (left_num == 0)	// 最初のブロックを計算する際にブロックを 0で埋める
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(buf + ((size_t)unit_size * i + off), p_buf + ((size_t)unit_size * j + off), len, factor[j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// パリティ・ブロック書き込み中
			if (th_id == 0){
				work_buf = th->buf;
			} else {
				memset(work_buf, 0, unit_size);	// ブロックを 0で埋める
			}
			left_num = th->count - i;	// 残りブロック数
			left_max = chunk_num * left_num;
			// ソース・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < left_max){	// j = ++th_now
				off = j / left_num;	// chunk の番号
				j = j % left_num;	// source の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				galois_align_multiply(buf + ((size_t)unit_size * (i + j) + off), work_buf + off, len, factor[i + j]);
#ifdef TIMER
loop_count2b++;
#endif
			}
#ifdef TIMER
time_encode2b += GetTickCount() - time_start2;
#endif
		}
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
printf("sub-thread[%d] : total loop = %d\n", th_id, loop_count2a + loop_count2b);
printf(" 1st encode %d.%03d sec, %d loop\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a);
printf(" 2nd encode %d.%03d sec, %d loop\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int encode_method1(	// ソース・ブロックが一個だけの場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk)		// パリティ・ブロックの情報
{
	unsigned char *buf = NULL, *work_buf, *hash;
	int err = 0, i, j;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	PHMD5 md_ctx, *md_ptr = NULL;

	// 作業バッファーを確保する
	len = 0;
	io_size = get_io_size(2, &len, 1);
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1)) - HASH_SIZE;	// 実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	len = 2 * unit_size + HASH_SIZE;
	buf = _aligned_malloc(len, sse_unit);
	if (buf == NULL){
		printf("malloc, %d\n", len);
		err = 1;
		goto error_end;
	}
	work_buf = buf + unit_size;
	hash = work_buf + unit_size;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_base *= parity_num;	// 全体の断片の個数
#ifdef TIMER
	printf("\n read one source block, and keep one parity block\n");
	printf("buffer size = %d MB, io_size = %d, split = %d\n", len >> 20, io_size, (block_size + io_size - 1) / io_size);
	j = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, j, (unit_size + j - 1) / j);
#endif

	if (io_size < block_size){	// スライスが分割される場合だけ、途中までのハッシュ値を保持する
		len = sizeof(PHMD5) * parity_num;
		md_ptr = malloc(len);
		if (md_ptr == NULL){
			printf("malloc, %d\n", len);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++){
			Phmd5Begin(&(md_ptr[i]));
			j = first_num + i;	// 最初の番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			Phmd5Process(&(md_ptr[i]), header_buf + 32, 36);
		}
	}

	// ソース・ファイルを開く
	wcscpy(file_path, base_dir);
	wcscpy(file_path + base_len, list_buf + files[s_blk[0].file].name);
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		hFile = NULL;
		printf_cp("cannot open file, %s\n", list_buf + files[s_blk[0].file].name);
		err = 1;
		goto error_end;
	}

	// バッファー・サイズごとにパリティ・ブロックを作成する
	time_last = GetTickCount();
	s_blk[0].crc = 0xFFFFFFFF;
	block_off = 0;
	while (block_off < block_size){
#ifdef TIMER
time_start = GetTickCount();
#endif
		// ソース・ブロックを読み込む
		len = s_blk[0].size - block_off;
		if (len > io_size)
			len = io_size;
		if (file_read_data(hFile, (__int64)block_off, buf, len)){
			printf("file_read_data, input slice %d\n", i);
			err = 1;
			goto error_end;
		}
		if (len < io_size)
			memset(buf + len, 0, io_size - len);
		// ソース・ブロックのチェックサムを計算する
		s_blk[0].crc = crc_update(s_blk[0].crc, buf, len);	// without pad
		checksum16_altmap(buf, buf + io_size, io_size);
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		// パリティ・ブロックごとに
		for (i = 0; i < parity_num; i++){
#ifdef TIMER
time_start = GetTickCount();
#endif
			memset(work_buf, 0, unit_size);
			// factor は 2の乗数になる
			galois_align_multiply(buf, work_buf, unit_size, galois_power(2, first_num + i));
#ifdef TIMER
time_calc += GetTickCount() - time_start;
#endif

			// 経過表示
			prog_num++;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((prog_num * 1000) / prog_base)){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

#ifdef TIMER
time_start = GetTickCount();
#endif
			// パリティ・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			// ハッシュ値を計算して、リカバリ・ファイルに書き込む
			if (io_size >= block_size){	// 1回で書き込みが終わるなら
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初の番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, len);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
			} else {
				Phmd5Process(&(md_ptr[i]), work_buf, len);
			}
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off, work_buf, len)){
				printf("file_write_data, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
		}

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	if (s_blk[0].size < block_size){	// 残りを 0 でパディングする
		len = block_size - s_blk[0].size;
		memset(buf, 0, len);
		s_blk[0].crc = crc_update(s_blk[0].crc, buf, len);
	}
	s_blk[0].crc ^= 0xFFFFFFFF;
	if (((unsigned int *)(files[s_blk[0].file].hash))[0] != s_blk[0].crc){
		printf("checksum mismatch, input file %d\n", s_blk[0].file);
		err = 1;
		goto error_end;
	}

	if (io_size < block_size){	// 1回で書き込みが終わらなかったなら
		if (GetTickCount() - time_last >= UPDATE_TIME){	// キャンセルを受け付ける
			if (cancel_progress()){
				err = 2;
				goto error_end;
			}
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 最後に Recovery Slice packet のヘッダーを書き込む
		for (i = 0; i < parity_num; i++){
			Phmd5End(&(md_ptr[i]));
			memcpy(header_buf + 16, md_ptr[i].hash, 16);
			j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			// リカバリ・ファイルに書き込む
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){	// ヘッダーのサイズ分だけずらす
				printf("file_write_data, packet header\n");
				err = 1;
				goto error_end;
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
printf("encode %d.%03d sec\n", time_calc / 1000, time_calc % 1000);
#endif

error_end:
	if (md_ptr)
		free(md_ptr);
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

int encode_method2(	// ソース・データを全て読み込む場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *sub_buf, *hash;
	unsigned short *factor;
	int err = 0, i, j, last_file, part_num;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 md_ctx, *md_ptr = NULL;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor = constant + source_num;

	// 作業バッファーを確保する
	part_num = source_num >> PART_MAX_RATE;	// ソース・ブロック数に対する割合で最大量を決める
	if (part_num > parity_num)
		part_num = parity_num;
	i = source_num + (cpu_num - 1);	// ソース・ブロックとサブ・スレッドの作業領域が必要
	io_size = get_io_size(i, &part_num, 1);
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1)) - HASH_SIZE;	// 実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (i + part_num) * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, sse_unit);
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	sub_buf = buf + (size_t)unit_size * source_num;	// サブ・スレッドの作業領域
	p_buf = sub_buf + (size_t)unit_size * (cpu_num - 1);	// パリティ・ブロックを部分的に記録する領域
	hash = p_buf + (size_t)unit_size * part_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_base *= source_num * parity_num;	// 全体の断片の個数
#ifdef TIMER
	printf("\n read all source blocks, and keep some parity blocks\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	len = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %I64d, part_num = %d\n", prog_base, part_num);
#endif

	if (io_size < block_size){	// スライスが分割される場合だけ、途中までのハッシュ値を保持する
		len = sizeof(PHMD5) * parity_num;
		md_ptr = malloc(len);
		if (md_ptr == NULL){
			printf("malloc, %d\n", len);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++){
			Phmd5Begin(&(md_ptr[i]));
			j = first_num + i;	// 最初の番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			Phmd5Process(&(md_ptr[i]), header_buf + 32, 36);
		}
	}

	// マルチ・スレッドの準備をする
	th->mat = factor;
	th->buf = buf;
	th->size = unit_size;
	th->count = part_num;
	th->off = try_cache_blocking(unit_size);	// キャッシュの最適化を試みる
	for (j = 0; j < cpu_num; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		th->now = j;	// スレッド番号
		hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_encode2, (LPVOID)th, 0, NULL);
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		if (j >= (cpu_num * 3) / 4)	// IO が延滞しないように、サブ・スレッドの優先度を下げる
			SetThreadPriority(hSub[j], THREAD_PRIORITY_BELOW_NORMAL);
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// バッファー・サイズごとにパリティ・ブロックを作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->buf = NULL;
		th->off = -1;	// まだ計算して無い印

		// ソース・ブロックを読み込む
#ifdef TIMER
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < source_num; i++){
			if (s_blk[i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
				file_off = block_off;
			} else {	// 同じファイルならブロック・サイズ分ずらす
				file_off += block_size;
			}
			if (s_blk[i].size > block_off){	// バッファーにソース・ファイルの内容を読み込む
				len = s_blk[i].size - block_off;
				if (len > io_size)
					len = io_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", i);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				// ソース・ブロックのチェックサムを計算する
				if (block_off == 0)
					s_blk[i].crc = 0xFFFFFFFF;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf + (size_t)unit_size * i, len);	// without pad
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif

				if (i + 1 < source_num){	// 最後のブロック以外なら
					// サブ・スレッドの動作状況を調べる
					j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
					if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
						// 経過表示
						prog_num += part_num;
						if (GetTickCount() - time_last >= UPDATE_TIME){
							if (print_progress((int)((prog_num * 1000) / prog_base))){
								err = 2;
								goto error_end;
							}
							time_last = GetTickCount();
						}
						// 計算終了したブロックの次から計算を開始する
						th->off += 1;
						if (th->off > 0){	// バッファーに読み込んだ時だけ計算する
							while (s_blk[th->off].size <= block_off){
								prog_num += part_num;
								th->off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						for (j = 0; j < part_num; j++)
							factor[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
						th->now = -1;	// 初期値 - 1
						for (j = 0; j < (cpu_num + 1) / 2; j++){	// 読み込み中はスレッド数を減らす
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
					}
				}
			} else {
				memset(buf + (size_t)unit_size * i, 0, unit_size);
			}
		}
		// 最後のソース・ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off > 0){
			while (s_blk[th->off].size <= block_off){	// 計算不要なソース・ブロックはとばす
				prog_num += part_num;
				th->off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		}
#ifdef TIMER
		j = (int)(((__int64)(th->off * part_num) * 1000) / (source_num * parity_num));
		printf("partial encode = %d (%d.%d%%), read = %d, skip = %d\n", th->off, j / 10, j % 10, read_count, skip_count);
		// ここまでのパリティ・ブロックのチェックサムを検証する
		for (j = 0; j < part_num; j++){
			checksum16_return(p_buf + (size_t)unit_size * j, hash, io_size);
			if (memcmp(p_buf + ((size_t)unit_size * j + io_size), hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d after 1st encode\n", j);
				err = 1;
			}
			galois_altmap_change(p_buf + (size_t)unit_size * j, unit_size);
		}
		if (err == 1)
			goto error_end;
#endif

		// 経過表示
		prog_num += source_num - th->off;
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress((int)((prog_num * 1000) / prog_base))){
				err = 2;
				goto error_end;
			}
			time_last = GetTickCount();
		}

		// 最初のパリティの計算を始めておく
		work_buf = p_buf;
		for (j = 0; j < source_num; j++){
			if (s_blk[j].size <= block_off){
				factor[j] = 0;	// 掛け算しない
			} else {
				factor[j] = galois_power(constant[j], first_num);	// factor は定数行列の乗数になる
			}
		}
		th->buf = work_buf;
		th->now = -1;	// 初期値 - 1
		for (j = 0; j < cpu_num; j++){
			ResetEvent(hEnd[j]);	// リセットしておく
			SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
		}

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		// パリティ・ブロックごとに
		for (i = 0; i < parity_num; i++){
			// パリティの計算が終わるのを待って完成させる
			WaitForMultipleObjects(cpu_num, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
			for (j = 1; j < cpu_num; j++){
				// 最後にサブ・スレッドのバッファーを XOR する
				galois_align_multiply(sub_buf + (size_t)unit_size * (j - 1), work_buf, unit_size, 1);
			}
			if (i + 1 < parity_num){	// 最後のブロック以外なら
				if (i + 1 >= part_num){	// 部分的なエンコードを施した箇所以降なら
					if (th->buf != p_buf){	// エンコードと書き込みでダブル・バッファリングする
						th->buf = p_buf;
						th->off = 0;	// 最初のソース・ブロックから計算する
					} else {
						th->buf += unit_size;
					}
				} else {
					th->buf += unit_size;
				}
				// 経過表示
				prog_num += source_num - th->off;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((int)((prog_num * 1000) / prog_base))){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
				// 次のパリティの計算を開始しておく
				for (j = 0; j < source_num; j++){
					if (s_blk[j].size <= block_off){
						factor[j] = 0;	// 掛け算しない
					} else {
						factor[j] = galois_power(constant[j], first_num + i + 1);	// factor は定数行列の乗数になる
					}
				}
				th->now = -1;	// 初期値 - 1
				for (j = 0; j < cpu_num; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}
			}

#ifdef TIMER
time_start = GetTickCount();
#endif
			// パリティ・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			// ハッシュ値を計算して、リカバリ・ファイルに書き込む
			if (io_size >= block_size){	// 1回で書き込みが終わるなら
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初の番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, len);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
			} else {
				Phmd5Process(&(md_ptr[i]), work_buf, len);
			}
			//printf("%d, buf = %p, size = %u, off = %I64d\n", i, work_buf, len, p_blk[i].off + block_off);
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off, work_buf, len)){
				printf("file_write_data, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

			if (i + 1 >= part_num) {	// 部分的なエンコードを施した箇所以降なら
				if (work_buf != p_buf){	// エンコードと書き込みでダブル・バッファリングする
					work_buf = p_buf;
				} else {
					work_buf += unit_size;
				}
			} else {
				work_buf += unit_size;
			}
		}

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	memset(buf, 0, io_size);
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		part_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		i = j + part_num - 1;	// 末尾ブロックの番号
		if (s_blk[i].size < block_size){	// 残りを 0 でパディングする
			len = block_size - s_blk[i].size;
			while (len > io_size){
				len -= io_size;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf, io_size);
			}
			s_blk[i].crc = crc_update(s_blk[i].crc, buf, len);
		}
		memset(hash, 0, 16);
		for (i = 0; i < part_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc ^ 0xFFFFFFFF;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += part_num;
	}

	if (io_size < block_size){	// 1回で書き込みが終わらなかったなら
		if (GetTickCount() - time_last >= UPDATE_TIME){	// キャンセルを受け付ける
			if (cancel_progress()){
				err = 2;
				goto error_end;
			}
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 最後に Recovery Slice packet のヘッダーを書き込む
		for (i = 0; i < parity_num; i++){
			Phmd5End(&(md_ptr[i]));
			memcpy(header_buf + 16, md_ptr[i].hash, 16);
			j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			// リカバリ・ファイルに書き込む
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){	// ヘッダーのサイズ分だけずらす
				printf("file_write_data, packet header\n");
				err = 1;
				goto error_end;
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
#endif

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (md_ptr)
		free(md_ptr);
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

int encode_method3(	// パリティ・ブロックを全て保持できる場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *sub_buf, *hash;
	unsigned short *factor;
	int err = 0, i, j, last_file, source_off, read_num;
	unsigned int unit_size, len;
	unsigned int time_last, prog_num = 0, prog_base;
	size_t mem_size;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];
	PHMD5 md_ctx;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor = constant + source_num;
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);	// チェックサムの分だけ増やす

	// 作業バッファーを確保する
	read_num = read_block_num(parity_num, cpu_num - 1, 1);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
		//printf("\n cannot keep enough blocks, use another method\n");
		return -2;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 2) / 3 + 1;	// 実験用
	mem_size = read_num + parity_num + (cpu_num - 1);	// パリティ・ブロックとサブ・スレッドの作業領域が必要
	mem_size = mem_size * unit_size + HASH_SIZE;
	buf = _aligned_malloc(mem_size, sse_unit);
	if (buf == NULL){
		printf("malloc, %Id\n", mem_size);
		err = 1;
		goto error_end;
	}
	sub_buf = buf + (size_t)unit_size * read_num;	// サブ・スレッドの作業領域
	p_buf = sub_buf + (size_t)unit_size * (cpu_num - 1);	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * parity_num;
	prog_base = source_num * parity_num;	// ブロックの合計掛け算個数
#ifdef TIMER
	printf("\n read some source blocks, and keep all parity blocks\n");
	printf("buffer size = %Id MB, read_num = %d, round = %d\n", mem_size >> 20, read_num, (source_num + read_num - 1) / read_num);
	len = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %d\n", prog_base);
#endif

	// マルチ・スレッドの準備をする
	th->mat = factor;
	th->buf = buf;
	th->count = read_num;
	th->off = try_cache_blocking(unit_size);	// キャッシュの最適化を試みる
	for (j = 0; j < cpu_num; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		th->now = j;	// スレッド番号
		hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_encode3, (LPVOID)th, 0, NULL);
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		if (j >= (cpu_num * 3) / 4)	// IO が延滞しないように、サブ・スレッドの優先度を下げる
			SetThreadPriority(hSub[j], THREAD_PRIORITY_BELOW_NORMAL);
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// 何回かに別けてソース・ブロックを読み込んで、パリティ・ブロックを少しずつ作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	last_file = -1;
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->buf = NULL;
		th->count = source_off;
		th->off = -1;	// まだ計算して無い印

#ifdef TIMER
time_start = GetTickCount();
#endif
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			// ソース・ブロックを読み込む
			if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[source_off + i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
			}
			// バッファーにソース・ファイルの内容を読み込む
			len = s_blk[source_off + i].size;
			if (!ReadFile(hFile, buf + (size_t)unit_size * i, len, &j, NULL) || (len != j)){
				print_win32_err();
				err = 1;
				goto error_end;
			}
			if (len < block_size)
				memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
			// ソース・ブロックのチェックサムを計算する
			s_blk[source_off + i].crc = crc_update(0xFFFFFFFF, buf + (size_t)unit_size * i, block_size) ^ 0xFFFFFFFF;	// include pad
			checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);

			if (i + 1 < read_num){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += parity_num;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress(((__int64)prog_num * 1000) / prog_base)){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 計算終了したブロックの次から計算を開始する
					th->off += 1;
					for (j = 0; j < parity_num; j++)
						factor[j] = galois_power(constant[source_off + th->off], first_num + j);	// factor は定数行列の乗数になる
					th->now = -1;	// 初期値 - 1
					for (j = 0; j < (cpu_num + 1) / 2; j++){	// 読み込み中はスレッド数を減らす
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}
		}
		if (source_off + i == source_num - 1){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (source_off + th->off == 0)	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * parity_num);
#ifdef TIMER
		j = (th->off * 1000) / read_num;
		printf("partial encode = %d (%d.%d%%)\n", th->off, j / 10, j % 10);
		// ここまでのパリティ・ブロックのチェックサムを検証する
		for (j = 0; j < parity_num; j++){
			checksum16_return(p_buf + (size_t)unit_size * j, hash, unit_size - HASH_SIZE);
			if (memcmp(p_buf + ((size_t)unit_size * j + unit_size - HASH_SIZE), hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d after 1st encode\n", j);
				err = 1;
			}
			galois_altmap_change(p_buf + (size_t)unit_size * j, unit_size);
		}
		if (err == 1)
			goto error_end;
#endif

		// 経過表示
		prog_num += read_num - th->off;
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress(((__int64)prog_num * 1000) / prog_base)){
				err = 2;
				goto error_end;
			}
			time_last = GetTickCount();
		}

		// 最初のパリティの計算を始めておく
		work_buf = p_buf;
		th->count = read_num;
		for (j = 0; j < read_num; j++)
			factor[j] = galois_power(constant[source_off + j], first_num);	// factor は定数行列の乗数になる
		th->buf = work_buf;
		th->now = -1;	// 初期値 - 1
		for (j = 0; j < cpu_num; j++){
			ResetEvent(hEnd[j]);	// リセットしておく
			SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
		}

		// パリティ・ブロックごとに
		for (i = 0; i < parity_num; i++){
			// パリティの計算が終わるのを待って完成させる
			WaitForMultipleObjects(cpu_num, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
			for (j = 1; j < cpu_num; j++){
				// 最後にサブ・スレッドのバッファーを XOR する
				galois_align_multiply(sub_buf + (size_t)unit_size * (j - 1), work_buf, unit_size, 1);
			}
			if (i + 1 < parity_num){	// 最後のブロック以外なら
				// 経過表示
				prog_num += read_num - th->off;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress(((__int64)prog_num * 1000) / prog_base)){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
				// 次のパリティの計算を開始しておく
				for (j = 0; j < read_num; j++)
					factor[j] = galois_power(constant[source_off + j], first_num + i + 1);	// factor は定数行列の乗数になる
				th->buf += unit_size;
				th->now = -1;	// 初期値 - 1
				for (j = 0; j < cpu_num; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}
			}

			if (source_off + read_num >= source_num){	// 最後の書き込みなら
#ifdef TIMER
time_start = GetTickCount();
#endif
				// パリティ・ブロックのチェックサムを検証する
				checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
				if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				// ハッシュ値を計算して、リカバリ・ファイルに書き込む
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, block_size);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				//printf("%d, buf = %p, off = %I64d\n", i, work_buf, p_blk[i].off);
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off, work_buf, block_size)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
#ifdef TIMER
time_write += GetTickCount() - time_start;
			} else {	// パリティ・ブロックのチェックサムだけ検証する
				checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
				if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				galois_altmap_change(work_buf, unit_size);
#endif
			}

			work_buf += unit_size;
		}

		source_off += read_num;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		read_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		memset(hash, 0, 16);
		for (i = 0; i < read_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += read_num;
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
#endif

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// GPU 対応の非同期 IO

// ブロックごとに計算するためのスレッド
static DWORD WINAPI thread_encode_each(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *constant, *factor1, *factor2;
	int i, j, th_id, src_start, src_num, *last_id, max_num;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	constant = th->mat;
	p_buf = th->buf;
	unit_size = th->size;
	chunk_size = th->off;
	th_id = th->now;	// スレッド番号
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	factor2 = (unsigned short *)(p_buf + ((size_t)unit_size * parity_num + HASH_SIZE));
	if (th_id == 0){	// 0番のスレッドだけ特別
		factor2 += source_num * (cpu_num - 1);
	} else {	// スレッドごとに保存場所を変える
		factor2 += source_num * (th_id - 1);
	}
	last_id = (int *)(constant + ((source_num + 1) & ~1));
	last_id += th_id;	// 最後に計算したブロック番号を記録しておく
	factor1 = constant + source_num;
	max_num = ((unit_size + chunk_size - 1) / chunk_size) * parity_num;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_start = th->off;	// ソース・ブロック番号

/*
		if (th->size == 1){	// ソース・ブロック読み込み中 (chunk に分割しない)
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + (size_t)unit_size * j, 0, unit_size);	// ブロックを 0で埋める
				galois_align_multiply(s_buf, p_buf + (size_t)unit_size * j,
						unit_size, galois_power(constant[src_start], first_num + j));
#ifdef TIMER
loop_count2a++;
#endif
			}
*/

/*
		if (th->size == 1){	// ソース・ブロック読み込み中 (Single Thread)
			len = chunk_size;
			off = 0;
			while (off < unit_size){
				// パリティ・ブロックごとに掛け算して追加していく
				for (j = 0; j < parity_num; j++){
					if (off + len > unit_size)
						len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
					if (src_start == 0)	// 最初のブロックを計算する際に
						memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
					galois_align_multiply(s_buf + off, p_buf + ((size_t)unit_size * j + off), len, factor1[j]);
#ifdef TIMER
loop_count2a++;
#endif
				}
				off += chunk_size;
			}
*/

		if (th->size == 1){	// ソース・ブロック読み込み中
			len = chunk_size;
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / parity_num;	// chunk の番号
				j = j % parity_num;		// parity の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(s_buf + off, p_buf + ((size_t)unit_size * j + off), len, factor1[j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {
			// スレッドごとに作成するパリティ・ブロックを変える
			src_num = th->count;
			*last_id = 0;
			while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
				work_buf = p_buf + (size_t)unit_size * j;

				// factor は定数行列の乗数になる
				for (i = 0; i < src_num; i++)
					factor2[i] = galois_power(constant[src_start + i], first_num + j);

				// chunk に分割して計算する
				len = chunk_size;
				off = 0;
				while (off < unit_size){
					// ソース・ブロックごとにパリティを追加していく
					for (i = 0; i < src_num; i++)
						galois_align_multiply(s_buf + ((size_t)unit_size * i + off), work_buf, len, factor2[i]);

					work_buf += len;
					off += len;
					if (off + len > unit_size)
						len = unit_size - off;
				}

				*last_id = j;	// 計算が終わったブロックの番号
#ifdef TIMER
loop_count2b += src_num;
#endif
			}
#ifdef TIMER
time_encode2b += GetTickCount() - time_start2;
#endif
		}
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
loop_count2a /= (unit_size + chunk_size - 1) / chunk_size;	// chunk数で割ってブロック数にする
printf("sub-thread[%d] : total loop = %d\n", th_id, loop_count2a + loop_count2b);
printf(" 1st encode %d.%03d sec, %d loop\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a);
printf(" 2nd encode %d.%03d sec, %d loop\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

// GPU を制御するためのスレッド (スレッド番号は 0 にすること)
static DWORD WINAPI thread_encode_gpu(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf;
	unsigned short *constant, *factor2;
	int i, j, k, src_start, src_num, gpu_max, gpu_num, *last_id;
	unsigned int unit_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int time_start2, time_encode2 = 0, loop_count2 = 0;
#endif

	th = (RS_TH *)lpParameter;
	constant = th->mat;
	p_buf = th->buf;
	unit_size = th->size;
	gpu_max = th->now;
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	factor2 = (unsigned short *)(p_buf + ((size_t)unit_size * parity_num + HASH_SIZE));
	factor2 += source_num * (cpu_num - 1);	// GPU のスレッドだけ特別
	last_id = (int *)(constant + ((source_num + 1) & ~1));

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		src_start = th->off;	// ソース・ブロック番号

/*
		if (th->size == 1){	// ソース・ブロック読み込み中は GPU を使わない
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < parity_num){	// j = ++th_now
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + (size_t)unit_size * j, 0, unit_size);	// ブロックを 0で埋める
				galois_align_multiply(s_buf, p_buf + (size_t)unit_size * j,
						unit_size, galois_power(constant[src_start], first_num + j));
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {
*/
		// GPU は複数のブロックを同時に計算する
		src_num = th->count;
		*last_id = 0;
		while ((j = InterlockedExchangeAdd(&(th->now), gpu_max)) < parity_num - 1){	// th_now += gpu_max
			// InterlockedExchangeAdd は加算前の値が戻ることに注意 (InterlockedIncrement は加算後の値が戻る)
			j++;
			gpu_num = gpu_max;
			if (j + gpu_num > parity_num)
				gpu_num = parity_num - j;
			//printf("GPU : start %d, num = %d \n", j, gpu_num);

			// ブロックごとの倍率を計算する
			for (k = 0; k < gpu_num; k++){
				for (i = 0; i < src_num; i++){
					factor2[src_num * k + i] = galois_power(constant[src_start + i], first_num + j + k);
				}
			}

			i = gpu_multiply_blocks(source_num, src_num, gpu_num, factor2);
			if (i != 0){
				printf("error: gpu_multiply_blocks, %d, %d\n", i & 0xFF, i >> 8);
				th->count = -1;
				break;
			}

			i = gpu_xor_blocks(p_buf + (size_t)unit_size * j, unit_size, gpu_num);
			if (i != 0){
				printf("gpu_xor_blocks, %d, %d\n", i & 0xFF, i >> 8);
				th->count = -1;
				break;
			}

			*last_id = j;	// 計算が終わったブロックの番号
#ifdef TIMER
loop_count2 += src_num * gpu_num;
#endif
		}
#ifdef TIMER
time_encode2 += GetTickCount() - time_start2;
#endif
		SetEvent(hEnd);	// 計算終了を通知する
		WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	}
#ifdef TIMER
printf("gpu-thread    : total loop = %d\n", loop_count2);
printf(" 2nd encode %d.%03d sec, %d loop\n", time_encode2 / 1000, time_encode2 % 1000, loop_count2);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int encode_method4(	// 全てのブロックを断片的に保持する場合 (GPU対応)
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant)	// 複数ブロック分の領域を確保しておく？
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *factor1;
	int err = 0, i, j, last_file, cpu_num1, cpu_off1;
	int write_id, *last_id, last_id_min;
	int cover_max, cover_ave, cover_from, cover_num, gpu_max;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU + 1], hRun[MAX_CPU + 1], hEnd[MAX_CPU + 1];
	RS_TH th[1];
	PHMD5 md_ctx, *md_ptr = NULL;

	memset(hSub, 0, sizeof(HANDLE) * (MAX_CPU + 1));
	factor1 = constant + source_num;
	last_id = (int *)(constant + ((source_num + 1) & ~1));	// MAX_CPU + 1 以上の要素数はあるはず

	// 作業バッファーを確保する
	i = GPU_MAX_NUM + 1;	// GPU の作業領域として GPU_MAX_NUM + 1 個の余裕を見ておく
	if (parity_num < GPU_MAX_NUM)
		i = parity_num + 1;
	io_size = get_io_size_gpu(source_num + parity_num + i, 1);
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (source_num + parity_num) * (size_t)unit_size + HASH_SIZE
			+ (source_num * sizeof(unsigned short) * (cpu_num + GPU_MAX_NUM));
	buf = _aligned_malloc((size_t)file_off, 4096);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * source_num;	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * parity_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_base *= source_num * parity_num;	// 全体の断片の個数
	cpu_num1 = (cpu_num + 1) / 2;	// 読み込み中はスレッド数を減らす 1~2=1, 3~4=2, 5~6=3, 7=4
	cpu_off1 = 0;
#ifdef TIMER
	printf("\n read all source blocks, and keep all parity blocks (GPU)\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
#endif

	if (io_size < block_size){	// スライスが分割される場合だけ、途中までのハッシュ値を保持する
		len = sizeof(PHMD5) * parity_num;
		md_ptr = malloc(len);
		if (md_ptr == NULL){
			printf("malloc, %d\n", len);
			err = 1;
			goto error_end;
		}
		for (i = 0; i < parity_num; i++){
			Phmd5Begin(&(md_ptr[i]));
			j = first_num + i;	// 最初の番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			Phmd5Process(&(md_ptr[i]), header_buf + 32, 36);
		}
	}

	// OpenCL の初期化
	cover_max = source_num;
	gpu_max = parity_num;
	i = init_OpenCL(unit_size, &cover_max, &gpu_max);
	if (i != 0){
		if (i != 3)	// GPU が見つからなかった場合はエラー表示しない
			printf("init_OpenCL, %d, %d\n", i & 0xFF, i >> 8);
		i = free_OpenCL();
		if (i != 0)
			printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
		gpu_max = 0;
		// GPU を使わずに計算を続行する場合は以下をコメントアウト
		err = -2;	// CPU だけの方式に切り替える
		goto error_end;
	}

	// キャッシュの最適化を試みる際には、CPU 個数の判定で GPU の分を減らす
	len = try_cache_blocking(unit_size);
	len = (len + 63) & ~63;	// 64の倍数にする (CPU のキャッシュ・ライン？)
#ifdef TIMER
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %I64d, cover_max = %d, gpu_max = %d\n", prog_base, cover_max, gpu_max);
#endif
	if (gpu_max > 0){
		cpu_num++;	// GPU 管理用のスレッドを追加する
		cpu_off1++;	// GPU 用のスレッドは 1st encode に使わない
	}

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->off = len;	// chunk size
	for (j = 0; j < cpu_num; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		if ((j == 0) && (gpu_max > 0)){	// 0番のスレッドを GPU 管理用にする
			th->now = gpu_max;	// GPU が同時に計算するブロック数
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_encode_gpu, (LPVOID)th, 0, NULL);
		} else {
			th->now = j;	// スレッド番号
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_encode_each, (LPVOID)th, 0, NULL);
		}
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		if (j >= (cpu_num * 3) / 4)	// IO が延滞しないように、サブ・スレッドの優先度を下げる
			SetThreadPriority(hSub[j], THREAD_PRIORITY_BELOW_NORMAL);
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// ソース・ブロック断片を読み込んで、パリティ・ブロック断片を作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->off = -1;	// まだ計算して無い印
		write_id = 0;

		// ソース・ブロックを読み込む
#ifdef TIMER
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < source_num; i++){
			if (s_blk[i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
				file_off = block_off;
			} else {	// 同じファイルならブロック・サイズ分ずらす
				file_off += block_size;
			}
			if (s_blk[i].size > block_off){	// バッファーにソース・ファイルの内容を読み込む
				len = s_blk[i].size - block_off;
				if (len > io_size)
					len = io_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", i);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				// ソース・ブロックのチェックサムを計算する
				if (block_off == 0)
					s_blk[i].crc = 0xFFFFFFFF;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf + (size_t)unit_size * i, len);	// without pad
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif

				if (i + 1 < source_num){	// 最後のブロック以外なら
					// サブ・スレッドの動作状況を調べる
					j = WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, 0);
					if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
						// 経過表示
						prog_num += parity_num;
						if (GetTickCount() - time_last >= UPDATE_TIME){
							if (print_progress((int)((prog_num * 1000) / prog_base))){
								err = 2;
								goto error_end;
							}
							time_last = GetTickCount();
						}
						// 計算終了したブロックの次から計算を開始する
						th->off += 1;
						if (th->off > 0){	// バッファーに読み込んだ時だけ計算する
							while (s_blk[th->off].size <= block_off){
								prog_num += parity_num;
								th->off += 1;
#ifdef TIMER
skip_count++;
#endif
							}
						}
						th->buf = buf + (size_t)unit_size * th->off;
						for (j = 0; j < parity_num; j++)
							factor1[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
						th->size = 1;	// 1st encode
						th->now = -1;	// 初期値 - 1
						for (j = cpu_off1; j < cpu_off1 + cpu_num1; j++){
							ResetEvent(hEnd[j]);	// リセットしておく
							SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
						}
					}
				}
			} else {
				memset(buf + (size_t)unit_size * i, 0, unit_size);
			}
		}
		// 最後のソース・ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->size = 2;	// 2nd encode
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off > 0){
			while (s_blk[th->off].size <= block_off){	// 計算不要なソース・ブロックはとばす
				prog_num += parity_num;
				th->off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		} else {	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * parity_num);
		}

#ifdef TIMER
		j = (th->off * 1000) / source_num;
		printf("partial encode = %d (%d.%d%%), read = %d, skip = %d\n", th->off, j / 10, j % 10, read_count, skip_count);
#endif

		// リカバリ・ファイルに書き込むサイズ
		if (block_size - block_off < io_size){
			len = block_size - block_off;
		} else {
			len = io_size;
		}

		// VRAM のサイズに応じて分割する
		cover_from = th->off;
		i = (source_num - cover_from + cover_max - 1) / cover_max;	// 何回に分けて処理するか
		cover_ave = (source_num - cover_from + i - 1) / i;	// 一度に処理する量を平均化する
		//printf("cover range = %d, cover_ave = %d\n", source_num - cover_from, cover_ave);
		while (cover_from < source_num){
			// ソース・ブロックを何個ずつ処理するか
			cover_num = cover_ave;
			if (cover_from + cover_num > source_num)
				cover_num = source_num - cover_from;
			//printf("cover_from = %d, cover_num = %d\n", cover_from, cover_num);

			if (gpu_max > 0){	// VRAM にソース・ブロックをコピーする
				i = gpu_copy_blocks(buf, unit_size, cover_from, cover_num);
				if (i != 0){
					printf("gpu_copy_blocks, %d, %d\n", i & 0xFF, i >> 8);
					err = 1;
					goto error_end;
				}
			}

			// GPU は一度に複数のパリティ・ブロックを計算する
			// CPU はスレッドごとに一個のパリティ・ブロックを計算する
			th->buf = buf + (size_t)unit_size * cover_from;
			th->off = cover_from;
			th->count = cover_num;
			th->now = -1;	// 初期値 - 1
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME/4 だけ待つ
			while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME / 4) == WAIT_TIMEOUT){
				last_id_min = parity_num;	// 何ブロック目まで計算が終わってるか
				for (j = 0; j < cpu_num; j++)	// メイン・スレッドの領域にコピーしてから比較する
					last_id[cpu_num + j] = last_id[j];
				for (j = 0; j < cpu_num; j++){
					if (last_id_min > last_id[cpu_num + j])
						last_id_min = last_id[cpu_num + j];
				}
				//printf("%d, %d, min = %d\n", last_id[0], last_id[1], last_id_min);

				if (cover_from + cover_num >= source_num){	// 最後のソース・ブロック集合なら
					//printf("\n write block from %d to %d\n", write_id, last_id_min - 1);
#ifdef TIMER
time_start = GetTickCount();
#endif
					// 次のブロックを計算中に、計算終了したブロックが残っていれば書き込む
					while ((write_id < last_id_min) && (WaitForMultipleObjects(cpu_num, hEnd, FALSE, 0) == WAIT_TIMEOUT)){
						work_buf = p_buf + (size_t)unit_size * write_id;
						// パリティ・ブロックのチェックサムを検証する
						checksum16_return(work_buf, hash, io_size);
						if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
							printf("checksum mismatch, recovery slice %d\n", write_id);
							err = 1;
							goto error_end;
						}
						// ハッシュ値を計算して、リカバリ・ファイルに書き込む
						if (io_size >= block_size){	// 1回で書き込みが終わるなら
							Phmd5Begin(&md_ctx);
							j = first_num + write_id;	// 最初の番号の分だけ足す
							memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
							Phmd5Process(&md_ctx, header_buf + 32, 36);
							Phmd5Process(&md_ctx, work_buf, len);
							Phmd5End(&md_ctx);
							memcpy(header_buf + 16, md_ctx.hash, 16);
							// ヘッダーを書き込む
							if (file_write_data(rcv_hFile[p_blk[write_id].file], p_blk[write_id].off + block_off - 68, header_buf, 68)){
								printf("file_write_data, recovery slice %d\n", write_id);
								err = 1;
								goto error_end;
							}
						} else {
							Phmd5Process(&(md_ptr[write_id]), work_buf, len);
						}
						//printf("%d, buf = %p, size = %u, off = %I64d\n", write_id, work_buf, len, p_blk[write_id].off + block_off);
						if (file_write_data(rcv_hFile[p_blk[write_id].file], p_blk[write_id].off + block_off, work_buf, len)){
							printf("file_write_data, recovery slice %d\n", write_id);
							err = 1;
							goto error_end;
						}
						write_id++;
						Sleep(0);	// サブ・スレッドの計算を優先する
					}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
				}

				// 経過表示
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((int)(((prog_num + cover_num * last_id_min) * 1000) / prog_base))){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
			}
			if (th->count < 0){	// エラー発生
				err = 1;
				goto error_end;
			}

			// 経過表示
			prog_num += cover_num * parity_num;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)((prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			cover_from += cover_num;
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 残りのパリティ・ブロックごとに
		work_buf = p_buf + (size_t)unit_size * write_id;
		//printf("write block from %d to %d\n", write_id, parity_num - 1);
		for (i = write_id; i < parity_num; i++){
			// パリティ・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			// ハッシュ値を計算して、リカバリ・ファイルに書き込む
			if (io_size >= block_size){	// 1回で書き込みが終わるなら
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初の番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, len);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
			} else {
				Phmd5Process(&(md_ptr[i]), work_buf, len);
			}
			//printf("%d, buf = %p, size = %u, off = %I64d\n", i, work_buf, len, p_blk[i].off + block_off);
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off + block_off, work_buf, len)){
				printf("file_write_data, recovery slice %d\n", i);
				err = 1;
				goto error_end;
			}
			work_buf += unit_size;
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

		block_off += io_size;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	memset(buf, 0, io_size);
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		cover_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		i = j + cover_num - 1;	// 末尾ブロックの番号
		if (s_blk[i].size < block_size){	// 残りを 0 でパディングする
			len = block_size - s_blk[i].size;
			while (len > io_size){
				len -= io_size;
				s_blk[i].crc = crc_update(s_blk[i].crc, buf, io_size);
			}
			s_blk[i].crc = crc_update(s_blk[i].crc, buf, len);
		}
		memset(hash, 0, 16);
		for (i = 0; i < cover_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc ^ 0xFFFFFFFF;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += cover_num;
	}

	if (io_size < block_size){	// 1回で書き込みが終わらなかったなら
		if (GetTickCount() - time_last >= UPDATE_TIME){	// キャンセルを受け付ける
			if (cancel_progress()){
				err = 2;
				goto error_end;
			}
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 最後に Recovery Slice packet のヘッダーを書き込む
		for (i = 0; i < parity_num; i++){
			Phmd5End(&(md_ptr[i]));
			memcpy(header_buf + 16, md_ptr[i].hash, 16);
			j = first_num + i;	// 最初のパリティ・ブロック番号の分だけ足す
			memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
			// リカバリ・ファイルに書き込む
			if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){	// ヘッダーのサイズ分だけずらす
				printf("file_write_data, packet header\n");
				err = 1;
				goto error_end;
			}
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
#else
	info_OpenCL(buf, MEM_UNIT * 2);	// デバイス情報を表示する
#endif

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (md_ptr)
		free(md_ptr);
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	if (gpu_max > 0)
		cpu_num--;	// GPU 用のスレッド分を減らす
	i = free_OpenCL();
	if (i != 0)
		printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
	return err;
}

int encode_method5(	// パリティ・ブロックだけ保持する場合 (GPU対応)
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *factor1;
	int err = 0, i, j, last_file, cpu_num1, cpu_off1, source_off, read_num;
	int write_id, *last_id, last_id_min;
	int cover_max, cover_ave, cover_from, cover_num, gpu_max;
	unsigned int unit_size, len;
	unsigned int time_last, prog_num = 0, prog_base;
	size_t mem_size;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU + 1], hRun[MAX_CPU + 1], hEnd[MAX_CPU + 1];
	RS_TH th[1];
	PHMD5 md_ctx;

	memset(hSub, 0, sizeof(HANDLE) * MAX_CPU);
	factor1 = constant + source_num;
	last_id = (int *)(constant + ((source_num + 1) & ~1));	// MAX_CPU + 1 以上の要素数はあるはず
	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);	// MEM_UNIT の倍数にする

	// 作業バッファーを確保する
	i = GPU_MAX_NUM + 1;	// GPU の作業領域として GPU_MAX_NUM + 1 個の余裕を見ておく
	if (parity_num < GPU_MAX_NUM)
		i = parity_num + 1;
	read_num = read_block_num_gpu(parity_num, i, 1);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
		//printf("\n cannot keep enough blocks, use another method\n");
		return -4;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 2) / 3 + 1;	// 実験用
	mem_size = (size_t)(read_num + parity_num) * unit_size + HASH_SIZE
			+ (source_num * sizeof(unsigned short) * (cpu_num + GPU_MAX_NUM));
	buf = _aligned_malloc(mem_size, 4096);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %Id\n", mem_size);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * parity_num;
	prog_base = source_num * parity_num;	// ブロックの合計掛け算個数
	cpu_num1 = (cpu_num + 1) / 2;	// 読み込み中はスレッド数を減らす 1~2=1, 3~4=2, 5~6=3, 7=4
	cpu_off1 = 0;
#ifdef TIMER
	printf("\n read some source blocks, and keep all parity blocks (GPU)\n");
	printf("buffer size = %Id MB, read_num = %d, round = %d\n", mem_size >> 20, read_num, (source_num + read_num - 1) / read_num);
#endif

	// OpenCL の初期化
	cover_max = read_num;	// 読み込める分だけにする
	gpu_max = parity_num;
	i = init_OpenCL(unit_size, &cover_max, &gpu_max);
	if (i != 0){
		if (i != 3)	// GPU が見つからなかった場合はエラー表示しない
			printf("init_OpenCL, %d, %d\n", i & 0xFF, i >> 8);
		i = free_OpenCL();
		if (i != 0)
			printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
		gpu_max = 0;
		// GPU を使わずに計算を続行する場合は以下をコメントアウト
		err = -3;	// CPU だけの方式に切り替える
		goto error_end;
	}

	// キャッシュの最適化を試みる際には、CPU 個数の判定で GPU の分を減らす
	len = try_cache_blocking(unit_size);
	len = (len + 63) & ~63;	// 64の倍数にする (CPU のキャッシュ・ライン？)
#ifdef TIMER
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %d, cover_max = %d, gpu_max = %d\n", prog_base, cover_max, gpu_max);
#endif
	if (gpu_max > 0){
		cpu_num++;	// GPU 管理用のスレッドを追加する
		cpu_off1++;	// GPU 用のスレッドは 1st encode に使わない
	}

	// マルチ・スレッドの準備をする
	th->mat = constant;
	th->buf = p_buf;
	th->size = unit_size;
	th->off = len;	// chunk size
	for (j = 0; j < cpu_num; j++){	// サブ・スレッドごとに
		hRun[j] = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (hRun[j] == NULL){
			print_win32_err();
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		hEnd[j] = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (hEnd[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		// サブ・スレッドを起動する
		th->run = hRun[j];
		th->end = hEnd[j];
		if ((j == 0) && (gpu_max > 0)){	// 0番のスレッドを GPU 管理用にする
			th->now = gpu_max;	// GPU が同時に計算するブロック数
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_encode_gpu, (LPVOID)th, 0, NULL);
		} else {
			th->now = j;	// スレッド番号
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_encode_each, (LPVOID)th, 0, NULL);
		}
		if (hSub[j] == NULL){
			print_win32_err();
			CloseHandle(hRun[j]);
			CloseHandle(hEnd[j]);
			printf("error, sub-thread\n");
			err = 1;
			goto error_end;
		}
		if (j >= (cpu_num * 3) / 4)	// IO が延滞しないように、サブ・スレッドの優先度を下げる
			SetThreadPriority(hSub[j], THREAD_PRIORITY_BELOW_NORMAL);
		WaitForSingleObject(hEnd[j], INFINITE);	// 設定終了の合図を待つ (リセットしない)
	}

	// 何回かに別けてソース・ブロックを読み込んで、パリティ・ブロックを少しずつ作成する
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	last_file = -1;
	write_id = 0;
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 1;	// 1st encode
		th->off = source_off - 1;	// まだ計算して無い印

#ifdef TIMER
time_start = GetTickCount();
#endif
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			// ソース・ブロックを読み込む
			if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[source_off + i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				wcscpy(file_path + base_len, list_buf + files[last_file].name);
				hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", list_buf + files[last_file].name);
					err = 1;
					goto error_end;
				}
			}
			// バッファーにソース・ファイルの内容を読み込む
			len = s_blk[source_off + i].size;
			if (!ReadFile(hFile, buf + (size_t)unit_size * i, len, &j, NULL) || (len != j)){
				print_win32_err();
				err = 1;
				goto error_end;
			}
			if (len < block_size)
				memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
			// ソース・ブロックのチェックサムを計算する
			s_blk[source_off + i].crc = crc_update(0xFFFFFFFF, buf + (size_t)unit_size * i, block_size) ^ 0xFFFFFFFF;	// include pad
			checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);

			if (i + 1 < read_num){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += parity_num;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress(((__int64)prog_num * 1000) / prog_base)){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 計算終了したブロックの次から計算を開始する
					th->off += 1;
					th->buf = buf + (size_t)unit_size * (th->off - source_off);
					for (j = 0; j < parity_num; j++)
						factor1[j] = galois_power(constant[th->off], first_num + j);	// factor は定数行列の乗数になる
					th->now = -1;	// 初期値 - 1
					for (j = cpu_off1; j < cpu_off1 + cpu_num1; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}
		}
		if (source_off + i == source_num - 1){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->size = 2;	// 2nd encode
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off == 0)	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * parity_num);
#ifdef TIMER
		j = (th->off - source_off) * 1000 / read_num;
		printf("partial encode = %d (%d.%d%%)\n", th->off - source_off, j / 10, j % 10);
#endif

		// VRAM のサイズに応じて分割する
		cover_from = th->off - source_off;
		i = (read_num - cover_from + cover_max - 1) / cover_max;	// 何回に分けて処理するか
		cover_ave = (read_num - cover_from + i - 1) / i;	// 一度に処理する量を平均化する
		//printf("cover range = %d, cover_ave = %d\n", read_num - cover_from, cover_ave);
		while (cover_from < read_num){
			// ソース・ブロックを何個ずつ処理するか
			cover_num = cover_ave;
			if (cover_from + cover_num > read_num)
				cover_num = read_num - cover_from;
			//printf("cover_from = %d, cover_num = %d\n", cover_from, cover_num);

			if (gpu_max > 0){	// VRAM にソース・ブロックをコピーする
				i = gpu_copy_blocks(buf, unit_size, cover_from, cover_num);
				if (i != 0){
					printf("gpu_copy_blocks, %d, %d\n", i & 0xFF, i >> 8);
					err = 1;
					goto error_end;
				}
			}

			// GPU は一度に複数のパリティ・ブロックを計算する
			// CPU はスレッドごとに一個のパリティ・ブロックを計算する
			th->buf = buf + (size_t)unit_size * cover_from;
			th->off = source_off + cover_from;	// ソース・ブロックの番号にする
			th->count = cover_num;
			th->now = -1;	// 初期値 - 1
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME/4 だけ待つ
			while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME / 4) == WAIT_TIMEOUT){
				last_id_min = parity_num;	// 何ブロック目まで計算が終わってるか
				for (j = 0; j < cpu_num; j++)	// メイン・スレッドの領域にコピーしてから比較する
					last_id[cpu_num + j] = last_id[j];
				for (j = 0; j < cpu_num; j++){
					if (last_id_min > last_id[cpu_num + j])
						last_id_min = last_id[cpu_num + j];
				}
				//printf("%d, %d, min = %d\n", last_id[0], last_id[1], last_id_min);

				if (source_off + cover_from + cover_num >= source_num){	// 最後のソース・ブロック集合なら
					//printf("\n write block from %d to %d\n", write_id, last_id_min - 1);
#ifdef TIMER
time_start = GetTickCount();
#endif
					// 次のブロックを計算中に、計算終了したブロックが残っていれば書き込む
					while ((write_id < last_id_min) && (WaitForMultipleObjects(cpu_num, hEnd, FALSE, 0) == WAIT_TIMEOUT)){
						work_buf = p_buf + (size_t)unit_size * write_id;
						// パリティ・ブロックのチェックサムを検証する
						checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
						if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
							printf("checksum mismatch, recovery slice %d\n", write_id);
							err = 1;
							goto error_end;
						}
						// ハッシュ値を計算して、リカバリ・ファイルに書き込む
						Phmd5Begin(&md_ctx);
						j = first_num + write_id;	// 最初の番号の分だけ足す
						memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
						Phmd5Process(&md_ctx, header_buf + 32, 36);
						Phmd5Process(&md_ctx, work_buf, block_size);
						Phmd5End(&md_ctx);
						memcpy(header_buf + 16, md_ctx.hash, 16);
						// ヘッダーを書き込む
						if (file_write_data(rcv_hFile[p_blk[write_id].file], p_blk[write_id].off - 68, header_buf, 68)){
							printf("file_write_data, recovery slice %d\n", write_id);
							err = 1;
							goto error_end;
						}
						//printf("%d, buf = %p, off = %I64d\n", write_id, work_buf, p_blk[write_id].off);
						if (file_write_data(rcv_hFile[p_blk[write_id].file], p_blk[write_id].off, work_buf, block_size)){
							printf("file_write_data, recovery slice %d\n", write_id);
							err = 1;
							goto error_end;
						}
						write_id++;
						Sleep(0);	// サブ・スレッドの計算を優先する
					}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
				}

				// 経過表示
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((int)((((__int64)prog_num + cover_num * last_id_min) * 1000) / prog_base))){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
			}
			if (th->count < 0){	// エラー発生
				err = 1;
				goto error_end;
			}

			// 経過表示
			prog_num += cover_num * parity_num;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((int)(((__int64)prog_num * 1000) / prog_base))){
					err = 2;
					goto error_end;
				}
				time_last = GetTickCount();
			}

			cover_from += cover_num;
		}

		if (source_off + read_num >= source_num){	// 最後の書き込みなら
#ifdef TIMER
time_start = GetTickCount();
#endif
			// 残りのパリティ・ブロックごとに
			work_buf = p_buf + (size_t)unit_size * write_id;
			//printf("write block from %d to %d\n", write_id, parity_num - 1);
			for (i = write_id; i < parity_num; i++){
				// パリティ・ブロックのチェックサムを検証する
				checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
				if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				// ハッシュ値を計算して、リカバリ・ファイルに書き込む
				Phmd5Begin(&md_ctx);
				j = first_num + i;	// 最初の番号の分だけ足す
				memcpy(header_buf + 64, &j, 4);	// Recovery Slice の番号を書き込む
				Phmd5Process(&md_ctx, header_buf + 32, 36);
				Phmd5Process(&md_ctx, work_buf, block_size);
				Phmd5End(&md_ctx);
				memcpy(header_buf + 16, md_ctx.hash, 16);
				// ヘッダーを書き込む
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off - 68, header_buf, 68)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				//printf("%d, buf = %p, off = %I64d\n", i, work_buf, p_blk[i].off);
				if (file_write_data(rcv_hFile[p_blk[i].file], p_blk[i].off, work_buf, block_size)){
					printf("file_write_data, recovery slice %d\n", i);
					err = 1;
					goto error_end;
				}
				work_buf += unit_size;
			}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
		}

		source_off += read_num;
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// ファイルごとにブロックの CRC-32 を検証する
	j = 0;
	while (j < source_num){
		last_file = s_blk[j].file;
		read_num = (int)((files[last_file].size + (__int64)block_size - 1) / block_size);
		memset(hash, 0, 16);
		for (i = 0; i < read_num; i++)	// XOR して 16バイトに減らす
			((unsigned int *)hash)[i & 3] ^= s_blk[j + i].crc;
		if (memcmp(files[last_file].hash, hash, 16) != 0){
			printf("checksum mismatch, input file %d\n", last_file);
			err = 1;
			goto error_end;
		}
		j += read_num;
	}

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
#else
	info_OpenCL(buf, MEM_UNIT * 2);	// デバイス情報を表示する
#endif

error_end:
	InterlockedExchange(&(th->now), INT_MAX / 2);	// サブ・スレッドの計算を中断する
	for (j = 0; j < cpu_num; j++){
		if (hSub[j]){	// サブ・スレッドを終了させる
			SetEvent(hRun[j]);
			WaitForSingleObject(hSub[j], INFINITE);
			CloseHandle(hSub[j]);
		}
	}
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	if (gpu_max > 0)
		cpu_num--;	// GPU 用のスレッド分を減らす
	i = free_OpenCL();
	if (i != 0)
		printf("free_OpenCL, %d, %d", i & 0xFF, i >> 8);
	return err;
}

