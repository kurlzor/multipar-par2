#ifndef _REEDSOLOMON_H_
#define _REEDSOLOMON_H_

#ifdef __cplusplus
extern "C" {
#endif


//#define TIMER // 実験用

// Read all source & Keep some parity 方式
// 部分的なエンコードを行う最低ブロック数
#define PART_MAX_RATE	1	// ソース・ブロック数の 1/2  = 50%
#define PART_MIN_RATE	5	// ソース・ブロック数の 1/32 = 3.1%
#define PART_MIN_NUM	8	// ダブル・バッファー用に 2個以上必要

// Read some source & Keep all parity 方式
// 一度に読み込む最少ブロック数
#define READ_MIN_RATE	1	// 保持するブロック数の 1/2 = 50%
#define READ_MIN_NUM	16

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// Cache Blocking を試みる
int try_cache_blocking(int unit_size);

// 空きメモリー量からファイル・アクセスのバッファー・サイズを計算する
unsigned int get_io_size(
	unsigned int buf_num,	// 何ブロック分の領域を確保するのか
	unsigned int *part_num,	// 部分的なエンコード用の作業領域
	size_t trial_alloc);	// 確保できるか確認するのか

// GPU 用に unit_size を MEM_UNIT の倍数にする
unsigned int get_io_size_gpu(
	unsigned int buf_num,	// 何ブロック分の領域を確保するのか
	size_t trial_alloc);	// 確保できるか確認するのか

// 何ブロックまとめてファイルから読み込むかを空きメモリー量から計算する
int read_block_num(
	int keep_num,			// 保持するパリティ・ブロック数
	int add_num,			// 余裕を見るブロック数
	size_t trial_alloc);	// 確保できるか確認するのか

int read_block_num_gpu(		// GPU 用
	int keep_num,			// 保持するパリティ・ブロック数
	int add_num,			// 余裕を見るブロック数
	size_t trial_alloc);	// 確保できるか確認するのか

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// リード・ソロモン符号を使ってエンコードする
int rs_encode(
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk);		// パリティ・ブロックの情報

// リード・ソロモン符号を使ってデコードする
int rs_decode(
	wchar_t *file_path,
	int block_lost,				// 失われたソース・ブロックの数
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_r *files,			// ソース・ファイルの情報
	source_ctx_r *s_blk,		// ソース・ブロックの情報
	parity_ctx_r *p_blk);		// パリティ・ブロックの情報


#ifdef __cplusplus
}
#endif

#endif
