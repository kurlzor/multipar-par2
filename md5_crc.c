// md5_crc.c
// Copyright : 2016-06-12 Yutaka Sawada
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

#include <process.h>
#include <stdio.h>

#include <windows.h>

#include "common2.h"
#include "crc.h"
#include "phmd5.h"
#include "md5_crc.h"


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// バイト配列の MD5 ハッシュ値を求める
void data_md5(
	unsigned char *data_in,	// ハッシュ値を求めるバイト配列
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash)	// ハッシュ値 16バイト
{
	PHMD5 ctx;

	Phmd5Begin(&ctx);
	Phmd5Process(&ctx, data_in, data_len);
	Phmd5End(&ctx);
	memcpy(hash, ctx.hash, 16);
}

// ブロックの MD5 ハッシュ値を求める
void data_md5_block(
	unsigned char *data_in,	// ハッシュ値を求めるバイト配列
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash)	// ハッシュ値 16バイト
{
	PHMD5 ctx;

	Phmd5Begin(&ctx);
	Phmd5Process(&ctx, data_in, data_len);
	if (data_len < block_size)	// ブロック・サイズまで 0で埋めて計算する
		Phmd5ProcessZero(&ctx, block_size - data_len);
	Phmd5End(&ctx);
	memcpy(hash, ctx.hash, 16);
}

// ファイルの MD5-16k ハッシュ値を求める
int file_md5_16(
	wchar_t *file_path,		// ハッシュ値を求めるファイル
	unsigned char *hash)	// ファイルの先頭 16KB 分のハッシュ値 (16バイト)
{
	unsigned char buf[16384];
	unsigned int len;
	PHMD5 hash_ctx;
	HANDLE hFile;

	// ソース・ファイルを開く
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return 1;

	// 先頭 16KB のハッシュ値を計算する
	Phmd5Begin(&hash_ctx);	// 初期化
	if (!ReadFile(hFile, buf, 16384, &len, NULL)){
		CloseHandle(hFile);
		return 2;
	}
	Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
	Phmd5End(&hash_ctx);	// 最終処理
	memcpy(hash, hash_ctx.hash, 16);

	CloseHandle(hFile);
	return 0;
}

// ファイルの指定部分の MD5 ハッシュ値を求める
int file_md5(
	HANDLE hFileRead,		// ハッシュ値を求めるファイルのハンドル
	__int64 offset,
	unsigned int data_len,	// 入力バイト数
	unsigned char *hash)	// ハッシュ値 (16バイト)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;
	PHMD5 hash_ctx;

	// ファイルの開始位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN))
		return 1;

	Phmd5Begin(&hash_ctx);	// 初期化

	while (data_len > 0){
		len = IO_SIZE;
		if (data_len < IO_SIZE)
			len = data_len;
		if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv))
			return 2;
		data_len -= len;

		Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
	}
	Phmd5End(&hash_ctx);	// 最終処理
	memcpy(hash, hash_ctx.hash, 16);

	return 0;
}

/*------------------  以下は MD5 と CRC-32 の組合せ関数  ------------------*/

// 内容が全て 0 のデータの MD5 ハッシュ値と CRC-32 を求める
void data_md5_crc32_zero(
	unsigned char *hash)	// ハッシュ値 (16 + 4バイト, MD5 + CRC-32)
{
	PHMD5 ctx;

	Phmd5Begin(&ctx);
	Phmd5ProcessZero(&ctx, block_size);
	Phmd5End(&ctx);
	memcpy(hash, ctx.hash, 16);
	memcpy(hash + 16, &window_mask, 4);	// CRC-32 は window_mask と同じ
}

// 2GB 未満のファイルの開始位置以降のハッシュ値を計算する
unsigned int file_crc_part(HANDLE hFile)
{
	unsigned char buf[4096];
	unsigned int len, crc = 0xFFFFFFFF;

	// 末尾まで読み込む
	do {
		if (!ReadFile(hFile, buf, 4096, &len, NULL) || (len == 0))
			break;
		crc = crc_update(crc, buf, len);
	} while (len > 0);

	return crc ^ 0xFFFFFFFF;
}

// ファイルの offset バイト目からブロック・サイズ分の MD5 と CRC-32 を求める
int file_md5_crc32_block(
	HANDLE hFileRead,			// MD5 と CRC を求めるファイルのハンドル
	__int64 offset,
	unsigned int avail_size,	// 入力バイト数
	unsigned char *hash)		// ハッシュ値 (16 + 4バイト, MD5 + CRC-32)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len, left, crc;
	PHMD5 hash_ctx;

	// ファイルの開始位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN))
		return 1;

	crc = 0xFFFFFFFF;	// 初期化
	Phmd5Begin(&hash_ctx);

	left = avail_size;	// 実際に読み込むサイズ
	while (left > 0){
		len = IO_SIZE;
		if (left < IO_SIZE)
			len = left;
		if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv))
			return 2;
		left -= len;

		crc = crc_update(crc, buf, len);	// CRC-32 計算
		Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
	}

	if (avail_size < block_size){	// ブロック・サイズまで 0で埋めて計算する
		crc = crc_update_zero(crc, block_size - avail_size);
		Phmd5ProcessZero(&hash_ctx, block_size - avail_size);
	}

	crc ^= 0xFFFFFFFF;	// 最終処理
	memcpy(hash + 16, &crc, 4);
	Phmd5End(&hash_ctx);
	memcpy(hash, hash_ctx.hash, 16);

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

