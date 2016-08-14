#ifndef _CREATE_H_
#define _CREATE_H_

#ifdef __cplusplus
extern "C" {
#endif


// PAR 2.0 のパケット・ヘッダーを作成する
void set_packet_header(
	unsigned char *buf,			// ヘッダーを格納する 64バイトのバッファー
	unsigned char *set_id,		// Recovery Set ID、16バイト
	int type_num,				// そのパケットの型番号
	unsigned int body_size);	// パケットのデータ・サイズ

// ソース・ファイルの情報を集める
int get_source_files(file_ctx_c *files);

// 共通パケットを作成する
int set_common_packet(
	unsigned char *buf,
	int *packet_num,			// 共通パケットの数
	int switch_u,				// ユニコードのファイル名も記録する
	file_ctx_c *files);

// ソース・ファイルの情報から共通パケットのサイズを計算する
int measure_common_packet(
	int *packet_num,			// 共通パケットの数
	int switch_u);				// ユニコードのファイル名も記録する

// 末尾パケットを作成する
int set_footer_packet(
	unsigned char *buf,
	wchar_t *par_comment,		// コメント
	unsigned char *set_id);		// Recovery Set ID

// 末尾パケットのサイズを計算する
int measure_footer_packet(
	wchar_t *par_comment);		// コメント

// リカバリ・ファイルを作成して共通パケットをコピーする
int create_recovery_file(
	wchar_t *recovery_path,		// 作業用
	int packet_limit,			// リカバリ・ファイルのパケット繰り返しの制限
	int block_distri,			// パリティ・ブロックの分配方法
	int packet_num,				// 共通パケットの数
	unsigned char *common_buf,	// 共通パケットのバッファー
	int common_size,			// 共通パケットのバッファー・サイズ
	unsigned char *footer_buf,	// 末尾パケットのバッファー
	int footer_size,			// 末尾パケットのバッファー・サイズ
	HANDLE *rcv_hFile,			// 各リカバリ・ファイルのハンドル
	parity_ctx_c *p_blk);		// 各パリティ・ブロックの情報

// 作成中のリカバリ・ファイルを削除する
void delete_recovery_file(
	wchar_t *recovery_path,		// 作業用
	int block_distri,			// パリティ・ブロックの分配方法
	int switch_p,				// インデックス・ファイルを作らない
	HANDLE *rcv_hFile);			// 各リカバリ・ファイルのハンドル

// リカバリ・ファイルのサイズを計算する
void measure_recovery_file(
	wchar_t *recovery_path,		// 作業用 (最初はコメントが入ってる)
	int packet_limit,			// リカバリ・ファイルのパケット繰り返しの制限
	int block_distri,			// パリティ・ブロックの分配方法
	int packet_num,				// 共通パケットの数
	int common_size,			// 共通パケットのバッファー・サイズ
	int footer_size,			// 末尾パケットのバッファー・サイズ
	int switch_p);				// インデックス・ファイルを作らない

// ソース・ファイルを分割する
int split_files(
	file_ctx_c *files,
	int *cur_num, int *cur_id);	// エラー発生時は、その時点でのファイル番号と分割番号が戻る

// 分割されたソース・ファイルを削除する
void delete_split_files(
	file_ctx_c *files,
	int max_num, int max_id);	// エラー発生時のファイル番号と分割番号


#ifdef __cplusplus
}
#endif

#endif
