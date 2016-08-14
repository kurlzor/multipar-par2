#ifndef _RS_ENCODE_H_
#define _RS_ENCODE_H_

#ifdef __cplusplus
extern "C" {
#endif


int encode_method1(	// ソース・ブロックが一個だけの場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk);		// パリティ・ブロックの情報

int encode_method2(	// ソース・データを全て読み込む場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant);

int encode_method3(	// パリティ・ブロックを全て保持できる場合
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant);

int encode_method4(	// 全てのブロックを断片的に保持する場合 (GPU対応)
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant);	// 複数ブロック分の領域を確保しておく？

int encode_method5(	// パリティ・ブロックだけ保持する場合 (GPU対応)
	wchar_t *file_path,
	unsigned char *header_buf,	// Recovery Slice packet のパケット・ヘッダー
	HANDLE *rcv_hFile,			// リカバリ・ファイルのハンドル
	file_ctx_c *files,			// ソース・ファイルの情報
	source_ctx_c *s_blk,		// ソース・ブロックの情報
	parity_ctx_c *p_blk,		// パリティ・ブロックの情報
	unsigned short *constant);


#ifdef __cplusplus
}
#endif

#endif