//#define TIMER // 実験用

#ifdef TIMER
//#undef IO_SIZE
//#define IO_SIZE	131072
static unsigned int time_start, time1_start, time2_start;
static unsigned int time_total = 0, time2_total = 0, time3_total = 0, time4_total = 0;
static unsigned int time5_total = 0, time6_total = 0, time7_total = 0;
#endif

typedef struct {
	unsigned char * volatile buf;
	volatile int size;
	HANDLE volatile hFile;
	HANDLE run;
	HANDLE end;
} HASH_TH;

// 読み込み用のサブ・スレッド
static DWORD WINAPI thread_read(LPVOID lpParameter)
{
	int rv;
	HASH_TH *th;
	PHMD5 *hash_ctx;

	th = (HASH_TH *)lpParameter;
	hash_ctx = (PHMD5 *)(th->buf);
	SetEvent(th->end);	// 設定終了を通知する

	WaitForSingleObject(th->run, INFINITE);	// 開始の合図を待つ
	while (th->size > 0){
#ifdef TIMER
time2_start = GetTickCount();
#endif
		// ファイルから読み込む
		if (!ReadFile(th->hFile, th->buf, th->size, &rv, NULL) || (th->size != rv)){
			th->size = -1;	// エラーなら
		} else if (cpu_num > 1){
#ifdef TIMER
time2_total += GetTickCount() - time2_start;
time2_start = GetTickCount();
#endif
			Phmd5Process(hash_ctx, th->buf, th->size);	// MD5 計算
#ifdef TIMER
time3_total += GetTickCount() - time2_start;
#endif
		}
		SetEvent(th->end);	// 読み込み終了を通知する
		WaitForSingleObject(th->run, INFINITE);	// 開始の合図を待つ
	}

	// 終了処理
	CloseHandle(th->run);
	CloseHandle(th->end);
	return 0;
}

// ハッシュ値計算用のサブ・スレッド
static DWORD WINAPI thread_hash(LPVOID lpParameter)
{
	HASH_TH *th;
	PHMD5 *hash_ctx;

	th = (HASH_TH *)lpParameter;
	hash_ctx = (PHMD5 *)(th->buf);
	SetEvent(th->end);	// 設定終了を通知する

	WaitForSingleObject(th->run, INFINITE);	// 開始の合図を待つ
	while (th->size > 0){
		Phmd5Process(hash_ctx, th->buf, th->size);	// MD5 計算
		SetEvent(th->end);	// 計算終了を通知する
		WaitForSingleObject(th->run, INFINITE);	// 開始の合図を待つ
	}

	// 終了処理
	CloseHandle(th->run);
	CloseHandle(th->end);
	return 0;
}

// ファイルのハッシュ値と各スライスのチェックサムを同時に計算する
int file_hash_crc(
	wchar_t *file_path,		// ハッシュ値を求めるファイル
	__int64 file_left,
	unsigned char *hash,	// ハッシュ値 (16バイト)
	unsigned char *sum,		// チェックサム (MD5 + CRC-32) の配列
	__int64 *prog_now)		// 経過表示での現在位置
{
	unsigned char *buf, buf1[IO_SIZE * 2];
	unsigned int err = 0, len, off, crc, block_left = 0, read_size;
	unsigned int time_last;
	PHMD5 hash_ctx, block_ctx;
	HANDLE hFile, hSub;
	HASH_TH th[1];
#ifdef TIMER
time1_start = GetTickCount();
#endif

	time_last = GetTickCount() / UPDATE_TIME;	// 時刻の変化時に経過を表示する
	hSub = NULL;
	buf = buf1 + IO_SIZE;

	// ソース・ファイルを開く
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		print_win32_err();
		return 1;
	}

	// ファイル・アクセス用スレッドの準備をする
	th->size = 0;
	th->run = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
	if (th->run == NULL){
		print_win32_err();
		printf("error, sub-thread\n");
		err = 1;
		goto error_end;
	}
	th->end = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (th->end == NULL){
		print_win32_err();
		CloseHandle(th->run);
		printf("error, sub-thread\n");
		err = 1;
		goto error_end;
	}
	th->buf = (unsigned char *)&hash_ctx;
	th->hFile = hFile;
	hSub = (HANDLE)_beginthreadex(NULL, 65536, thread_read, (LPVOID)th, 0, NULL);
	if (hSub == NULL){
		print_win32_err();
		CloseHandle(th->run);
		CloseHandle(th->end);
		printf("error, sub-thread\n");
		err = 1;
		goto error_end;
	}

	// 最初の分を読み込む
	read_size = IO_SIZE;
	if (file_left < IO_SIZE)
		read_size = (unsigned int)file_left;
#ifdef TIMER
time_start = GetTickCount();
#endif
	if (!ReadFile(hFile, buf1, read_size, &len, NULL) || (read_size != len)){
		print_win32_err();
		err = 1;
		goto error_end;
	}
