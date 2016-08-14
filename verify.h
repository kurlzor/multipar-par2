#ifndef _VERIFY_H_
#define _VERIFY_H_

#ifdef __cplusplus
extern "C" {
#endif


// スライス検査の準備をする
long init_verification(
	long switch_v,			// 検査方法、0=標準, 1=簡易
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ソース・ファイルのスライスを探す (分割ファイルも)
long check_file_slice(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	long num,				// file_ctx におけるファイル番号
	long switch_v,			// 検査レベル +16=検出後に保存
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);			// スライス検査用の情報

// 分割ファイルと類似名ファイルに含まれるスライスを探す
long search_file_split(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	long num,				// file_ctx におけるファイル番号
	long switch_v,			// 検査レベル +16=検出後に保存
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);			// スライス検査用の情報

// 指定された外部ファイルを検査する
long check_external_file(
	char *ascii_buf,		// 作業用
	wchar_t *file_path,		// 作業用
	long switch_v,			// 検査レベル +16=検出後に保存
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);			// スライス検査用の情報

// 基準ディレクトリ内を検索して、名前が異なってるソース・ファイルを探す
long search_additional_file(
	char *ascii_buf,		// 作業用
	wchar_t *find_path,		// 作業用
	long switch_v,			// 検査レベル +16=検出後に保存
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk,	// 各ソース・ブロックの情報
	slice_ctx *sc);			// スライス検査用の情報

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// スライスを逆算するか、共通してるスライスを探す
long search_calculable_slice(
	file_ctx_r *files,		// 各ソース・ファイルの情報
	source_ctx_r *s_blk);	// 各ソース・ブロックの情報


#ifdef __cplusplus
}
#endif

#endif
