#ifndef _INI_H_
#define _INI_H_

#ifdef __cplusplus
extern "C" {
#endif


#define INI_NAME_LEN	38		// 検査結果ファイルのファイル名の文字数

extern long recent_data;

long check_ini_file(unsigned char *set_id, wchar_t *recv_buf);
void close_ini_file(void);
void write_ini_file2(unsigned char *par_client, wchar_t *par_comment);
void write_ini_file(file_ctx_r *files);
long read_ini_file(wchar_t *uni_buf, file_ctx_r *files);

long check_ini_recovery(
	HANDLE hFile,			// リカバリ・ファイルのハンドル
	unsigned long meta[7]);	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号

void write_ini_recovery(long id, __int64 off);

void write_ini_recovery2(
	long packet_count,		// そのリカバリ・ファイル内に含まれるパケットの数
	long block_count,		// そのリカバリ・ファイル内に含まれるパリティ・ブロックの数
	long bad_flag,
	unsigned long meta[7]);	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号

long read_ini_recovery(
	long num,
	long *packet_count,		// そのリカバリ・ファイル内に含まれるパケットの数
	long *block_count,		// そのリカバリ・ファイル内に含まれるパリティ・ブロックの数
	long *bad_flag,
	parity_ctx_r *p_blk);	// 各パリティ・ブロックの情報

// Input File Slice Checksum を書き込む
void write_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// Input File Slice Checksum を読み込む
long read_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// Input File Slice Checksum が不完全でも読み書きする
void update_ini_checksum(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

void write_ini_state(
	long num,				// ファイル番号
	unsigned long meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	long result);			// 検査結果 0〜=何ブロック目まで一致, -3=完全に一致

long check_ini_state(
	long num,				// ファイル番号
	unsigned long meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	HANDLE hFile);			// そのファイルのハンドル

void write_ini_complete(
	long num,				// ファイル番号
	wchar_t *file_path);	// ソース・ファイルの絶対パス

long check_ini_verify(
	wchar_t *file_name,		// 表示するファイル名
	HANDLE hFile,			// ファイルのハンドル
	long num1,				// チェックサムを比較したファイルの番号
	unsigned long meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	long switch_v,			// 検査方法、0=標準, 1=簡易, +16=検出後に保存
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);			// スライス検査用の情報

void write_ini_verify(long id, long flag, __int64 off);

void write_ini_verify2(
	long num1,				// チェックサムを比較したファイルの番号
	unsigned long meta[7],	// サイズ、作成日時、更新日時、ボリューム番号、オブジェクト番号
	long switch_v,			// 検査方法、0=標準, 1=簡易
	long max);				// 記録したブロック数

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 作業用のテンポラリ・ファイルを開く
long open_temp_file(
	wchar_t *temp_path,		// 作業用、基準ディレクトリが入ってる
	long num,				// 見つけたスライスが属するファイル番号
	file_ctx_r *files,		// 各ソース・ファイルの情報
	slice_ctx *sc);


#ifdef __cplusplus
}
#endif

#endif