#ifdef TIMER
time2_total += GetTickCount() - time_start;
#endif
	Phmd5Begin(&hash_ctx);
	if (cpu_num > 1)
		Phmd5Process(&hash_ctx, buf1, read_size);	// MD5 計算

	while (file_left > 0){
		len = read_size;
		file_left -= read_size;
		(*prog_now) += read_size;

		// 前回の読み込みが終わるのを待つ
		WaitForSingleObject(th->end, INFINITE);
		if (th->size < 0){	// 前回の読み込みが失敗していれば
			printf("thread_read %d\n", read_size);
			err = 1;
			goto error_end;
		}
		// 次の分を読み込み開始しておく
		if (file_left > 0){
			ResetEvent(th->end);	// リセットしておく
			read_size = IO_SIZE;
			if (file_left < IO_SIZE)
				read_size = (unsigned int)file_left;
			th->buf = buf;
			th->size = read_size;
			SetEvent(th->run);	// サブ・スレッドに読み込みを開始させる
		}
		// バッファーを入れ替える
		if (buf == buf1){
			buf = buf1 + IO_SIZE;
		} else {
			buf = buf1;
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		if (cpu_num == 1)
			Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
		off = 0;	// チェックサム計算
		if (block_left > 0){	// 前回足りなかった分を追加する
			//printf("file_left = %I64d, block_left = %d\n", file_left, block_left);
			if (block_left <= len){
				crc = crc_update(crc, buf, block_left) ^ 0xFFFFFFFF;	// CRC-32 計算
				Phmd5Process(&block_ctx, buf, block_left);	// MD5 計算
				Phmd5End(&block_ctx);	// 最終処理
				memcpy(sum, block_ctx.hash, 16);
				memcpy(sum + 16, &crc, 4);
				sum += 20;
				off += block_left;
				block_left = 0;
			} else {
				crc = crc_update(crc, buf, len);	// CRC-32 計算
				Phmd5Process(&block_ctx, buf, len);	// MD5 計算
				off = len;
				block_left -= len;
			}
		}
		for (; off < len; off += block_size){
			Phmd5Begin(&block_ctx);
			if (off + block_size <= len){
				crc = crc_update(0xFFFFFFFF, buf + off, block_size) ^ 0xFFFFFFFF;	// CRC-32 計算
				Phmd5Process(&block_ctx, buf + off, block_size);	// MD5 計算
				Phmd5End(&block_ctx);	// 最終処理
				memcpy(sum, block_ctx.hash, 16);
				memcpy(sum + 16, &crc, 4);
				sum += 20;
			} else {	// スライスが途中までなら
				crc = crc_update(0xFFFFFFFF, buf + off, len - off);	// CRC-32 計算
				Phmd5Process(&block_ctx, buf + off, len - off);	// MD5 計算
				block_left = block_size - len + off;
			}
		}
#ifdef TIMER
time4_total += GetTickCount() - time_start;
#endif

		// 経過表示
		if (GetTickCount() / UPDATE_TIME != time_last){
			if (print_progress((int)(((*prog_now) * 1000) / total_file_size))){
				err = 2;
				goto error_end;
			}
			time_last = GetTickCount() / UPDATE_TIME;
		}
	}

	// 最終ブロックが半端なら
	if (block_left > 0){	// 残りを 0 でパディングする (PAR2 仕様の欠点)
		crc = crc_update_zero(crc, block_left) ^ 0xFFFFFFFF;	// CRC-32 計算
		Phmd5ProcessZero(&block_ctx, block_left);
		Phmd5End(&block_ctx);
		memcpy(sum, block_ctx.hash, 16);
		memcpy(sum + 16, &crc, 4);
	}
	Phmd5End(&hash_ctx);	// 最終処理
	memcpy(hash, hash_ctx.hash, 16);

error_end:
	CloseHandle(hFile);
	if (hSub){
		WaitForSingleObject(th->end, INFINITE);	// 動作中なら待つ
		th->size = 0;	// 終了指示
		SetEvent(th->run);
		WaitForSingleObject(hSub, INFINITE);
		CloseHandle(hSub);
	}

#ifdef TIMER
time_total += GetTickCount() - time1_start;
if (*prog_now == total_file_size){
	printf("\nread  %d.%03d sec\n", time2_total / 1000, time2_total % 1000);
	printf("sub   %d.%03d sec\n", time3_total / 1000, time3_total % 1000);
	printf("main  %d.%03d sec\n", time4_total / 1000, time4_total % 1000);
	printf("total %d.%03d sec\n", time_total / 1000, time_total % 1000);
}
#endif
	return err;
}

