// rs_decode.c
// Copyright : 2016-04-28 Yutaka Sawada
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
#include "rs_decode.h"


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

static DWORD WINAPI thread_decode2(LPVOID lpParameter)
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
	buf = th->buf;
	unit_size = th->size;
	part_num = th->count;
	chunk_size = th->off;
	th_id = th->now;	// スレッド番号
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	p_buf = buf + (size_t)unit_size * (source_num + cpu_num - 1);
	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * part_num;
	if (th_id > 0)
		work_buf = buf + (size_t)unit_size * (source_num + th_id - 1);

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		i = th->off;	// ソース・ブロック番号
		factor = th->mat;
		len = chunk_size;
		if (th->buf == NULL){	// ソース・ブロック読み込み中
			// 復元するブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / part_num;	// chunk の番号
				j = j % part_num;	// lost block の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (i == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(buf + ((size_t)unit_size * i + off), p_buf + ((size_t)unit_size * j + off), len, factor[source_num * j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// 復元したブロック書き込み中
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
printf(" 1st decode %d.%03d sec, %d loop\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a);
printf(" 2nd decode %d.%03d sec, %d loop\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

static DWORD WINAPI thread_decode3(LPVOID lpParameter)
{
	unsigned char *buf, *p_buf, *work_buf;
	unsigned short *factor;
	int i, j, th_id, block_lost, chunk_num, max_num, left_num, left_max;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	buf = th->buf;
	block_lost = th->size;
	left_num = th->count;
	chunk_size = th->off;
	th_id = th->now;	// スレッド番号
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);
	p_buf = buf + (size_t)unit_size * (left_num + cpu_num - 1);
	chunk_num = (unit_size + chunk_size - 1) / chunk_size;
	max_num = chunk_num * block_lost;
	if (th_id > 0)
		work_buf = buf + (size_t)unit_size * (left_num + th_id - 1);

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		i = th->off;	// ソース・ブロック番号
		factor = th->mat;
		len = chunk_size;
		if (th->buf == NULL){	// ソース・ブロック読み込み中
			left_num = th->count + i;
			// 復元するブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / block_lost;	// chunk の番号
				j = j % block_lost;		// lost block の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (left_num == 0)
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(buf + ((size_t)unit_size * i + off), p_buf + ((size_t)unit_size * j + off), len, factor[source_num * j]);
#ifdef TIMER
loop_count2a++;
#endif
			}
#ifdef TIMER
time_encode2a += GetTickCount() - time_start2;
#endif
		} else {	// 復元したブロック書き込み中
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

int decode_method1(	// ソース・ブロックが一個だけの場合
	wchar_t *file_path,
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk)	// パリティ・ブロックの情報
{
	unsigned char *buf = NULL, *work_buf, *hash;
	int err = 0, id;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last, prog_num = 0, prog_base;
	__int64 file_off;
	HANDLE hFile = NULL;

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
	prog_base = (block_size + io_size - 1) / io_size;	// 断片の個数
#ifdef TIMER
	printf("\n read one block, and keep one recovering block\n");
	printf("buffer size = %d MB, io_size = %d, split = %d\n", len >> 20, io_size, (block_size + io_size - 1) / io_size);
	id = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, id, (unit_size + id - 1) / id);
#endif

	// 作業用のソース・ファイルを開く
	wcscpy(file_path, base_dir);
	get_temp_name(list_buf + files[s_blk[0].file].name, file_path + base_len);
	hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		hFile = NULL;
		printf_cp("cannot open file, %s\n", file_path);
		err = 1;
		goto error_end;
	}

	// 何番のパリティ・ブロックを使うか
	for (id = 0; id < parity_num; id++){
		if (p_blk[id].exist == 1)
			break;
	}
	//printf("parity_num = %d, id = %d\n", parity_num, id);

	// バッファー・サイズごとにソース・ブロックを復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	block_off = 0;
	while (block_off < block_size){
#ifdef TIMER
time_start = GetTickCount();
#endif
		// パリティ・ブロックを読み込む
		len = block_size - block_off;
		if (len > io_size)
			len = io_size;
		file_off = p_blk[id].off + (__int64)block_off;
		if (file_read_data(rcv_hFile[p_blk[id].file], file_off, buf, len)){
			printf("file_read_data, recovery slice %d\n", id);
			err = 1;
			goto error_end;
		}
		if (len < io_size)
			memset(buf + len, 0, io_size - len);
		// パリティ・ブロックのチェックサムを計算する
		checksum16_altmap(buf, buf + io_size, io_size);
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

#ifdef TIMER
time_start = GetTickCount();
#endif
		// 失われたソース・ブロックを復元する
		memset(work_buf, 0, unit_size);
		// factor で割ると元に戻る
		galois_align_multiply(buf, work_buf, unit_size, galois_divide(1, galois_power(2, id)));
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
		// 復元されたソース・ブロックのチェックサムを検証する
		checksum16_return(work_buf, hash, io_size);
		if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
			printf("checksum mismatch, recovered input slice %d\n", 0);
			err = 1;
			goto error_end;
		}
		// 作業用のソース・ファイルに書き込む
		len = s_blk[0].size - block_off;
		if (len > io_size)
			len = io_size;
		if (file_write_data(hFile, (__int64)block_off, work_buf, len)){
			printf("file_write_data, input slice %d\n", 0);
			err = 1;
			goto error_end;
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

		block_off += io_size;
	}
	print_progress_done();	// 末尾ブロックの断片化によっては 100% で完了するとは限らない

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
printf("write  %d.%03d sec\n", time_write / 1000, time_write % 1000);
printf("decode %d.%03d sec\n", time_calc / 1000, time_calc % 1000);
#endif

error_end:
	if (hFile)
		CloseHandle(hFile);
	if (buf)
		_aligned_free(buf);
	return err;
}

int decode_method2(	// ソース・データを全て読み込む場合
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,		// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *sub_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, k, last_file, part_num, recv_now;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * (MAX_CPU));
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか

	// 作業バッファーを確保する
	part_num = source_num >> PART_MAX_RATE;	// ソース・ブロック数に対する割合で最大量を決める
	if (part_num > block_lost)
		part_num = block_lost;
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
	p_buf = sub_buf + (size_t)unit_size * (cpu_num - 1);	// 復元したブロックを部分的に記録する領域
	hash = p_buf + (size_t)unit_size * part_num;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_base *= source_num * block_lost;	// 全体の断片の個数
#ifdef TIMER
	printf("\n read all blocks, and keep some recovering blocks\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
	len = try_cache_blocking(unit_size);
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %I64d, part_num = %d\n", prog_base, part_num);
#endif

	// マルチ・スレッドの準備をする
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
		hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_decode2, (LPVOID)th, 0, NULL);
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

	// バッファー・サイズごとにソース・ブロックを復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->buf = NULL;
		th->off = -1;	// まだ計算して無い印

#ifdef TIMER
time_start = GetTickCount();
#endif
		last_file = -1;
		recv_now = 0;	// 何番目の代替ブロックか
		for (i = 0; i < source_num; i++){
			switch(s_blk[i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				len = block_size - block_off;
				if (len > io_size)
					len = io_size;
				file_off = p_blk[id[recv_now]].off + (__int64)block_off;
				if (file_read_data(rcv_hFile[p_blk[id[recv_now]].file], file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, recovery slice %d\n", id[recv_now]);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				recv_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				len = 0;
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if ((files[last_file].state & 7) != 0){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if ((files[last_file].state & 32) != 0){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				if (s_blk[i].size > block_off){
					len = s_blk[i].size - block_off;
					if (len > io_size)
						len = io_size;
					file_off = (i - files[last_file].b_off) * (__int64)block_size + (__int64)block_off;
					if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
						printf("file_read_data, input slice %d\n", i);
						err = 1;
						goto error_end;
					}
					if (len < io_size)
						memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
					// ソース・ブロックのチェックサムを計算する
					checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				} else {
					len = 0;
					memset(buf + (size_t)unit_size * i, 0, unit_size);
				}
			}

			if ((len > 0) && (i + 1 < source_num)){	// 最後のブロック以外なら
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
						while ((s_blk[th->off].exist != 0) &&
								((s_blk[th->off].size <= block_off) || (s_blk[th->off].exist == 3))){
							prog_num += part_num;
							th->off += 1;
#ifdef TIMER
skip_count++;
#endif
						}
					}
					th->mat = mat + th->off;
					th->now = -1;	// 初期値 - 1
					for (j = 0; j < (cpu_num + 1) / 2; j++){	// 読み込み中はスレッド数を減らす
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}
		}
		if (hFile){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off > 0){	// 計算不要なソース・ブロックはとばす
			while ((s_blk[th->off].exist != 0) &&
					((s_blk[th->off].size <= block_off) || (s_blk[th->off].exist == 3))){
				prog_num += part_num;
				th->off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		}
#ifdef TIMER
		j = (int)(((__int64)(th->off * part_num) * 1000) / (source_num * block_lost));
		printf("partial decode = %d (%d.%d%%), read = %d, skip = %d\n", th->off, j / 10, j % 10, read_count, skip_count);
#endif

		// 最初の消失ブロックを探す
		recv_now = 0;	// 何番目の消失ブロックか
		for (i = 0; i < source_num; i++){
			if (s_blk[i].exist == 0){
				if (s_blk[i].size > block_off){
					if (recv_now >= part_num){	// 部分的なエンコードを施した箇所以降なら
						work_buf = p_buf;
					} else {
						work_buf = p_buf + (size_t)unit_size * recv_now;
					}
					//printf("%d: s_blk[%d].size = %d, off = %d\n", recv_now, i, s_blk[i].size, th->off);
					// 経過表示
					prog_num += source_num - th->off;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress((int)((prog_num * 1000) / prog_base))){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 最初のブロックの復元を開始しておく
					th->mat = mat + source_num * recv_now;
					th->buf = work_buf;
					th->now = -1;	// 初期値 - 1
					for (j = 0; j < cpu_num; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
					break;
				} else {
					//printf("%d: s_blk[%d] = empty, off = %d\n", recv_now, i, th->off);
					prog_num += source_num - th->off;
					recv_now++;	// 復元する必要が無くても番号は進める
					if (recv_now == part_num)	// 部分的なエンコードを施した箇所以降なら
						th->off = 0;	// 最初のソース・ブロックから計算する
				}
			}
		}

		// 失われたソース・ブロックごとに復元する
		last_file = -1;
		while (recv_now < block_lost){
			recv_now++;	// 次の消失ブロックの番号にする
			// ブロックの復元が終わるのを待って完成させる
			WaitForMultipleObjects(cpu_num, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
			for (j = 1; j < cpu_num; j++){
				// 最後にサブ・スレッドのバッファーを XOR する
				galois_align_multiply(sub_buf + (size_t)unit_size * (j - 1), work_buf, unit_size, 1);
			}

			if (recv_now < block_lost){	// 最後のブロック以外なら
				// 次の消失ブロックを探す
				for (k = i + 1; k < source_num; k++){
					if (s_blk[k].exist == 0){
						if (s_blk[k].size > block_off)
							break;
						if (recv_now >= part_num)	// 部分的なエンコードを施した箇所以降なら
							th->off = 0;	// 最初のソース・ブロックから計算する
						//printf("%d: s_blk[%d] = empty, off = %d\n", recv_now, k, th->off);
						prog_num += source_num - th->off;
						recv_now++;	// 復元する必要が無くても番号は進める
					}
				}
				if (k < source_num){	// 復元するブロックが存在するなら
					if (recv_now >= part_num){	// 部分的なエンコードを施した箇所以降なら
						if (th->buf != p_buf){	// エンコードと書き込みでダブル・バッファリングする
							th->buf = p_buf;
						} else {
							th->buf += unit_size;
						}
						th->off = 0;	// 最初のソース・ブロックから計算する
					} else {
						th->buf = p_buf + (size_t)unit_size * recv_now;
					}
					//printf("%d: s_blk[%d].size = %d, off = %d\n", recv_now, k, s_blk[k].size, th->off);
					// 経過表示
					prog_num += source_num - th->off;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress((int)((prog_num * 1000) / prog_base))){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 次のブロックの復元を開始しておく
					th->mat = mat + source_num * recv_now;
					th->now = -1;	// 初期値 - 1
					for (j = 0; j < cpu_num; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}

#ifdef TIMER
time_start = GetTickCount();
#endif
			// 復元されたソース・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovered input slice %d\n", i);
				err = 1;
				goto error_end;
			}
			// 作業用のソース・ファイルに書き込む
			if (s_blk[i].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[i].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				get_temp_name(list_buf + files[last_file].name, file_path + base_len);
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", file_path);
					err = 1;
					goto error_end;
				}
				//printf("file %d, open %S\n", last_file, file_path);
			}
			// ソース・ファイル内でのブロック断片の大きさと位置
			len = s_blk[i].size - block_off;
			if (len > io_size)
				len = io_size;
			if (file_write_data(hFile, (i - files[last_file].b_off) * (__int64)block_size + block_off, work_buf, len)){
				printf("file_write_data, input slice %d\n", i);
				err = 1;
				goto error_end;
			}
#ifdef TIMER
time_write += GetTickCount() - time_start;
write_count++;
#endif

			if (recv_now >= part_num) {	// 部分的なエンコードを施した箇所以降なら
				if (work_buf != p_buf){	// エンコードと書き込みでダブル・バッファリングする
					work_buf = p_buf;
				} else {
					work_buf += unit_size;
				}
			} else {
				work_buf = p_buf + (size_t)unit_size * recv_now;
			}
			i = k;	// 次の消失ブロックの番号
		}

		block_off += io_size;
		// 最後の作業ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
	}
	print_progress_done();	// 末尾ブロックの断片化によっては 100% で完了するとは限らない

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
j = ((block_size + io_size - 1) / io_size) * block_lost;
printf("write  %d.%03d sec, count = %d/%d\n", time_write / 1000, time_write % 1000, write_count, j);
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

int decode_method3(	// 復元するブロックを全て保持できる場合
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,	// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *sub_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, k, last_file, source_off, read_num, recv_now, parity_now;
	unsigned int unit_size, len;
	unsigned int time_last, prog_num = 0, prog_base;
	__int64 file_off;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU], hRun[MAX_CPU], hEnd[MAX_CPU];
	RS_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * (MAX_CPU));
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか
	unit_size = (block_size + HASH_SIZE + (sse_unit - 1)) & ~(sse_unit - 1);	// チェックサムの分だけ増やす

	// 作業バッファーを確保する
	read_num = read_block_num(block_lost, cpu_num - 1, 1);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
		//printf("\n cannot keep enough blocks, use another method\n");
		return -2;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 2) / 3 + 1;	// 実験用
	len = read_num + block_lost + (cpu_num - 1);	// 復元するブロックとサブ・スレッドの作業領域が必要
	file_off = len * (size_t)unit_size + HASH_SIZE;
	buf = _aligned_malloc((size_t)file_off, sse_unit);
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	sub_buf = buf + (size_t)unit_size * read_num;	// サブ・スレッドの作業領域
	p_buf = sub_buf + (size_t)unit_size * (cpu_num - 1);	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * block_lost;
	prog_base = source_num * block_lost;	// ブロックの合計掛け算個数
#ifdef TIMER
	printf("\n read some blocks, and keep all recovering blocks\n");
	printf("buffer size = %I64d MB, read_num = %d, round = %d\n", file_off >> 20, read_num, (source_num + read_num - 1) / read_num);
	len = try_cache_blocking(unit_size);	// キャッシュの最適化を試みる
	printf("cache: limit size = %d, chunk_size = %d, split = %d\n", cpu_flag & 0x7FFF8000, len, (unit_size + len - 1) / len);
	printf("prog_base = %d\n", prog_base);
#endif

	// マルチ・スレッドの準備をする
	th->buf = buf;
	th->size = block_lost;
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
		hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_decode3, (LPVOID)th, 0, NULL);
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

	// 何回かに別けてブロックを読み込んで、ソース・ブロックを少しずつ復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	parity_now = 0;	// 何番目の代替ブロックか
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
		last_file = -1;
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			switch(s_blk[source_off + i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				if (file_read_data(rcv_hFile[p_blk[id[parity_now]].file], p_blk[id[parity_now]].off, buf + (size_t)unit_size * i, block_size)){
					printf("file_read_data, recovery slice %d\n", id[parity_now]);
					err = 1;
					goto error_end;
				}
				parity_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[source_off + i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if ((files[last_file].state & 7) != 0){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if ((files[last_file].state & 32) != 0){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む (追加訂正失敗時も)
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				len = s_blk[source_off + i].size;
				file_off = (source_off + i - files[last_file].b_off) * (__int64)block_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", source_off + i);
					err = 1;
					goto error_end;
				}
				if (len < block_size)
					memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
				// ソース・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
			}

			if (i + 1 < read_num){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += block_lost;
					if (GetTickCount() - time_last >= UPDATE_TIME){
						if (print_progress(((__int64)prog_num * 1000) / prog_base)){
							err = 2;
							goto error_end;
						}
						time_last = GetTickCount();
					}
					// 計算終了したブロックの次から計算を開始する
					th->off += 1;
					th->mat = mat + (th->off + source_off);
					th->now = -1;	// 初期値 - 1
					for (j = 0; j < (cpu_num + 1) / 2; j++){	// 読み込み中はスレッド数を減らす
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}
		}
		if (hFile){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects((cpu_num + 1) / 2, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (source_off + th->off == 0)	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * block_lost);
#ifdef TIMER
		j = (th->off * 1000) / read_num;
		printf("partial decode = %d (%d.%d%%), read = %d\n", th->off, j / 10, j % 10, read_count);
#endif

		// 最初の消失ブロックを探す
		recv_now = 0;	// 何番目の消失ブロックか
		for (i = 0; i < source_num; i++){
			if (s_blk[i].exist == 0){
				// 経過表示
				prog_num += read_num - th->off;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress(((__int64)prog_num * 1000) / prog_base)){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
				// 最初のブロックの復元を開始しておく
				//printf("%d: s_blk[%d].size = %d\n", recv_now, i, s_blk[i].size);
				work_buf = p_buf;
				th->count = read_num;
				th->mat = mat + source_off;
				th->buf = work_buf;
				th->now = -1;	// 初期値 - 1
				for (j = 0; j < cpu_num; j++){
					ResetEvent(hEnd[j]);	// リセットしておく
					SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
				}
				break;
			}
		}

		// 失われたソース・ブロックごとに復元する
		last_file = -1;
		while (recv_now < block_lost){
			recv_now++;	// 次の消失ブロックの番号にする
			// ブロックの復元が終わるのを待って完成させる
			WaitForMultipleObjects(cpu_num, hEnd, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
			for (j = 1; j < cpu_num; j++){
				// 最後にサブ・スレッドのバッファーを XOR する
				galois_align_multiply(sub_buf + (size_t)unit_size * (j - 1), work_buf, unit_size, 1);
			}

			if (recv_now < block_lost){	// 最後のブロック以外なら
				// 次の消失ブロックを探す
				for (k = i + 1; k < source_num; k++){
					if (s_blk[k].exist == 0)
						break;	// 復元するブロックは必ず存在するはず
				}
				// 経過表示
				prog_num += read_num - th->off;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress(((__int64)prog_num * 1000) / prog_base)){
						err = 2;
						goto error_end;
					}
					time_last = GetTickCount();
				}
				// 次のブロックの復元を開始しておく
				//printf("%d: s_blk[%d].size = %d\n", recv_now, k, s_blk[k].size);
				th->mat = mat + (source_num * recv_now + source_off);	// 読み込んだソース・ブロックの位置までずらす
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
				// 復元されたソース・ブロックのチェックサムを検証する
				checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
				if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovered input slice %d\n", i);
					err = 1;
					goto error_end;
				}
				// 作業用のソース・ファイルに書き込む
				if (s_blk[i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
					//printf("file %d, open %S\n", last_file, file_path);
				}
				// ソース・ファイル内でのブロックの大きさと位置
				if (file_write_data(hFile, (i - files[last_file].b_off) * (__int64)block_size, work_buf, s_blk[i].size)){
					printf("file_write_data, input slice %d\n", i);
					err = 1;
					goto error_end;
				}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif
			}

			work_buf += unit_size;
			i = k;	// 次の消失ブロックの番号
		}

		source_off += read_num;
		// 最後の作業ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
	}
	print_progress_done();	// 末尾ブロックの断片化によっては 100% で完了するとは限らない

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
static DWORD WINAPI thread_decode_each(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf, *work_buf;
	unsigned short *factor, *factor2;
	int i, j, th_id, block_lost, src_start, src_num, *last_id, max_num;
	unsigned int unit_size, len, off, chunk_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int loop_count2a = 0, loop_count2b = 0;
unsigned int time_start2, time_encode2a = 0, time_encode2b = 0;
#endif

	th = (RS_TH *)lpParameter;
	last_id = (int *)(th->mat);
	p_buf = th->buf;
	unit_size = th->size;
	chunk_size = th->off;
	block_lost = th->count;
	th_id = th->now;	// スレッド番号
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	last_id += th_id;	// 最後に計算したブロック番号を記録しておく
	max_num = ((unit_size + chunk_size - 1) / chunk_size) * block_lost;

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		factor = th->mat;
		src_start = th->off;	// ソース・ブロック番号

		if (th->size == 1){	// ソース・ブロック読み込み中
			len = chunk_size;
			// パリティ・ブロックごとに掛け算して追加していく
			while ((j = InterlockedIncrement(&(th->now))) < max_num){	// j = ++th_now
				off = j / block_lost;	// chunk の番号
				j = j % block_lost;		// lost block の番号
				off *= chunk_size;
				if (off + len > unit_size)
					len = unit_size - off;	// 最後の chunk だけサイズが異なるかも
				if (src_start == 0)	// 最初のブロックを計算する際に
					memset(p_buf + ((size_t)unit_size * j + off), 0, len);	// ブロックを 0で埋める
				galois_align_multiply(s_buf + off, p_buf + ((size_t)unit_size * j + off), len, factor[source_num * j]);
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
			while ((j = InterlockedIncrement(&(th->now))) < block_lost){	// j = ++th_now
				work_buf = p_buf + (size_t)unit_size * j;
				factor2 = factor + source_num * j;

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
printf(" 1st decode %d.%03d sec, %d loop\n", time_encode2a / 1000, time_encode2a % 1000, loop_count2a);
printf(" 2nd decode %d.%03d sec, %d loop\n", time_encode2b / 1000, time_encode2b % 1000, loop_count2b);
#endif

	// 終了処理
	CloseHandle(hRun);
	CloseHandle(hEnd);
	return 0;
}

// GPU を制御するためのスレッド (スレッド番号は 0 にすること)
static DWORD WINAPI thread_decode_gpu(LPVOID lpParameter)
{
	unsigned char *s_buf, *p_buf;
	unsigned short *factor, *factor2;
	int i, j, k, block_lost, src_start, src_num, gpu_max, gpu_num, *last_id;
	unsigned int unit_size;
	HANDLE hRun, hEnd;
	RS_TH *th;
#ifdef TIMER
unsigned int time_start2, time_encode2 = 0, loop_count2 = 0;
#endif

	th = (RS_TH *)lpParameter;
	last_id = (int *)(th->mat);
	p_buf = th->buf;
	unit_size = th->size;
	block_lost = th->count;
	gpu_max = th->now;
	hRun = th->run;
	hEnd = th->end;
	SetEvent(hEnd);	// 設定完了を通知する

	factor2 = (unsigned short *)(p_buf + ((size_t)unit_size * block_lost + HASH_SIZE));

	WaitForSingleObject(hRun, INFINITE);	// 計算開始の合図を待つ
	while (th->now < INT_MAX / 2){
#ifdef TIMER
time_start2 = GetTickCount();
#endif
		s_buf = th->buf;
		factor = th->mat;
		src_start = th->off;	// ソース・ブロック番号

		// GPU は複数のブロックを同時に計算する
		src_num = th->count;
		*last_id = 0;
		while ((j = InterlockedExchangeAdd(&(th->now), gpu_max)) < block_lost - 1){	// th_now += gpu_max
			// InterlockedExchangeAdd は加算前の値が戻ることに注意 (InterlockedIncrement は加算後の値が戻る)
			j++;
			gpu_num = gpu_max;
			if (j + gpu_num > block_lost)
				gpu_num = block_lost - j;
			//printf("GPU : start %d, num = %d \n", j, gpu_num);

			// ブロックごとの倍率をコピーする
			for (k = 0; k < gpu_num; k++){
				for (i = 0; i < src_num; i++){
					factor2[src_num * k + i] = factor[source_num * (j + k) + i];
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

int decode_method4(	// 全てのブロックを断片的に保持する場合 (GPU対応)
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,		// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, last_file, cpu_num1, cpu_off1, recv_now;
	int write_id, last_id[(MAX_CPU + 1) * 2], last_id_min;
	int cover_max, cover_ave, cover_from, cover_num, gpu_max;
	unsigned int io_size, unit_size, len, block_off;
	unsigned int time_last;
	__int64 file_off, prog_num = 0, prog_base;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU + 1], hRun[MAX_CPU + 1], hEnd[MAX_CPU + 1];
	RS_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * (MAX_CPU));
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか

	// 作業バッファーを確保する
	i = GPU_MAX_NUM + 1;	// GPU の作業領域として GPU_MAX_NUM + 1 個の余裕を見ておく
	if (block_lost < GPU_MAX_NUM)
		i = block_lost + 1;
	io_size = get_io_size_gpu(source_num + block_lost + i, 1);
	//io_size = (((io_size + 2) / 3 + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1)) - HASH_SIZE;	// 実験用
	unit_size = io_size + HASH_SIZE;	// チェックサムの分だけ増やす
	file_off = (source_num + block_lost) * (size_t)unit_size + HASH_SIZE
			+ (source_num * sizeof(unsigned short) * GPU_MAX_NUM);
	buf = _aligned_malloc((size_t)file_off, 4096);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * source_num;	// 復元したブロックを記録する領域
	hash = p_buf + (size_t)unit_size * block_lost;
	prog_base = (block_size + io_size - 1) / io_size;
	prog_base *= source_num * block_lost;	// 全体の断片の個数
	cpu_num1 = (cpu_num + 1) / 2;	// 読み込み中はスレッド数を減らす 1~2=1, 3~4=2, 5~6=3, 7=4
	cpu_off1 = 0;
#ifdef TIMER
	printf("\n read all blocks, and keep all recovering blocks\n");
	printf("buffer size = %I64d MB, io_size = %d, split = %d\n", file_off >> 20, io_size, (block_size + io_size - 1) / io_size);
#endif

	// OpenCL の初期化
	cover_max = source_num;
	gpu_max = block_lost;
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
		cpu_off1++;	// GPU 用のスレッドは 1st decode に使わない
	}

	// マルチ・スレッドの準備をする
	th->mat = (unsigned short *)last_id;
	th->buf = p_buf;
	th->size = unit_size;
	th->count = block_lost;
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
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_decode_gpu, (LPVOID)th, 0, NULL);
		} else {
			th->now = j;	// スレッド番号
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_decode_each, (LPVOID)th, 0, NULL);
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

	// ブロック断片を読み込んで、ソース・ブロック断片を復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	block_off = 0;
	while (block_off < block_size){
		th->size = 1;	// 1st decode
		th->off = -1;	// まだ計算して無い印
		write_id = 0;

#ifdef TIMER
time_start = GetTickCount();
#endif
		last_file = -1;
		recv_now = 0;	// 何番目の代替ブロックか
		for (i = 0; i < source_num; i++){
			switch(s_blk[i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				len = block_size - block_off;
				if (len > io_size)
					len = io_size;
				file_off = p_blk[id[recv_now]].off + (__int64)block_off;
				if (file_read_data(rcv_hFile[p_blk[id[recv_now]].file], file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, recovery slice %d\n", id[recv_now]);
					err = 1;
					goto error_end;
				}
				if (len < io_size)
					memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
				recv_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				len = 0;
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if ((files[last_file].state & 7) != 0){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if ((files[last_file].state & 32) != 0){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				if (s_blk[i].size > block_off){
					len = s_blk[i].size - block_off;
					if (len > io_size)
						len = io_size;
					file_off = (i - files[last_file].b_off) * (__int64)block_size + (__int64)block_off;
					if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
						printf("file_read_data, input slice %d\n", i);
						err = 1;
						goto error_end;
					}
					if (len < io_size)
						memset(buf + ((size_t)unit_size * i + len), 0, io_size - len);
					// ソース・ブロックのチェックサムを計算する
					checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + io_size), io_size);
#ifdef TIMER
read_count++;
#endif
				} else {
					len = 0;
					memset(buf + (size_t)unit_size * i, 0, unit_size);
				}
			}

			if ((len > 0) && (i + 1 < source_num)){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += block_lost;
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
						while ((s_blk[th->off].exist != 0) &&
								((s_blk[th->off].size <= block_off) || (s_blk[th->off].exist == 3))){
							prog_num += block_lost;
							th->off += 1;
#ifdef TIMER
skip_count++;
#endif
						}
					}
					th->buf = buf + (size_t)unit_size * th->off;
					th->mat = mat + th->off;
					th->now = -1;	// 初期値 - 1
					for (j = cpu_off1; j < cpu_off1 + cpu_num1; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}
		}
		if (hFile){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->size = 2;	// 2nd decode
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off > 0){	// 計算不要なソース・ブロックはとばす
			while ((s_blk[th->off].exist != 0) &&
					((s_blk[th->off].size <= block_off) || (s_blk[th->off].exist == 3))){
				prog_num += block_lost;
				th->off += 1;
#ifdef TIMER
skip_count++;
#endif
			}
		} else {	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * block_lost);
		}
#ifdef TIMER
		j = (th->off * 1000) / source_num;
		printf("partial decode = %d (%d.%d%%), read = %d, skip = %d\n", th->off, j / 10, j % 10, read_count, skip_count);
#endif
		recv_now = -1;	// 消失ブロックの本来のソース番号
		last_file = -1;

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
			th->mat = mat + cover_from;
			th->off = cover_from;
			th->count = cover_num;
			th->now = -1;	// 初期値 - 1
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME/4 だけ待つ
			while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME / 4) == WAIT_TIMEOUT){
				last_id_min = block_lost;	// 何ブロック目まで計算が終わってるか
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
						for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
							if (s_blk[j].exist == 0){
								recv_now = j;
								break;
							}
						}
						//printf(" lost block[%d] = source block[%d]\n", write_id, recv_now);

						// 復元されたソース・ブロックのチェックサムを検証する
						checksum16_return(work_buf, hash, io_size);
						if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
							printf("checksum mismatch, recovered input slice %d\n", recv_now);
							err = 1;
							goto error_end;
						}
						if (s_blk[recv_now].size <= block_off){	// 書き込み不要
							write_id++;
							continue;
						}
						// 作業用のソース・ファイルに書き込む
						if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
							last_file = s_blk[recv_now].file;
							if (hFile){
								CloseHandle(hFile);	// 前のファイルを閉じる
								hFile = NULL;
							}
							get_temp_name(list_buf + files[last_file].name, file_path + base_len);
							hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
							if (hFile == INVALID_HANDLE_VALUE){
								print_win32_err();
								hFile = NULL;
								printf_cp("cannot open file, %s\n", file_path);
								err = 1;
								goto error_end;
							}
							//printf("file %d, open %S\n", last_file, file_path);
						}
						// ソース・ファイル内でのブロック断片の大きさと位置
						len = s_blk[recv_now].size - block_off;
						if (len > io_size)
							len = io_size;
						if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size + block_off, work_buf, len)){
							printf("file_write_data, input slice %d\n", recv_now);
							err = 1;
							goto error_end;
						}
#ifdef TIMER
write_count++;
#endif
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
			prog_num += cover_num * block_lost;
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
		// 残りの復元されたブロックごとに
		work_buf = p_buf + (size_t)unit_size * write_id;
		//printf("write block from %d to %d\n", write_id, block_lost - 1);
		for (i = write_id; i < block_lost; i++){
			for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
				if (s_blk[j].exist == 0){
					recv_now = j;
					break;
				}
			}
			//printf(" lost block[%d] = source block[%d]\n", i, recv_now);

			// 復元されたソース・ブロックのチェックサムを検証する
			checksum16_return(work_buf, hash, io_size);
			if (memcmp(work_buf + io_size, hash, HASH_SIZE) != 0){
				printf("checksum mismatch, recovered input slice %d\n", recv_now);
				err = 1;
				goto error_end;
			}
			if (s_blk[recv_now].size <= block_off){	// 書き込み不要
				work_buf += unit_size;
				continue;
			}
			// 作業用のソース・ファイルに書き込む
			if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
				last_file = s_blk[recv_now].file;
				if (hFile){
					CloseHandle(hFile);	// 前のファイルを閉じる
					hFile = NULL;
				}
				get_temp_name(list_buf + files[last_file].name, file_path + base_len);
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					hFile = NULL;
					printf_cp("cannot open file, %s\n", file_path);
					err = 1;
					goto error_end;
				}
				//printf("file %d, open %S\n", last_file, file_path);
			}
			// ソース・ファイル内でのブロック断片の大きさと位置
			len = s_blk[recv_now].size - block_off;
			if (len > io_size)
				len = io_size;
			if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size + block_off, work_buf, len)){
				printf("file_write_data, input slice %d\n", recv_now);
				err = 1;
				goto error_end;
			}
#ifdef TIMER
write_count++;
#endif
			work_buf += unit_size;
		}
#ifdef TIMER
time_write += GetTickCount() - time_start;
#endif

		block_off += io_size;
		// 最後の作業ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
	}
	print_progress_done();	// 末尾ブロックの断片化によっては 100% で完了するとは限らない

