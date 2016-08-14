#ifndef _LIST_H_
#define _LIST_H_

#ifdef __cplusplus
extern "C" {
#endif


// ソース・ファイルを検査せずに一覧を表示する
long list_file_data(
	char *ascii_buf,		// 作業用
	file_ctx_r *files);		// 各ソース・ファイルの情報

// ソース・ファイルが完全かどうかを一覧表示する
long check_file_complete(
	char *ascii_buf,
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

// ソース・ファイルが不完全なら別名・移動ファイルを探す
long search_misnamed_file(
	char *ascii_buf,
	wchar_t *file_path,		// 作業用
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 破損・分割・類似名のファイルから使えるスライスを探す
long search_file_slice(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	long switch_v,			// 検査レベル 0=詳細検査, 1=簡易検査, +16=検出後に保存
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報


#ifdef __cplusplus
}
#endif

#endif