// ファイルのハッシュ値が同じか調べる (全てのスライスのチェックサムも)
// 0〜=破損してるが何個目まで同じ, -1=MD5-16kが異なる, -2=キャンセル, -3=同じ(完全か追加)
int file_hash_check(
	int num,				// file_ctx におけるファイル番号
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	int prog_min,			// 検出数がこの値を超えたら合計数を表示する (INT_MAX なら表示しない)
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報 (NULL なら比較しない)
{
	unsigned char *buf, buf1[IO_SIZE * 2];
	int find_next, comp_num = 0;
	unsigned int len, off, crc, block_left = 0, read_size;
	unsigned int time_last;
	__int64 file_size, file_left;
	PHMD5 hash_ctx, block_ctx;
	HANDLE hSub;
	HASH_TH th[1];
#ifdef TIMER
time1_start = GetTickCount();
#endif

	prog_last = -1;	// 検証中のファイル名を毎回表示する
	time_last = GetTickCount();
	hSub = NULL;
	buf = buf1 + IO_SIZE;
	file_size = files[num].size;	// 本来のファイル・サイズ
	find_next = files[num].b_off;	// 先頭ブロックの番号

	// ファイルの先頭に戻す
//	file_left = 0;
//	if (!SetFilePointerEx(hFile, *((PLARGE_INTEGER)&file_left), NULL, FILE_BEGIN))
//		return -1;

	// まずは先頭 16KB のハッシュ値を比較する
	len = 16384;
	if (file_size < 16384){
		len = (unsigned int)file_size;
		file_left = 0;
	} else {
		file_left = file_size - 16384;	// 本来のファイル・サイズまでしか検査しない
	}
#ifdef TIMER
time_start = GetTickCount();
#endif
	if (!ReadFile(hFile, buf, len, &off, NULL) || (len != off))
		return -1;
#ifdef TIMER
time2_total += GetTickCount() - time_start;
#endif

	Phmd5Begin(&hash_ctx);	// 初期化
	Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
	Phmd5End(&hash_ctx);	// 最終処理
	if (file_left == 0){	// ファイルは 16KB 以下
		if (s_blk != NULL){	// 全てのスライスを比較する
			for (off = 0; off < len; off += block_size){
				Phmd5Begin(&block_ctx);
				if (off + block_size <= len){
					crc = crc_update(0, buf + off, block_size);
					Phmd5Process(&block_ctx, buf + off, block_size);
				} else {	// 末尾の半端なスライスなら
					crc = crc_update(0, buf + off, len - off);
					Phmd5Process(&block_ctx, buf + off, len - off);
					block_left = block_size - len + off;	// ブロック・サイズまで 0で埋めて計算する
					crc = crc_update_zero(crc, block_left);
					Phmd5ProcessZero(&block_ctx, block_left);
				}
				Phmd5End(&block_ctx);
				if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
					return comp_num;	// スライスの CRC-32 か MD5 が異なる
				comp_num++;
				find_next++;
			}
		}
		if (memcmp(hash_ctx.hash, files[num].hash + 16, 16) != 0)	// 16k MD5 が異なる
			return -1;
		return -3;	// 同じ
	}
	// 16KB よりも大きくて 16k MD5 が異なる場合は、スライス単位の検査をしない
	if (memcmp(hash_ctx.hash, files[num].hash + 16, 16) != 0)
		return -1;

	// 16k MD5 が一致したら残りの部分も計算する
	Phmd5Begin(&hash_ctx);	// 初期化
	Phmd5Process(&hash_ctx, buf, 16384);	// MD5 計算
	if (s_blk != NULL){
		for (off = 0; off < 16384; off += block_size){
			Phmd5Begin(&block_ctx);
			if (off + block_size <= 16384){
				crc = crc_update(0, buf + off, block_size);
				Phmd5Process(&block_ctx, buf + off, block_size);
				Phmd5End(&block_ctx);
				if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
					return comp_num;	// スライスの CRC-32 か MD5 が異なる
				comp_num++;
				find_next++;
			} else {	// スライスが途中までなら
				crc = crc_update(0, buf + off, 16384 - off);
				Phmd5Process(&block_ctx, buf + off, 16384 - off);
				block_left = block_size - 16384 + off;
			}
		}
	}

	// ファイル・アクセス用スレッドの準備をする
	th->size = 0;
	th->run = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
	if (th->run == NULL)
		return comp_num;
	th->end = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (th->end == NULL){
		CloseHandle(th->run);
		return comp_num;
	}
	th->buf = (unsigned char *)&hash_ctx;
	th->hFile = hFile;
	hSub = (HANDLE)_beginthreadex(NULL, 65536, thread_read, (LPVOID)th, 0, NULL);
	if (hSub == NULL){
		CloseHandle(th->run);
		CloseHandle(th->end);
		return comp_num;
	}

	// 最初の分を読み込む
	read_size = IO_SIZE;
	if (file_left < IO_SIZE)
		read_size = (unsigned int)file_left;
#ifdef TIMER
time_start = GetTickCount();
#endif
	if (!ReadFile(hFile, buf1, read_size, &len, NULL) || (read_size != len))
		goto error_end;
#ifdef TIMER
time2_total += GetTickCount() - time_start;
#endif
	if (cpu_num > 1)
		Phmd5Process(&hash_ctx, buf1, read_size);	// MD5 計算

	while (file_left > 0){
		len = read_size;	// 読み込んだサイズ
		file_left -= read_size;

		// 前回の読み込みが終わるのを待つ
		WaitForSingleObject(th->end, INFINITE);
		if (th->size < 0)	// 前回の読み込みが失敗していれば
			goto error_end;
		// 次の分を読み込み開始しておく
		if (file_left > 0){
			ResetEvent(th->end);	// リセットしておく
			read_size = IO_SIZE;
			if (file_left < IO_SIZE)
				read_size = (unsigned int)file_left;
			th->buf = buf;
			th->size = read_size;
			SetEvent(th->run);	// サブ・スレッドに読み込みを開始させる
		}
		// バッファーを入れ替える
		if (buf == buf1){
			buf = buf1 + IO_SIZE;
		} else {
			buf = buf1;
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		if (cpu_num == 1)
			Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
		if (s_blk != NULL){
			off = 0;
			if (block_left > 0){	// 前回足りなかった分を追加する
				if (block_left <= len){
					crc = crc_update(crc, buf, block_left);
					Phmd5Process(&block_ctx, buf, block_left);
					Phmd5End(&block_ctx);
					if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
						goto error_end;	// スライスの CRC-32 か MD5 が異なる
					comp_num++;
					find_next++;
					off = block_left;
					block_left = 0;
				} else {
					crc = crc_update(crc, buf, len);
					Phmd5Process(&block_ctx, buf, len);
					off = len;
					block_left -= len;
				}
			}
			for (; off < len; off += block_size){
				Phmd5Begin(&block_ctx);
				if (off + block_size <= len){
					// s_blk[].crc は初期値と最終処理の 0xFFFFFFFF を取り除いてる
					crc = crc_update(0, buf + off, block_size);
					Phmd5Process(&block_ctx, buf + off, block_size);
					Phmd5End(&block_ctx);
					if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
						goto error_end;	// スライスの CRC-32 か MD5 が異なる
					comp_num++;
					find_next++;
				} else {	// スライスが途中までなら
					crc = crc_update(0, buf + off, len - off);
					Phmd5Process(&block_ctx, buf + off, len - off);
					block_left = block_size - len + off;
				}
			}
		}
#ifdef TIMER
time4_total += GetTickCount() - time_start;
#endif

		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (comp_num <= prog_min){	// 最低値以下なら検出スライス数の合計は表示しない
				len = -1;
			} else {
				len = first_num + comp_num - prog_min;
			}
			if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), len, file_name)){
				comp_num = -2;
				goto error_end;
			}
			time_last = GetTickCount();
		}
	}
	Phmd5End(&hash_ctx);	// 最終処理

	if ((s_blk != NULL) && (block_left > 0)){	// 末尾の半端なスライスがあるなら
		crc = crc_update_zero(crc, block_left);
		Phmd5ProcessZero(&block_ctx, block_left);
		Phmd5End(&block_ctx);
		if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
			goto error_end;	// スライスの CRC-32 か MD5 が異なる
		comp_num++;
	}

	if (memcmp(hash_ctx.hash, files[num].hash, 16) == 0)	// ファイル全域の MD5 が同じ
		comp_num = -3;