#ifdef TIMER
printf("read   %d.%03d sec\n", time_read / 1000, time_read % 1000);
j = ((block_size + io_size - 1) / io_size) * block_lost;
printf("write  %d.%03d sec, count = %d/%d\n", time_write / 1000, time_write % 1000, write_count, j);
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

int decode_method5(	// 復元するブロックだけ保持する場合 (GPU対応)
	wchar_t *file_path,
	int block_lost,			// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,		// リカバリ・ファイルのハンドル
	file_ctx_r *files,		// ソース・ファイルの情報
	source_ctx_r *s_blk,	// ソース・ブロックの情報
	parity_ctx_r *p_blk,	// パリティ・ブロックの情報
	unsigned short *mat)
{
	unsigned char *buf = NULL, *p_buf, *work_buf, *hash;
	unsigned short *id;
	int err = 0, i, j, last_file, cpu_num1, cpu_off1, source_off, read_num, recv_now, parity_now;
	int write_id, last_id[(MAX_CPU + 1) * 2], last_id_min;
	int cover_max, cover_ave, cover_from, cover_num, gpu_max;
	unsigned int unit_size, len;
	unsigned int time_last, prog_num = 0, prog_base;
	__int64 file_off;
	HANDLE hFile = NULL;
	HANDLE hSub[MAX_CPU + 1], hRun[MAX_CPU + 1], hEnd[MAX_CPU + 1];
	RS_TH th[1];

	memset(hSub, 0, sizeof(HANDLE) * (MAX_CPU));
	id = mat + (block_lost * source_num);	// 何番目の消失ソース・ブロックがどのパリティで代替されるか
	unit_size = (block_size + HASH_SIZE + (MEM_UNIT - 1)) & ~(MEM_UNIT - 1);	// MEM_UNIT の倍数にする

	// 作業バッファーを確保する
	i = GPU_MAX_NUM + 1;	// GPU の作業領域として GPU_MAX_NUM + 1 個の余裕を見ておく
	if (block_lost < GPU_MAX_NUM)
		i = block_lost + 1;
	read_num = read_block_num_gpu(block_lost, i, 1);	// ソース・ブロックを何個読み込むか
	if (read_num == 0){
		//printf("\n cannot keep enough blocks, use another method\n");
		return -4;	// スライスを分割して処理しないと無理
	}
	//read_num = (read_num + 2) / 3 + 1;	// 実験用
	file_off = (read_num + block_lost) * (size_t)unit_size + HASH_SIZE
			+ (source_num * sizeof(unsigned short) * GPU_MAX_NUM);
	buf = _aligned_malloc((size_t)file_off, 4096);	// GPU 用の境界
	if (buf == NULL){
		printf("malloc, %I64d\n", file_off);
		err = 1;
		goto error_end;
	}
	p_buf = buf + (size_t)unit_size * read_num;	// パリティ・ブロックを記録する領域
	hash = p_buf + (size_t)unit_size * block_lost;
	prog_base = source_num * block_lost;	// ブロックの合計掛け算個数
	cpu_num1 = (cpu_num + 1) / 2;	// 読み込み中はスレッド数を減らす 1~2=1, 3~4=2, 5~6=3, 7=4
	cpu_off1 = 0;
#ifdef TIMER
	printf("\n read some blocks, and keep all recovering blocks\n");
	printf("buffer size = %I64d MB, read_num = %d, round = %d\n", file_off >> 20, read_num, (source_num + read_num - 1) / read_num);
#endif

	// OpenCL の初期化
	cover_max = source_num;
	gpu_max = block_lost;
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
		cpu_off1++;	// GPU 用のスレッドは 1st decode に使わない
	}

	// マルチ・スレッドの準備をする
	th->mat = (unsigned short *)last_id;
	th->buf = p_buf;
	th->size = unit_size;
	th->count = block_lost;
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
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_decode_gpu, (LPVOID)th, 0, NULL);
		} else {
			th->now = j;	// スレッド番号
			hSub[j] = (HANDLE)_beginthreadex(NULL, 65536, thread_decode_each, (LPVOID)th, 0, NULL);
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

	// 何回かに別けてブロックを読み込んで、ソース・ブロックを少しずつ復元する
	print_progress_text(0, "Recovering slice");
	time_last = GetTickCount();
	wcscpy(file_path, base_dir);
	write_id = 0;
	parity_now = 0;	// 何番目の代替ブロックか
	source_off = 0;	// 読み込み開始スライス番号
	while (source_off < source_num){
		if (read_num > source_num - source_off)
			read_num = source_num - source_off;
		th->size = 1;	// 1st decode
		th->off = source_off - 1;	// まだ計算して無い印

#ifdef TIMER
time_start = GetTickCount();
#endif
		last_file = -1;
		for (i = 0; i < read_num; i++){	// スライスを一個ずつ読み込んでメモリー上に配置していく
			switch(s_blk[source_off + i].exist){
			case 0:		// バッファーにパリティ・ブロックの内容を読み込む
				if (file_read_data(rcv_hFile[p_blk[id[parity_now]].file], p_blk[id[parity_now]].off, buf + (size_t)unit_size * i, block_size)){
					printf("file_read_data, recovery slice %d\n", id[parity_now]);
					err = 1;
					goto error_end;
				}
				parity_now++;
				// パリティ・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
				break;
			case 3:		// ソース・ブロックの内容は全て 0
				memset(buf + (size_t)unit_size * i, 0, unit_size);
				break;
			default:	// バッファーにソース・ブロックの内容を読み込む
				if (s_blk[source_off + i].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[source_off + i].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					if ((files[last_file].state & 7) != 0){	// 作り直した作業ファイルから読み込む
						get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					} else if ((files[last_file].state & 32) != 0){	// 名前訂正失敗時には別名ファイルから読み込む
						wcscpy(file_path + base_len, list_buf + files[last_file].name2);
					} else {	// 完全なソース・ファイルから読み込む (追加訂正失敗時も)
						wcscpy(file_path + base_len, list_buf + files[last_file].name);
					}
					hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
				}
				len = s_blk[source_off + i].size;
				file_off = (source_off + i - files[last_file].b_off) * (__int64)block_size;
				if (file_read_data(hFile, file_off, buf + (size_t)unit_size * i, len)){
					printf("file_read_data, input slice %d\n", source_off + i);
					err = 1;
					goto error_end;
				}
				if (len < block_size)
					memset(buf + ((size_t)unit_size * i + len), 0, block_size - len);
				// ソース・ブロックのチェックサムを計算する
				checksum16_altmap(buf + (size_t)unit_size * i, buf + ((size_t)unit_size * i + unit_size - HASH_SIZE), unit_size - HASH_SIZE);
#ifdef TIMER
read_count++;
#endif
			}

			if (i + 1 < read_num){	// 最後のブロック以外なら
				// サブ・スレッドの動作状況を調べる
				j = WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, 0);
				if ((j != WAIT_TIMEOUT) && (j != WAIT_FAILED)){	// 計算中でないなら
					// 経過表示
					prog_num += block_lost;
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
					th->mat = mat + th->off;
					th->now = -1;	// 初期値 - 1
					for (j = cpu_off1; j < cpu_off1 + cpu_num1; j++){
						ResetEvent(hEnd[j]);	// リセットしておく
						SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
					}
				}
			}
		}
		if (hFile){	// 最後のソース・ファイルを閉じる
			CloseHandle(hFile);
			hFile = NULL;
		}
#ifdef TIMER
time_read += GetTickCount() - time_start;
#endif

		WaitForMultipleObjects(cpu_num1, hEnd + cpu_off1, TRUE, INFINITE);	// サブ・スレッドの計算終了の合図を待つ
		th->size = 2;	// 2nd decode
		th->off += 1;	// 計算を開始するソース・ブロックの番号
		if (th->off == 0)	// エラーや実験時以外は th->off は 0 にならない
			memset(p_buf, 0, (size_t)unit_size * block_lost);
#ifdef TIMER
		j = (th->off - source_off) * 1000 / read_num;
		printf("partial decode = %d (%d.%d%%), read = %d\n", th->off - source_off, j / 10, j % 10, read_count);
#endif
		recv_now = -1;	// 消失ブロックの本来のソース番号
		last_file = -1;

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
			th->mat = mat + (source_off + cover_from);
			th->off = source_off + cover_from;	// ソース・ブロックの番号にする
			th->count = cover_num;
			th->now = -1;	// 初期値 - 1
			for (j = 0; j < cpu_num; j++){
				ResetEvent(hEnd[j]);	// リセットしておく
				SetEvent(hRun[j]);	// サブ・スレッドに計算を開始させる
			}

			// サブ・スレッドの計算終了の合図を UPDATE_TIME/4 だけ待つ
			while (WaitForMultipleObjects(cpu_num, hEnd, TRUE, UPDATE_TIME / 4) == WAIT_TIMEOUT){
				last_id_min = block_lost;	// 何ブロック目まで計算が終わってるか
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
						for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
							if (s_blk[j].exist == 0){
								recv_now = j;
								break;
							}
						}
						//printf(" lost block[%d] = source block[%d]\n", write_id, recv_now);

						// 復元されたソース・ブロックのチェックサムを検証する
						checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
						if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
							printf("checksum mismatch, recovered input slice %d\n", recv_now);
							err = 1;
							goto error_end;
						}
						// 作業用のソース・ファイルに書き込む
						if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
							last_file = s_blk[recv_now].file;
							if (hFile){
								CloseHandle(hFile);	// 前のファイルを閉じる
								hFile = NULL;
							}
							get_temp_name(list_buf + files[last_file].name, file_path + base_len);
							hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
							if (hFile == INVALID_HANDLE_VALUE){
								print_win32_err();
								hFile = NULL;
								printf_cp("cannot open file, %s\n", file_path);
								err = 1;
								goto error_end;
							}
							//printf("file %d, open %S\n", last_file, file_path);
						}
						if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size, work_buf, s_blk[recv_now].size)){
							printf("file_write_data, input slice %d\n", recv_now);
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
			prog_num += cover_num * block_lost;
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
			// 残りの復元されたブロックごとに
			work_buf = p_buf + (size_t)unit_size * write_id;
			//printf("write block from %d to %d\n", write_id, block_lost - 1);
			for (i = write_id; i < block_lost; i++){
				for (j = recv_now + 1; j < source_num; j++){	// 何番のソース・ブロックか
					if (s_blk[j].exist == 0){
						recv_now = j;
						break;
					}
				}
				//printf(" lost block[%d] = source block[%d]\n", i, recv_now);

				// 復元されたソース・ブロックのチェックサムを検証する
				checksum16_return(work_buf, hash, unit_size - HASH_SIZE);
				if (memcmp(work_buf + unit_size - HASH_SIZE, hash, HASH_SIZE) != 0){
					printf("checksum mismatch, recovered input slice %d\n", recv_now);
					err = 1;
					goto error_end;
				}
				// 作業用のソース・ファイルに書き込む
				if (s_blk[recv_now].file != last_file){	// 別のファイルなら開く
					last_file = s_blk[recv_now].file;
					if (hFile){
						CloseHandle(hFile);	// 前のファイルを閉じる
						hFile = NULL;
					}
					get_temp_name(list_buf + files[last_file].name, file_path + base_len);
					hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
					if (hFile == INVALID_HANDLE_VALUE){
						print_win32_err();
						hFile = NULL;
						printf_cp("cannot open file, %s\n", file_path);
						err = 1;
						goto error_end;
					}
					//printf("file %d, open %S\n", last_file, file_path);
				}
				if (file_write_data(hFile, (recv_now - files[last_file].b_off) * (__int64)block_size, work_buf, s_blk[recv_now].size)){
					printf("file_write_data, input slice %d\n", recv_now);
					err = 1;
					goto error_end;
				}
	#ifdef TIMER
	write_count++;
	#endif
				work_buf += unit_size;
			}
	#ifdef TIMER
	time_write += GetTickCount() - time_start;
	#endif
		}

		source_off += read_num;
		// 最後の作業ファイルを閉じる
		CloseHandle(hFile);
		hFile = NULL;
	}
	print_progress_done();	// 末尾ブロックの断片化によっては 100% で完了するとは限らない

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

