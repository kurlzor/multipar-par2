#ifndef _SEARCH_H_
#define _SEARCH_H_

#ifdef __cplusplus
extern "C" {
#endif


// リカバリ・ファイルを検索してファイル・リストに追加する
int search_recovery_files(int check_all);

// Main packet を末尾から遡って探す
int search_main_packet(
	unsigned char *buf,		// 作業バッファー、File ID が戻る
	unsigned char *set_id,	// Recovery Set ID が戻る
	int switch_v);			// 検査レベル 0=全領域, +1=後半分だけ, +2=全てのリカバリ・ファイル

// ファイル情報のパケットを探す
int search_file_packet(
	char *ascii_buf,
	unsigned char *buf,		// 作業バッファー
	wchar_t *par_commentU,	// Unicode コメントを入れる
	unsigned char *set_id,	// Recovery Set ID を確かめる
	int switch_v,			// 検査レベル 0=全て検査する, 1=巨大パケットはとばす, +32=ファイル名を浄化する
	file_ctx_r *files);		// 各ソース・ファイルの情報

// 修復用のパケットを探す
int search_recovery_packet(
	char *ascii_buf,
	unsigned char *buf,		// 作業バッファー
	wchar_t *uni_buf,
	unsigned char *set_id,	// Recovery Set ID を確かめる
	HANDLE *rcv_hFile,		// 各リカバリ・ファイルのハンドル (verify なら NULL)
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	parity_ctx_r *p_blk);	// 各パリティ・ブロックの情報


#ifdef __cplusplus
}
#endif

#endif