error_end:
	if (hSub){
		WaitForSingleObject(th->end, INFINITE);	// 動作中なら待つ
		th->size = 0;	// 終了指示
		SetEvent(th->run);
		WaitForSingleObject(hSub, INFINITE);
		CloseHandle(hSub);
	}

#ifdef TIMER
time_total += GetTickCount() - time1_start;
	printf("\nread  %d.%03d sec\n", time2_total / 1000, time2_total % 1000);
	printf("hash  %d.%03d sec\n", time4_total / 1000, time4_total % 1000);
	printf("total %d.%03d sec\n", time_total / 1000, time_total % 1000);
#endif
	return comp_num;
}

// キャッシュ無しでファイルのハッシュ値が同じか調べる
// 0〜=破損してるが何個目まで同じ, -1=MD5-16kが異なる, -2=キャンセル, -3=同じ(完全か追加), -4=存在しない
int file_hash_direct(
	int num,				// file_ctx におけるファイル番号
	wchar_t *file_path,		// ハッシュ値を求めるファイル
	wchar_t *file_name,		// 表示するファイル名
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk)	// 各ソース・ブロックの情報 (NULL なら比較しない)
{
	unsigned char *buf, *buf1 = NULL;
	int find_next, comp_num = 0;
	unsigned int len, off, crc, block_left = 0, read_size;
	unsigned int time_last;
	__int64 file_size, file_left, file_off;
	PHMD5 hash_ctx, block_ctx;
	HANDLE hFile, hSub;
	OVERLAPPED ol;
	HASH_TH th[1];
#ifdef TIMER
time1_start = GetTickCount();
#endif

	prog_last = -1;	// 検証中のファイル名を毎回表示する
	time_last = GetTickCount();
	memset(&ol, 0, sizeof(OVERLAPPED));
	hSub = NULL;
	file_size = files[num].size;	// 本来のファイル・サイズ
	find_next = files[num].b_off;	// 先頭ブロックの番号

	// ソース・ファイルを開く
	hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
						FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		if (GetLastError() == 2)	// file not found (2)
			return -4;
		//printf("cannot set FILE_FLAG_NO_BUFFERING %d\n", GetLastError());
		// ディスク・キャッシュを無効にできない場合
		hFile = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
						FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
			return -4;
	}

	// バッファーの境界をセクター・サイズの倍数にする
	// IO_SIZE は 16KB 以上で 4KB の倍数にすること
	buf1 = _aligned_malloc(IO_SIZE * 2, 4096);
	if (buf1 == NULL){
		comp_num = -1;
		goto error_end;
	}
	buf = buf1 + IO_SIZE;

	// 非同期ファイル・アクセスの準備をする
	ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ol.hEvent == NULL){
		comp_num = -1;
		goto error_end;
	}

	// まずは先頭 16KB のハッシュ値を比較する
	ol.Offset = 0;
	ol.OffsetHigh = 0;
	file_off = 16384;
	if (file_size < 16384){
		len = (unsigned int)file_size;
		read_size = (len + 4095) & ~4095;	// 4KB の倍数にする
		file_left = 0;
	} else {
		len = 16384;
		read_size = 16384;
		file_left = file_size - 16384;	// 本来のファイル・サイズまでしか検査しない
	}
#ifdef TIMER
time_start = GetTickCount();
#endif
	off = ReadFile(hFile, buf, read_size, NULL, &ol);
	if ((off == 0) && (GetLastError() != ERROR_IO_PENDING)){
		comp_num = -1;
		goto error_end;
	}
	WaitForSingleObject(ol.hEvent, INFINITE);
#ifdef TIMER
time6_total += GetTickCount() - time_start;
#endif

	Phmd5Begin(&hash_ctx);	// 初期化
	Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
	Phmd5End(&hash_ctx);	// 最終処理
	if (file_left == 0){	// ファイルは 16KB 以下
		if (s_blk != NULL){	// 全てのスライスを比較する
			for (off = 0; off < len; off += block_size){
				Phmd5Begin(&block_ctx);
				if (off + block_size <= len){
					crc = crc_update(0, buf + off, block_size);
					Phmd5Process(&block_ctx, buf + off, block_size);
				} else {	// 末尾の半端なスライスなら
					crc = crc_update(0, buf + off, len - off);
					Phmd5Process(&block_ctx, buf + off, len - off);
					block_left = block_size - len + off;	// ブロック・サイズまで 0で埋めて計算する
					crc = crc_update_zero(crc, block_left);
					Phmd5ProcessZero(&block_ctx, block_left);
				}
				Phmd5End(&block_ctx);
				if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
					return comp_num;	// スライスの CRC-32 か MD5 が異なる
				comp_num++;
				find_next++;
			}
		}
		if (memcmp(hash_ctx.hash, files[num].hash + 16, 16) != 0){	// 16k MD5 が異なる
			comp_num = -1;
			goto error_end;
		}
		comp_num = -3;	// 同じ
		goto error_end;
	}
	// 16KB よりも大きくて 16k MD5 が異なる場合は、スライス単位の検査をしない
	if (memcmp(hash_ctx.hash, files[num].hash + 16, 16) != 0){
		comp_num = -1;
		goto error_end;
	}

	// 16k MD5 が一致したら残りの部分も計算する
	Phmd5Begin(&hash_ctx);	// 初期化
	Phmd5Process(&hash_ctx, buf, 16384);	// MD5 計算
	if (s_blk != NULL){
		for (off = 0; off < 16384; off += block_size){
			Phmd5Begin(&block_ctx);
			if (off + block_size <= 16384){
				crc = crc_update(0, buf + off, block_size);
				Phmd5Process(&block_ctx, buf + off, block_size);
				Phmd5End(&block_ctx);
				if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
					return comp_num;	// スライスの CRC-32 か MD5 が異なる
				comp_num++;
				find_next++;
			} else {	// スライスが途中までなら
				crc = crc_update(0, buf + off, 16384 - off);
				Phmd5Process(&block_ctx, buf + off, 16384 - off);
				block_left = block_size - 16384 + off;
			}
		}
	}

	// 次の分を読み込む
	ol.Offset = (unsigned int)file_off;
	ol.OffsetHigh = (unsigned int)(file_off >> 32);
	file_off += IO_SIZE;
	read_size = IO_SIZE;
	if (file_left < IO_SIZE){
		read_size = (unsigned int)file_left;
		read_size = (read_size + 4095) & ~4095;	// 4KB の倍数にする
	}
#ifdef TIMER
time_start = GetTickCount();
#endif
	off = ReadFile(hFile, buf1, read_size, NULL, &ol);
	if ((off == 0) && (GetLastError() != ERROR_IO_PENDING))
		goto error_end;
#ifdef TIMER
time6_total += GetTickCount() - time_start;
#endif

	if (cpu_num > 1){	// ハッシュ値計算用スレッドの準備をする
		th->run = CreateEvent(NULL, FALSE, FALSE, NULL);	// Auto Reset にする
		if (th->run == NULL)
			goto error_end;
		th->end = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (th->end == NULL){
			CloseHandle(th->run);
			goto error_end;
		}
		th->buf = (unsigned char *)&hash_ctx;
		hSub = (HANDLE)_beginthreadex(NULL, 65536, thread_hash, (LPVOID)th, 0, NULL);
		if (hSub == NULL){
			CloseHandle(th->run);
			CloseHandle(th->end);
			goto error_end;
		}
		// IO が延滞しないように、サブ・スレッドの優先度を落とす
		SetThreadPriority(hSub, THREAD_PRIORITY_BELOW_NORMAL);
	}

	while (file_left > 0){
		len = IO_SIZE;	// 読み込んだサイズ
		if (file_left < IO_SIZE)
			len = (unsigned int)file_left;
		file_left -= len;

		// 前回の読み込みが終わるのを待つ
		if (hSub)
			WaitForSingleObject(th->end, INFINITE);
		WaitForSingleObject(ol.hEvent, INFINITE);

		// 次の分を読み込み開始しておく
		if (file_left > 0){
			ol.Offset = (unsigned int)file_off;
			ol.OffsetHigh = (unsigned int)(file_off >> 32);
			file_off += IO_SIZE;
			read_size = IO_SIZE;
			if (file_left < IO_SIZE){
				read_size = (unsigned int)file_left;
				read_size = (read_size + 4095) & ~4095;	// 4KB の倍数にする
			}
#ifdef TIMER
time_start = GetTickCount();
#endif
			off = ReadFile(hFile, buf, read_size, NULL, &ol);
			if ((off == 0) && (GetLastError() != ERROR_IO_PENDING))
				goto error_end;
#ifdef TIMER
time6_total += GetTickCount() - time_start;
#endif
		}
		// バッファーを入れ替える
		if (buf == buf1){
			buf = buf1 + IO_SIZE;
		} else {
			buf = buf1;
		}

#ifdef TIMER
time_start = GetTickCount();
#endif
		if (hSub){	// サブ・スレッドで計算を開始する
			ResetEvent(th->end);	// リセットしておく
			th->buf = buf;
			th->size = len;
			SetEvent(th->run);
		} else {
			Phmd5Process(&hash_ctx, buf, len);	// MD5 計算
		}
		if (s_blk != NULL){
			off = 0;
			if (block_left > 0){	// 前回足りなかった分を追加する
				if (block_left <= len){
					crc = crc_update(crc, buf, block_left);
					Phmd5Process(&block_ctx, buf, block_left);
					Phmd5End(&block_ctx);
					if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
						goto error_end;	// スライスの CRC-32 か MD5 が異なる
					comp_num++;
					find_next++;
					off = block_left;
					block_left = 0;
				} else {
					crc = crc_update(crc, buf, len);
					Phmd5Process(&block_ctx, buf, len);
					off = len;
					block_left -= len;
				}
			}
			for (; off < len; off += block_size){
				Phmd5Begin(&block_ctx);
				if (off + block_size <= len){
					crc = crc_update(0, buf + off, block_size);
					Phmd5Process(&block_ctx, buf + off, block_size);
					Phmd5End(&block_ctx);
					if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
						goto error_end;	// スライスの CRC-32 か MD5 が異なる
					comp_num++;
					find_next++;
				} else {	// スライスが途中までなら
					crc = crc_update(0, buf + off, len - off);
					Phmd5Process(&block_ctx, buf + off, len - off);
					block_left = block_size - len + off;
				}
			}
		}
#ifdef TIMER
time7_total += GetTickCount() - time_start;
#endif

		// 経過表示
		if (GetTickCount() - time_last >= UPDATE_TIME){
			if (print_progress_file((int)(((file_size - file_left) * 1000) / file_size), -1, file_name)){
				comp_num = -2;
				goto error_end;
			}
			time_last = GetTickCount();
		}
	}

	if ((s_blk != NULL) && (block_left > 0)){	// 末尾の半端なスライスがあるなら
		crc = crc_update_zero(crc, block_left);
		Phmd5ProcessZero(&block_ctx, block_left);
		Phmd5End(&block_ctx);
		if ((crc != s_blk[find_next].crc) || (memcmp(block_ctx.hash, s_blk[find_next].hash, 16) != 0))
			goto error_end;	// スライスの CRC-32 か MD5 が異なる
		comp_num++;
	}

	if (hSub)	// サブ・スレッドの計算終了を待つ
		WaitForSingleObject(th->end, INFINITE);
	Phmd5End(&hash_ctx);	// 最終処理
	if (memcmp(hash_ctx.hash, files[num].hash, 16) == 0)	// ファイル全域の MD5 が同じ
		comp_num = -3;

error_end:
	CancelIo(hFile);	// 非同期 IO を取り消す
	CloseHandle(hFile);
	if (ol.hEvent)
		CloseHandle(ol.hEvent);
	if (hSub){
		WaitForSingleObject(th->end, INFINITE);	// 動作中なら待つ
		th->size = 0;	// 終了指示
		SetEvent(th->run);
		WaitForSingleObject(hSub, INFINITE);
		CloseHandle(hSub);
	}
	if (buf1)
		_aligned_free(buf1);

#ifdef TIMER
time5_total += GetTickCount() - time1_start;
	printf("\nread  %d.%03d sec\n", time6_total / 1000, time6_total % 1000);
	printf("hash  %d.%03d sec\n", time7_total / 1000, time7_total % 1000);
	printf("total %d.%03d sec\n", time5_total / 1000, time5_total % 1000);
#endif
	return comp_num;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// CRC-32 を使って 1バイト内のバースト・エラーを訂正する
// -1=エラー訂正できず, 0=エラー無し, 1=エラー訂正できた
// 全て 0 のデータの判定は他で行うこと
int correct_error(
	unsigned char *data_in,		// ハッシュ値を求めたバイト配列
	unsigned int data_len,		// 入力バイト数
	unsigned char *hash,		// ブロック・サイズ分の本来のハッシュ値 (16バイト, MD5)
	unsigned int crc,			// 入力バイト数分の本来の CRC-32
	unsigned int *error_off,	// エラー開始位置
	unsigned char *error_mag)	// エラー内容、XORでエラー訂正を取り消せる
{
	unsigned char data_hash[16];
	unsigned int data_crc, pos;

	// まずエラーが存在するかを調べる
	data_crc = crc_update(0, data_in, data_len);
	if (data_crc == crc){
		// MD5 はブロック・サイズ分を計算する
		data_md5_block(data_in, data_len, data_hash);
		if (memcmp(data_hash, hash, 16) == 0)
			return 0;	// エラー無し
		return -1;	// CRC-32 でエラーを検出できない場合は訂正もできない
	}
	if (data_len > 0x100000)
		return -1;	// 1MB 以上ならエラー訂正しない
	// CRC-32 で 1バイト訂正を試みると、失敗する確率はエラー位置ごとに 1 / 2^24 ぐらい？
	// データ・サイズが 1MB (1^20) だと、訂正できなくても 1/16 の確率で MD5 による検算が必要

	// エラー位置と程度を計算する
	crc ^= data_crc;	// CRC-32 の差にする
	for (pos = 1; pos <= data_len; pos++){
		// 1 バイトずつ遡りながら最初のエラー発生位置を探す
		crc = reverse_table[(crc >> 24)] ^ (crc << 8);

		// 一定範囲内のエラーを訂正する
		if ((crc & 0xFFFFFF00) == 0){
			// 試しにエラーを訂正してみる
			*error_mag = (unsigned char)crc;
			data_in[data_len - pos] ^= *error_mag;	// エラーが発生した 1バイト目

			// 正しく訂正できてるかを確認する
			data_md5_block(data_in, data_len, data_hash);
			if (memcmp(data_hash, hash, 16) == 0){
				//printf("position = %d byte from end, magnitude = 0x%08X\n", pos - 1, crc);
				*error_off = data_len - pos;
				return 1;	// エラー訂正できた
			}
			// 訂正失敗ならデータを元に戻しておく
			//printf("MD5 found errors\n");
			data_in[data_len - pos] ^= *error_mag;
		}
	}

	return -1;	// 訂正できず
}

// CRC-32 を使って 1バイト内のバースト・エラーを訂正する
// -1=エラー訂正できず, 0=エラー無し, 1=エラー訂正できた
// CRC-32 は既に計算済み、エラー無しも比較済み
int correct_error2(
	unsigned char *data_in,		// ハッシュ値を求めたバイト配列
	unsigned int data_len,		// 入力バイト数
	unsigned int data_crc,		// 入力バイト数分の CRC-32
	unsigned char *hash,		// ブロック・サイズ分の本来のハッシュ値 (16バイト, MD5)
	unsigned int crc,			// 入力バイト数分の本来の CRC-32
	unsigned int *error_off,	// エラー開始位置
	unsigned char *error_mag)	// エラー内容、XORでエラー訂正を取り消せる
{
	unsigned char data_hash[16];
	unsigned int pos;

	if (data_len > 0x100000)
		return -1;	// 1MB 以上ならエラー訂正しない

	// エラー位置と程度を計算する
	crc ^= data_crc;	// CRC-32 の差にする
	for (pos = 1; pos <= data_len; pos++){
		// 1 バイトずつ遡りながら最初のエラー発生位置を探す
		crc = reverse_table[(crc >> 24)] ^ (crc << 8);

		// 一定範囲内のエラーを訂正する
		if ((crc & 0xFFFFFF00) == 0){
			// 試しにエラーを訂正してみる
			*error_mag = (unsigned char)crc;
			data_in[data_len - pos] ^= *error_mag;	// エラーが発生した 1バイト目

			// 正しく訂正できてるかを確認する
			data_md5_block(data_in, data_len, data_hash);
			if (memcmp(data_hash, hash, 16) == 0){
				//printf("position = %d byte from end, magnitude = 0x%08X\n", pos - 1, crc);
				*error_off = data_len - pos;
				return 1;	// エラー訂正できた
			}
			// 訂正失敗ならデータを元に戻しておく
			//printf("MD5 found errors\n");
			data_in[data_len - pos] ^= *error_mag;
		}
	}

	return -1;	// 訂正できず
}

/*
// MD5 と CRC32 から 8バイトのデータを逆算するための関数
void data_reverse8(
	unsigned char *data_out,	// 不明な入力データ (8バイト)
	unsigned char *hash,		// ハッシュ値 (16バイト)
	unsigned char *crc)			// CRC (4バイト)
{
	unsigned int i, rv, crc1, crc2, data[16], state[4], time;
	// crc = 0xffffffff ^ data[0〜3];	// 初期値と最初のデータを XOR する
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc ^= data[4〜7];	// 結果と次のデータを XOR する
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc = table[crc & 0xff] ^ (crc >> 8);
	// crc ^= 0xffffffff; 最終処理

time = GetTickCount();

	memcpy(&crc2, crc, 4);
	crc2 ^= 0xffffffff;	// 最終処理を取り消す
	rv = CrcReverse4(crc2);	// 2回目のCRC計算を取り消した値 = crc ^ data[4〜7]
	// 1回目のCRC計算の結果がわからないので、入力データもわからない (XORした値だけわかってる)
	// そこで、入力データを変化させながら1回目のCRCを逆算して、候補をMD5で確認する
	for (i = 0; i < 16; i++)
		data[i] = 0;
	data[2] = 0x00000080;	// データ終端の 1bit
	data[14] = 0x00000040;	// データ・サイズ 8byte = 64bit
	do {
		crc1 = rv ^ data[1];
		data[0] = CrcReverse4(crc1) ^ 0xffffffff;	// 逆算して初期化処理を取り消す
		if ((data[1] & 0x00ffffff) == 0)
			printf("%08x%08x\n", data[0], data[1]);
		// このデータで合ってるかをMD5で調べる
		state[0] = 0x67452301;
		state[1] = 0xefcdab89;
		state[2] = 0x98badcfe;
		state[3] = 0x10325476;
		MD5Transform(data, state);

		if (memcmp(hash, state, 16) == 0)
			break;
		data[1]++;
	} while (data[1] != 0);

	memcpy(data_out, (unsigned char *)data, 8);
// Pentium 3 800MHz だと完全走査に約50分かかる、テキストなら20分ぐらい?
time = (GetTickCount() - time);
printf("\n%3u.%03u sec\n", time / 1000, time % 1000);
}
*/

