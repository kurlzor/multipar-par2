// common2.c
// Copyright : 2016-06-27 Yutaka Sawada
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

#include <conio.h>
#include <stdio.h>

#include <windows.h>
#include <imagehlp.h>
#include <shlwapi.h>

#include "common2.h"


// グローバル変数
wchar_t recovery_file[MAX_LEN];	// リカバリ・ファイルのパス
wchar_t base_dir[MAX_LEN];		// ソース・ファイルの基準ディレクトリ
wchar_t ini_path[MAX_LEN];		// 検査結果ファイルのパス

int base_len;		// ソース・ファイルの基準ディレクトリの長さ
int recovery_limit;	// 作成時はリカバリ・ファイルのサイズ制限
int first_num;		// 作成時は最初のパリティ・ブロック番号、検査時は初めて見つけた数
int switch_b;		// バックアップを作るか (作成時はソース・ブロックの割合と基準サイズ)

int file_num;		// ソース・ファイルの数
int entity_num;		// 実体のあるファイルの数 (recovery set に含まれるファイル数)
int recovery_num;	// リカバリ・ファイルの数
int source_num;		// ソース・ブロックの数
int parity_num;		// パリティ・ブロックの数

unsigned int block_size;	// ブロック・サイズ
unsigned int split_size;	// 分割サイズ
__int64 total_file_size;	// 合計ファイル・サイズ

// 可変長サイズの領域にファイル名を記録する
wchar_t *list_buf;	// ソース・ファイルのリスト
int list_len;		// ファイル・リストの文字数
int list_max;		// ファイル・リストの最大文字数

wchar_t *recv_buf;	// リカバリ・ファイルのリスト
int recv_len;		// ファイル・リストの文字数

wchar_t *list2_buf;	// 指定された検査対象のファイル名のリスト
int list2_len;
int list2_max;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

unsigned int cp_output;	// Console Output Code Page

// 指定された Code Page から UTF-16 に変換する
int cp_to_utf16(char *in, wchar_t *out, unsigned int cp)
{
	int err = 0;
	unsigned int dwFlags = MB_ERR_INVALID_CHARS;	// 不適切な文字列を警告する

	if (cp == CP_UTF8)
		dwFlags = 0;

	if (MultiByteToWideChar(cp, dwFlags, in, -1, out, MAX_LEN) == 0){
		err = GetLastError();
		if ((dwFlags != 0) && (err == ERROR_INVALID_FLAGS)){
			//printf("MultiByteToWideChar, ERROR_INVALID_FLAGS\n");
			if (MultiByteToWideChar(cp, 0, in, -1, out, MAX_LEN) != 0)
				return 0;
			err = GetLastError();
		}
		out[0] = 0;
		return err;
	}
	return 0;
}

// Windown OS の UTF-16 から指定された Code Page に変換する
int utf16_to_cp(wchar_t *in, char *out, unsigned int cp)
{
	unsigned int dwFlags = WC_NO_BEST_FIT_CHARS;	// 似た文字への自動変換を行わない

	if (cp == CP_UTF8)
		dwFlags = 0;

	if (WideCharToMultiByte(cp, dwFlags, in, -1, out, MAX_LEN * 3, NULL, NULL) == 0){
		if ((dwFlags != 0) && (GetLastError() == ERROR_INVALID_FLAGS)){
			//printf("WideCharToMultiByte, ERROR_INVALID_FLAGS\n");
			if (WideCharToMultiByte(cp, 0, in, -1, out, MAX_LEN * 3, NULL, NULL) != 0)
				return 0;
		}
		strcpy(out, "cannot encode");
		return 1;
	}
	return 0;
}

// Windows OS の UTF-8 から UTF-16 に変換する
void utf8_to_utf16(char *in, wchar_t *out)
{
	// UTF-16 <-> UTF-8 間では文字数以外のエラーは発生しない
	if (MultiByteToWideChar(CP_UTF8, 0, in, -1, out, MAX_LEN) == 0)
		wcscpy(out, L"too long");
}

// Windown OS の UTF-16 から UTF-8 に変換する
void utf16_to_utf8(wchar_t *in, char *out)
{
	// UTF-16 <-> UTF-8 間では文字数以外のエラーは発生しない
	WideCharToMultiByte(CP_UTF8, 0, in, -1, out, MAX_LEN * 3, NULL, NULL);
}

// 文字列が UTF-8 かどうかを判定する (0 = maybe UTF-8)
int check_utf8(unsigned char *text)
{
	unsigned char c1;
	int tail_len = 0;	// UTF8-tail が何バイト続くか

	while (*text != 0){
		c1 = *text;
		// 禁止 0xC0〜0xC1，0xF5〜0xFF
		if ((c1 == 0xC0) || (c1 == 0xC1) || (c1 >= 0xF5))
			return 1;	// 禁止文字
		if (tail_len == 0){	// 第1バイトなら
			// 1バイト文字
			if (c1 <= 0x7F){
				tail_len = 0;
			// 2バイト文字の第1バイト
			} else if ((c1 >= 0xC2) && (c1 <= 0xDF)){
				tail_len = 1;
			// 3バイト文字の第1バイト
			} else if ((c1 >= 0xE0) && (c1 <= 0xEF)){
				tail_len = 2;
			// 4バイト文字の第1バイト
			} else if (c1 >= 0xF0){
				tail_len = 3;
			} else {
				return 2;	//  第1バイトとして不適当な文字
			}
		} else {	// 第2バイト以後なら
			if ((c1 >= 0x80) && (c1 <= 0xBF)){
				tail_len--;
			} else {
				return 2;	// 第2バイト以後として不適当な文字
			}
		}
		text++;	// 次の文字へ
	}
	return 0;
}

// ファイル・パスから、先頭にある "\\?\" を省いて、指定された Code Page に変換する
int path_to_cp(wchar_t *path, char *out, unsigned int cp)
{
	unsigned int dwFlags = WC_NO_BEST_FIT_CHARS;	// 似た文字への自動変換を行わない

	if (cp == CP_UTF8)
		dwFlags = 0;

	if (wcsncmp(path, L"\\\\?\\", 4) == 0){	// "\\?\" を省く
		path += 4;
		if (wcsncmp(path, L"UNC\\", 4) == 0){	// "\\?\UNC" を省いて "\" を追加する
			path += 3;
			*out = '\\';
			out += 1;
		}
	}

	if (WideCharToMultiByte(cp, dwFlags, path, -1, out, MAX_LEN * 3, NULL, NULL) == 0){
		if ((dwFlags != 0) && (GetLastError() == ERROR_INVALID_FLAGS)){
			//printf("WideCharToMultiByte, ERROR_INVALID_FLAGS\n");
			if (WideCharToMultiByte(cp, 0, path, -1, out, MAX_LEN * 3, NULL, NULL) != 0)
				return 0;
		}
		strcpy(out, "cannot encode");
		return 1;
	}
	return 0;
}

// UTF-16 のファイル・パスを画面出力用の Code Page を使って表示する
void printf_cp(unsigned char *format, wchar_t *path)
{
	unsigned char buf[MAX_LEN * 3];
	path_to_cp(path, buf, cp_output);
	printf(format, buf);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイルの offset バイト目から size バイトのデータを buf に読み込む
int file_read_data(
	HANDLE hFileRead,
	__int64 offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv;

	// ファイルの位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// size バイトを読み込む
	if (!ReadFile(hFileRead, buf, size, &rv, NULL)){
		print_win32_err();
		return 1;
	}
	if (size != rv)
		return 1;	// 指定サイズを読み込めなかったらエラーになる

	return 0;
}

// ファイルの offset バイト目に size バイトのデータを buf から書き込む
int file_write_data(
	HANDLE hFileWrite,
	__int64 offset,
	unsigned char *buf,
	unsigned int size)
{
	unsigned int rv;

	// ファイルの位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileWrite, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// size バイトを書き込む
	if (!WriteFile(hFileWrite, buf, size, &rv, NULL)){
		print_win32_err();
		return 1;
	}

	return 0;
}

// ファイルの offset バイト目に size バイトの指定値を書き込む
int file_fill_data(
	HANDLE hFileWrite,
	__int64 offset,
	unsigned char value,
	unsigned int size)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;

	// ファイルの位置を offsetバイト目にする
	if (!SetFilePointerEx(hFileWrite, *((PLARGE_INTEGER)&offset), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// 指定された値で埋める
	memset(buf, value, IO_SIZE);
	while (size){
		len = IO_SIZE;
		if (size < IO_SIZE)
			len = size;
		size -= len;
		if (!WriteFile(hFileWrite, buf, len, &rv, NULL)){
			print_win32_err();
			return 1;
		}
	}

	return 0;
}

// ファイルの指定バイト目から size バイトのデータを別のファイルに書き込む
int file_copy_data(
	HANDLE hFileRead,
	__int64 offset_read,
	HANDLE hFileWrite,
	__int64 offset_write,
	unsigned int size)
{
	unsigned char buf[IO_SIZE];
	unsigned int rv, len;

	// ファイルの位置を offset_read バイト目にする
	if (!SetFilePointerEx(hFileRead, *((PLARGE_INTEGER)&offset_read), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// ファイルの位置を offset_write バイト目にする
	if (!SetFilePointerEx(hFileWrite, *((PLARGE_INTEGER)&offset_write), NULL, FILE_BEGIN)){
		print_win32_err();
		return 1;
	}

	// コピーする
	while (size){
		len = IO_SIZE;
		if (size < IO_SIZE)
			len = size;
		size -= len;
		if (!ReadFile(hFileRead, buf, len, &rv, NULL)){
			print_win32_err();
			return 1;	// 指定サイズを読み込めなくてもエラーにしない
		}
		if (rv < len){
			memset(buf + rv, 0, len - rv);	// 足りなかった分は 0 で埋める
			size = 0;
		}
		if (!WriteFile(hFileWrite, buf, len, &rv, NULL)){
			print_win32_err();
			return 1;
		}
	}

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 修復中のテンポラリ・ファイルの名前を作る
// 末尾に _par.tmp を追加する (ADD_LEN 以下の文字数)
void get_temp_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *temp_path)		// テンポラリ・ファイルのパス
{
	wcscpy(temp_path, file_path);
	wcscat(temp_path, L"_par.tmp");	// 末尾に追加する
}

// 作業用のゼロで埋められたテンポラリ・ファイルを作成する
int create_temp_file(
	wchar_t *file_path,		// ファイルのパス
	__int64 file_size)		// ファイルのサイズ
{
	HANDLE hFile;

	hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE){
		if (GetLastError() == ERROR_PATH_NOT_FOUND){	// Path not found (3)
			make_dir(file_path);	// 途中のフォルダが存在しないのなら作成する
			hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		}
		if (hFile == INVALID_HANDLE_VALUE){
			print_win32_err();
			return 1;
		}
	}

	// 本来のサイズにする
	if (!SetFilePointerEx(hFile, *((PLARGE_INTEGER)&file_size), NULL, FILE_BEGIN)){
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	if (!SetEndOfFile(hFile)){
		print_win32_err();
		CloseHandle(hFile);
		return 1;
	}
	CloseHandle(hFile);

	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイル・パスがファイル・リスト上に既に存在するか調べる
int search_file_path(
	wchar_t *list,			// ファイル・リスト
	int total_len,			// ファイル・リストの文字数
	wchar_t *search_file)	// 検索するファイルのパス
{
	int off = 0;

	while (off < total_len){
		if (_wcsicmp(list + off, search_file) == 0)
			return 1;
		// 次のファイル名の位置へ
		//off += (wcslen(list + off) + 1);
		while (list[off] != 0)
			off++;
		off++;
	}

	return 0;
}

// ファイル・リストの内容を並び替える
void sort_list(
	wchar_t *list,	// ファイル・リスト
	int total_len)	// ファイル・リストの文字数
{
	wchar_t *work_buf;
	int off1 = 0, off2, off3, work_off = 0;

	// 作業バッファーを確保する
	work_buf = (wchar_t *)calloc(total_len, 2);
	if (work_buf == NULL)
		return;	// 並べ替え失敗

	while (off1 < total_len){
		if (list[off1] == 0){
			break;
		} else if (list[off1] == '*'){	// 既にコピー済なら
			//off1 += (wcslen(list + off1) + 1);
			while (list[off1] != 0)
				off1++;
			off1++;
			continue;
		}
		off3 = off1;

		//off2 = off3 + wcslen(list + off3) + 1;	// 次の項目
		off2 = off3;
		while (list[off2] != 0)
			off2++;
		off2++;
		while (off2 < total_len){
//printf("%S (%d), %S (%d)\n", list + off3, off3, list + off2, off2);
			if (list[off2] != '*'){	// まだなら項目を比較する
				//if (wcscmp(list + off3, list + off2) > 0)	// QuickPar はこちらの順序
				if (CompareString(LOCALE_USER_DEFAULT, 0, list + off3, -1, list + off2, -1) > 2)
					off3 = off2;
			}
			//off2 += (wcslen(list + off2) + 1);	// 次の項目
			while (list[off2] != 0)
				off2++;
			off2++;
		}

		// 順番にコピーしていく
//printf("get %S (%d)\n", list + off3, off3);
		wcscpy(work_buf + work_off, list + off3);
		work_off += ((int)wcslen(work_buf + work_off) + 1);
		list[off3] = '*';	// コピーした印
		if (off3 == off1){
			//off1 += (wcslen(list + off1) + 1);
			while (list[off1] != 0)
				off1++;
			off1++;
		}
	}

	// 作業バッファーから戻す
	memcpy(list, work_buf, total_len * 2);
	free(work_buf);
}

// ソース・ファイルのリストに新しいファイル名を追加する
int add_file_path(wchar_t *filename)	// 追加するファイル名
{
	wchar_t *tmp_p;
	int len;

	len = (int)wcslen(filename);

	if (list_len + len >= list_max){	// 領域が足りなくなるなら拡張する
		list_max += ALLOC_LEN;
		tmp_p = (wchar_t *)realloc(list_buf, list_max * 2);
		if (tmp_p == NULL){
			return 1;
		} else {
			list_buf = tmp_p;
		}
	}

	wcscpy(list_buf + list_len, filename);
	list_len += len + 1;

	return 0;
}

// ファイル・リストから指定されたファイル・パスを取り除く
// 減らした後のファイル・リストの文字数を返す
int remove_file_path(
	wchar_t *list,	// ファイル・リスト
	int total_len,	// ファイル・リストの文字数
	int file_off)	// 取り除くファイル・パスの位置
{
	int off, len;

	if (file_off > total_len)
		return total_len;

	len = (int)wcslen(list + file_off) + 1;	// 末尾のNULLを含める
	if (len < total_len){
		for (off = file_off; off < total_len - len; off++)
			list[off] = list[off + len];
		for (off = total_len - len; off < total_len; off++)
			list[off] = 0;
		return total_len - len;
	} else {
		for (off = 0; off < total_len; off++)
			list[off] = 0;
		return 0;
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ファイル・パスからファイル名の位置を戻す
wchar_t * offset_file_name(wchar_t *file_path)
{
	int i;

	for (i = (int)wcslen(file_path) - 2; i >= 0; i--){
		if ((file_path[i] == '\\') || (file_path[i] == '/'))
			break;
	}
	i++;
	return file_path + i;
}

// ファイル・パスからファイル名だけ取り出す
void get_file_name(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *file_name)		// ファイル名
{
	int i, len, find = -1;

	len = (int)wcslen(file_path);
	for (i = len - 2; i >= 0; i--){
		if ((file_path[i] == '\\') || (file_path[i] == '/')){
			find = i;
			break;
		}
	}
	find++;
	for (i = find; i < len + 1; i++){
		file_name[i - find] = file_path[i];
	}
}

// ファイル・パスからディレクトリだけ取り出す、末尾は「\」か「/」
void get_base_dir(
	wchar_t *file_path,		// ファイル・パス
	wchar_t *base_path)		// ディレクトリ
{
	int i, len, find = 0;

	len = (int)wcslen(file_path);
	if (len <= 1){
		base_path[0] = 0;
		return;
	}

	// 最初から末尾が「\」か「/」なら
	if ((file_path[len - 1] == '\\') || (file_path[len - 1] == '/'))
		len--;	// それを取り除いてひとつ親のディレクトリを返す
	for (i = len - 1; i >= 0; i--){
		if (find == 0){
			if ((file_path[i] == '\\') || (file_path[i] == '/')){
				find = 1;
				base_path[i] = file_path[i];
				base_path[i + 1] = 0;
			}
		} else {
			base_path[i] = file_path[i];
		}
	}
	if (find == 0)
		base_path[0] = 0;
}

// ディレクトリ記号の「\」を「/」に置換する
void unix_directory(wchar_t *path)
{
	wchar_t *tmp_p;

	tmp_p = wcschr(path, '\\');
	while (tmp_p != NULL){
		*tmp_p = '/';
		tmp_p = wcschr(tmp_p, '\\');
	}
}

// 絶対パスかどうかを判定する
int is_full_path(wchar_t *path)
{
	if ((path[0] == 0) || (path[1] == 0))
		return 0;	// 2文字以下だと短すぎ

	// 通常のドライブ指定「c:\」の形式
	if ((path[1] == ':') && ((path[2] == '\\') || (path[2] == '/')))
		return 1;

	//「\\?\」が付くのは絶対パスだけ
	// ネットワークの UNC パスなら「\\<server>\<share>」
	if (((path[0] == '\\') || (path[0] == '/')) && ((path[1] == '\\') || (path[1] == '/')))
		return 1;

	return 0;
}

// ファイルのディレクトリ位置が同じかどうかを調べる
// フォルダには対応してないことに注意!
int compare_directory(wchar_t *path1, wchar_t *path2)
{
	int len1, len2;

	// パスの長さ
	len1 = 0;
	while (path1[len1] != 0)
		len1++;
	// フォルダの位置を調べたい時は末尾の「\」を無視する
//	if (accept_folder != 0)
//		len1--;
	// サブ・ディレクトリの長さにする
	while ((len1 > 0) && (path1[len1] != '\\'))
		len1--;

	// パスの長さ
	len2 = 0;
	while (path2[len2] != 0)
		len2++;
	// フォルダの位置を調べたい時は末尾の「\」を無視する
//	if (accept_folder != 0)
//		len2--;
	// サブ・ディレクトリの長さにする
	while ((len2 > 0) && (path2[len2] != '\\'))
		len2--;

	// サブ・ディレクトリの長さを比較する
	if (len1 > len2)
		return 1;	// path1 が path2 より大きい
	if (len1 < len2)
		return -1;	// path1 が path2 より小さい

	// ディレクトリの長さが同じなら
	return _wcsnicmp(path1, path2, len1);
}

// ワイルドカードを含む文字列をコピーする
// 出力文字数を返す (null文字を含む)、不正なパスなら 0 を返す
int copy_wild(wchar_t *dst, wchar_t *src)
{
	wchar_t s;
	int len = 0;

	while ((src[0] == '\\') || (src[0] == '/'))	// 先頭の「\」は無視する
		src++;

	do {
		s = src[0];
		if (s == '/'){	// ディレクトリ記号を統一する
			s = '\\';
			if ((src[1] == '\\') || (src[1] == '/'))
				return 0;
		} else if (s == '\\'){	// 「\\」はパスに含まれない
			if ((src[1] == '\\') || (src[1] == '/'))
				return 0;
		} else if ((s >= 'A') && (s <= 'Z')){	// 大文字を小文字に変換する
			s += 32;
		} else if ((s == '*') && (src[1] == '*')){	// 三個以上の「*」を一個にする
			while (src[2] == '*')
				src++;
		} else if ((s == '.') && (src[1] == '.')){	// 「..」はパスに含まれない
			return 0;
		}

		dst[len++] = s;
		src++;
	} while (s != 0);

	return len;	// 末尾の null 文字を個数に含むことに注意
}

// ワイルドカード('*', '?')を使ってユニコードのパスを比較する
// 「*」と「?」はディレクトリ記号に一致しない
// 「**」はディレクトリ記号に一致する
int PathMatchWild(
	wchar_t *text,	// 比較する文字列
	wchar_t *wild)	// ワイルドカード (小文字にすること)
{
	wchar_t next_char, *wild_prev, *wild_dir, *text_dir;
	int stop_dir;

	next_char = 0;
	wild_prev = NULL;
	wild_dir = NULL;
	text_dir = text;
	stop_dir = 0;

	while (*text != 0){
		//printf("%c, %c \n", *text, *wild);
		if (*wild == '*'){
			wild++;
			if (*wild == '*'){	// 「**」
				while (*wild == '*')	// 二個より連続した「*」を無視する
					wild++;
				stop_dir = 0;
				wild_dir = wild;
			} else {	// 「*」
				stop_dir = 1;
			}

			if (*wild == 0){	// 「*」の次が無ければ一致とみなす
				if (stop_dir){	// ディレクトリ記号が存在すれば不一致
					while (*text != 0){
						if (*text == '\\'){
							stop_dir = 2;
							break;
						}
						text++;
					}
					if (stop_dir == 2){
						if (wild_dir != NULL){
							stop_dir = 0;
							wild_prev = wild_dir;
							text = text_dir + 1;
							wild = wild_dir;
							next_char = *wild;
							//printf("\n agian last: %c, %c \n", *text, next_char);
							continue;
						}
						//printf("\n last * != include \\ \n");
						return 0;
					}
				}
				return 1;
			} else {
				next_char = *wild;	// 「*」の次の文字
				wild_prev = wild;	// 最後の「*」の位置
				//printf("next = %c, stop_dir = %d\n", next_char, stop_dir);
			}
		}

		if ((*text == '\\') && (stop_dir))	// ディレクトリ記号が存在するなら再比較をしない
			wild_prev = NULL;

		if (next_char != 0){	// 「*」の後なら
			if ((next_char == towlower(*text)) || ((*text != '\\') && (next_char == '?'))){
				wild++;
				next_char = 0;
				text_dir = text;
			}
			if ((*text == '\\') && (stop_dir))	// ディレクトリ記号が存在するならワイルドカードを無効にする
				next_char = 0;
			text++;

		} else if ((*wild == towlower(*text)) || ((*text != '\\') && (*wild == '?'))){	// 同じ文字なら
			wild++;
			text++;

		} else {	// 違う文字なら
			if (wild_prev != NULL){	// 直前のワイルドカードから比較しなおす
				wild = wild_prev;
				next_char = *wild;
				//printf("\n agian: %c, %c \n", *text, next_char);

			} else if ((stop_dir != 0) && (wild_dir != NULL)){
				stop_dir = 0;
				wild_prev = wild_dir;
				text = text_dir + 1;
				wild = wild_dir;
				next_char = *wild;
				//printf("\n agian dir: %c, %c \n", *text, next_char);

			} else{
				//printf("\n diff: %c, %c \n", *text, *wild);
				return 0;	// それ以上一致しない
			}
		}
	}

	while (*wild == '*')	// 残りの「*」を無視する
		wild++;
	if (*wild == 0)	// ワイルドカードも終端なら一致とみなす
		return 1;

	return 0;
}

/*
int PathMatchWild1(
	wchar_t *text,	// 比較する文字列
	wchar_t *wild)	// ワイルドカード (小文字にすること)
{
	wchar_t next_char, *wild_prev;

	next_char = 0;
	wild_prev = NULL;

	while (*text != 0){
		//printf("%c, %c \n", *text, *wild);

		if (*wild == '*'){
			wild++;
			while (*wild == '*')	// 連続した「*」を無視する
				wild++;

			if (*wild == 0){	// 「*」の次が無ければ一致とみなす
				return 1;
			} else {
				next_char = *wild;	// 「*」の次の文字
				wild_prev = wild;	// 最後の「*」の位置
			}
		}

		if (next_char != 0){	// 「*」の後なら
			if ((next_char == towlower(*text)) || (next_char == '?')){
				wild++;
				next_char = 0;
			}
			text++;

		} else if ((*wild == towlower(*text)) || (*wild == '?')){	// 同じ文字なら
			wild++;
			text++;

		} else {	// 違う文字なら
			if (wild_prev != NULL){	// 直前のワイルドカードから比較しなおす
				wild = wild_prev;
				next_char = *wild;
				//printf("\n agian: %c, %c \n", *text, next_char);

			} else{
				return 0;	// それ以上一致しない
			}
		}
	}

	while (*wild == '*')	// 残りの「*」を無視する
		wild++;
	if (*wild == 0)	// ワイルドカードも終端なら一致とみなす
		return 1;

	return 0;
}
*/

// ファイルのパスを除外リストと比較する
int exclude_path(wchar_t *path)
{
	int off = 0, deny = 0;

	deny = list2_max;
	while (off < list2_len){
		if (list2_buf[off++] == '+'){	// allow
			if ((deny != 0) && (PathMatchWild(path, list2_buf + off) != 0)){
			//	printf_cp("allow: '%s'", path);
			//	printf_cp(" : '%s'\n", list2_buf + off);
				deny = 0;
			}
		} else {	// deny
			if (PathMatchWild(path, list2_buf + off) != 0){
			//	printf_cp("deny : '%s'", path);
			//	printf_cp(" : '%s'\n", list2_buf + off);
				return 1;
			}
		}
		off += (int)wcslen(list2_buf + off) + 1;
	}

//	if ((list2_max != 0) && (deny != 0))
//		printf_cp("not  : '%s'\n", path);

	return deny;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define PREFIX_LEN	4	// 「\\?\」の長さ

// 相対パスを絶対パスに変換し、パスの先頭に "\\?\" を追加する
// 戻り値 : 0=エラー, 5〜=新しいパスの長さ
int copy_path_prefix(
	wchar_t *new_path,	// 新しいパス
	int max_len,		// 新しいパスの最大長さ (末尾の null文字も含む)
	wchar_t *src_path,	// 元のパス (相対パスでもよい)
	wchar_t *dir_path)	// 相対パスの場合に基準となるディレクトリ (NULL ならカレント・ディレクトリ)
{
	wchar_t tmp_path[MAX_LEN];
	int len;

	// 不正なファイル名を拒否する (検索記号は許可する)
	if (src_path[0] == 0)
		return 0;
	if (wcspbrk(src_path, L"\"<>|"))	// WinOS ではファイル名に「\/:*?"<>|」の文字は使えない
		return 0;

	// 相対パスに対して基準となるディレクトリが指定されてるなら
	if ((dir_path != NULL) && (is_full_path(src_path) == 0)){
		// そのまま基準ディレクトリを連結して絶対パスにする
		len = (int)wcslen(dir_path);
		if (len + (int)wcslen(src_path) >= max_len)
			return 0;
		wcscpy(tmp_path, dir_path);	// dir_path の末尾は「\」にしておくこと
		wcscpy(tmp_path + len, src_path);
		if (_wfullpath(new_path, tmp_path, max_len) == NULL)
			return 0;
	} else {	// カレント・ディレクトリを使う
		// 相対パスから絶対パスに変換する (ディレクトリ記号も「\」に統一される)
		if (_wfullpath(new_path, src_path, max_len) == NULL)
			return 0;
	}

	// 先頭に "\\?\" が存在しないなら、追加する
	if ((wcsncmp(new_path, L"\\\\?\\", PREFIX_LEN) != 0)){
		len = (int)wcslen(new_path);
		if ((new_path[0] == '\\') && (new_path[1] == '\\')){	// UNC パスなら
			if (len + PREFIX_LEN + 2 >= max_len)
				return 0;	// バッファー・サイズが足りなくて追加できない
			memmove(new_path + (PREFIX_LEN + 3), new_path + 1, len * 2);
			new_path[PREFIX_LEN    ] = 'U';
			new_path[PREFIX_LEN + 1] = 'N';
			new_path[PREFIX_LEN + 2] = 'C';
		} else {	// 通常のドライブ記号で始まるパスなら
			if (len + PREFIX_LEN >= max_len)
				return 0;	// バッファー・サイズが足りなくて追加できない
			memmove(new_path + PREFIX_LEN, new_path, (len + 1) * 2);
		}
		memcpy(new_path, L"\\\\?\\", PREFIX_LEN * 2);
	}

	// 8.3形式の短いファイル名を長いファイル名に変換する
	if (GetFileAttributes(new_path) == INVALID_FILE_ATTRIBUTES){
		// 指定されたファイルが存在しない場合 (作成するPARファイルや「*?」で検索するソース・ファイル)
		for (len = PREFIX_LEN; new_path[len] != 0; len++){
			if ((new_path[len] == '*') || (new_path[len] == '?'))
				break;	// ワイルドカードを含むパス
		}
		// ワイルドカードより前のディレクトリ記号を探す
		for (; len >= 0; len--){
			if (new_path[len] == '\\')
				break;
		}
		len++;
		wcscpy(tmp_path, new_path + len);	// 存在しないファイル名を記録しておく
		new_path[len] = 0;	// ファイル名を消す
		len = GetLongPathName(new_path, new_path, max_len);
		if (len == 0){	// フォルダーやファイルが存在しなくてもエラー終了しない
			//printf("GetLongPathName (not exist) : err = %d\n", GetLastError());
			//print_win32_err();
			wcscat(new_path, tmp_path);
			len = (int)wcslen(new_path);
		} else {	// 退避させておいたファイル名を追加する
			len += (int)wcslen(tmp_path);
			if (len < max_len)
				wcscat(new_path, tmp_path);
		}
	} else {
		len = GetLongPathName(new_path, new_path, max_len);
		if (len == 0){	// 変換エラー
			//printf("GetLongPathName (exist) : err = %d\n", GetLastError());
			return 0;
		}
	}
	if (len >= max_len)
		return 0;

	return len;
}

// ファイル・パスから、先頭にある "\\?\" を省いた長さを戻す
int len_without_prefix(wchar_t *file_path)
{
	// 最初から "\\?\" が無ければ
	if (wcsncmp(file_path, L"\\\\?\\", PREFIX_LEN) != 0)
		return (int)wcslen(file_path);	// そのままの長さ

	// "\\?\UNC\<server>\<share>" ならネットワーク・パス
	if (wcsncmp(file_path + PREFIX_LEN, L"UNC\\", 4) == 0)
		return (int)wcslen(file_path + PREFIX_LEN + 2);	// "\\?\UNC\" -> "\\" なので6文字減らす

	// "\\?\<drive:>\<path>" なら
	return (int)wcslen(file_path + PREFIX_LEN);	// "\\?\" を省くので4文字減らす
}

// ファイル・パスから、先頭にある "\\?\" を省いてコピーする
int copy_without_prefix(
	wchar_t *dst_path,	// コピー先 (コピー元と同じアドレスでもよい)
	wchar_t *src_path)	// コピー元のパス
{
	int i, len;

	len = (int)wcslen(src_path);	// 元の文字数

	// 最初から "\\?\" が無ければ
	if (wcsncmp(src_path, L"\\\\?\\", PREFIX_LEN) != 0){
		for (i = 0; i <= len; i++)
			dst_path[i] = src_path[i];	// そのままコピーする
		return len;
	}

	// "\\?\UNC\<server>\<share>" ならネットワーク・パス
	if (wcsncmp(src_path + PREFIX_LEN, L"UNC\\", 4) == 0){
		len -= PREFIX_LEN + 2;	// "\\?\UNC\" -> "\\" なので6文字減らす
		for (i = 1; i <= len; i++)
			dst_path[i] = src_path[PREFIX_LEN + 2 + i];
		dst_path[0] = '\\';	// 先頭を "\\" に変えておく
		return len;
	}

	// "\\?\<drive:>\<path>" なら
	len -= PREFIX_LEN;	// "\\?\UNC\" -> "\\" なので6文字減らす
	for (i = 0; i <= len; i++)
		dst_path[i] = src_path[PREFIX_LEN + i];
	return len;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// 順番にサブ・ディレクトリを作成する
int make_dir(
	wchar_t *file_path)		// サブ・ディレクトリを含むファイル・パス
{
	wchar_t dir_path[MAX_LEN];
	unsigned int rv;

	get_base_dir(file_path, dir_path);	// 親ディレクトリを取得する
	if (dir_path[0] == 0)
		return 0;	// ディレクトリがないなら出る
	rv = (int)wcslen(dir_path);
	if ((dir_path[rv - 1] == '\\') || (dir_path[rv - 1] == '/'))	// 末尾の「/」を取り除く
		dir_path[rv - 1] = 0;

	rv = GetFileAttributes(dir_path);
	if (rv == INVALID_FILE_ATTRIBUTES){	// フォルダがまだ存在しない場合
		// 先に親フォルダを作成する
		if (rv = make_dir(dir_path))
			return rv;
		if (!CreateDirectory(dir_path, NULL))
			return 1;
	} else if ((rv & FILE_ATTRIBUTE_DIRECTORY) == 0){
		// すでに同じ名前のファイルが存在する場合はエラーが発生する
		return 2;
	}
	return 0;
}

// ファイルを置き換える (ファイル名の修正、テンポラリー・ファイルからの書き戻しなど)
int replace_file(
	wchar_t *dest_path,		// 置き換える先のパス (移動先、修正されたファイル名)
	wchar_t *sorc_path)		// 置き換える元のパス (移動元、現在のファイル名)
{
	unsigned int rv, num;

	// 移動元のファイルが存在するかどうか
	num = GetFileAttributes(sorc_path);
	if (num == INVALID_FILE_ATTRIBUTES){
		return 1;	// 移動させるファイルが存在しない
	} else {
		if (num & FILE_ATTRIBUTE_DIRECTORY){	// 対象がフォルダなら末尾の「\」を取り除く
			rv = (unsigned int)wcslen(sorc_path);
			if (sorc_path[rv - 1] == '\\')
				sorc_path[rv - 1] = 0;
			rv = (unsigned int)wcslen(dest_path);
			if (dest_path[rv - 1] == '\\')
				dest_path[rv - 1] = 0;
		}
		if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、その属性を解除する
			if (SetFileAttributes(sorc_path, num ^ FILE_ATTRIBUTE_READONLY) == 0)
				return 2;	// 移動元の読み取り専用属性を解除できなかった
		}
	}
	//printf_cp("dest_path = %s\n", dest_path);
	//printf_cp("sorc_path = %s\n", sorc_path);

	// 移動先のファイルが存在するかどうか
	rv = GetFileAttributes(dest_path);
	if (rv == INVALID_FILE_ATTRIBUTES){	// ファイルが存在しないなら、そのまま移動する
		if (MoveFile(sorc_path, dest_path) == 0){
			if (GetLastError() == ERROR_PATH_NOT_FOUND){	// Path not found (3)
				make_dir(dest_path);	// 途中のフォルダが存在しないのなら作成する
				if (MoveFile(sorc_path, dest_path) == 0)
					return 3;	// それでも移動失敗
			} else {
				return 4;	// 移動失敗
			}
		}
		if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
			rv = GetFileAttributes(dest_path);
			SetFileAttributes(dest_path, rv | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
		}

	} else if ((num & FILE_ATTRIBUTE_DIRECTORY) != (rv & FILE_ATTRIBUTE_DIRECTORY)){
		// ファイルの移動先にフォルダ、またはフォルダの移動先にファイルが存在するなら
		move_away_file(dest_path);	// それをどかす
		if (MoveFile(sorc_path, dest_path) == 0)
			return 5;	// 移動失敗
		if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
			rv = GetFileAttributes(dest_path);
			SetFileAttributes(dest_path, rv | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
		}

	} else {	// 既にファイルが存在するなら
		wchar_t back_path[MAX_LEN];

		back_path[0] = 0;
		if (switch_b & 3){	// 既存のファイルを削除せずに別名で残す
			for (num = 1; num < 100; num++){
				swprintf(back_path, _countof(back_path), L"%s.%d", dest_path, num);	// バックアップ・ファイルのパス
				if (GetFileAttributes(back_path) == INVALID_FILE_ATTRIBUTES)
					break;	// 存在しないなら OK
			}
			if (num >= 100)
				back_path[0] = 0;	// バックアップがいっぱいならそれ以上作らない
		}
		if (back_path[0] != 0){	// バックアップを作って置き換える
			//printf_cp("back_path = %s\n", back_path);
			num = ReplaceFile(dest_path, sorc_path, back_path, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
			if (num == 0){
				num = GetLastError();
				//printf("error = %d\n", num);
				if ((num == 32) ||	// The process cannot access the file because it is being used by another process.
						(num == 1175) ||	// ERROR_UNABLE_TO_REMOVE_REPLACED
						(num == 1176)){		// ERROR_UNABLE_TO_MOVE_REPLACEMENT
					Sleep(100);
					num = ReplaceFile(dest_path, sorc_path, back_path, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
				} else if (num == 1177){	// ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
					Sleep(100);
					num = MoveFile(sorc_path, dest_path);	// 既存ファイルはバックアップ済みなら、移動するだけ
				} else {
					num = 0;
				}
				if (num == 0)
					return 6;
			}
			if (switch_b & 2)	// バックアップをゴミ箱に入れる
				delete_file_recycle(back_path);
		} else {	// 既存のファイルを削除して置き換える
			if (rv & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、一旦その属性を解除する
				if (SetFileAttributes(dest_path, rv ^ FILE_ATTRIBUTE_READONLY) == 0)
					return 7;	// 移動先の読み取り専用属性を解除できなかった = 削除もできない
			}
			num = ReplaceFile(dest_path, sorc_path, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
			if (num == 0){
				num = GetLastError();
				//printf("error = %d\n", num);
				if ((num == 32) ||	// The process cannot access the file because it is being used by another process.
						(num == 1175)){	// ERROR_UNABLE_TO_REMOVE_REPLACED
					Sleep(100);
					num = ReplaceFile(dest_path, sorc_path, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0);
				} else if (num == 1176){	// ERROR_UNABLE_TO_MOVE_REPLACEMENT
					Sleep(100);
					num = MoveFile(sorc_path, dest_path);	// 既存ファイルは削除済みなら、移動するだけ
				} else {
					num = 0;
				}
				if (num == 0)
					return 8;
			}
			if (rv & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
				rv = GetFileAttributes(dest_path);
				SetFileAttributes(dest_path, rv | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
			}
		}
	}

	return 0;
}

// ファイルまたはフォルダをどかす
void move_away_file(
	wchar_t *file_path)		// ファイル・パス
{
	wchar_t new_path[MAX_LEN];
	unsigned int rv, num;

	rv = GetFileAttributes(file_path);
	if (rv != INVALID_FILE_ATTRIBUTES){	// 既にファイルが存在するなら
		if (rv & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、一旦その属性を解除する
			if (SetFileAttributes(file_path, rv ^ FILE_ATTRIBUTE_READONLY) == 0)
				return;	// 既存ファイルの読み取り専用属性を解除できなかった
		}

		if (switch_b & 3){	// 削除せずに別名で残す
			for (num = 1; num < 100; num++){
				swprintf(new_path, _countof(new_path), L"%s.%d", file_path, num);
				if ((MoveFile(file_path, new_path) != 0) || (GetLastError() != ERROR_ALREADY_EXISTS))
					break;	// Destination file already exists (183) で移動できない時だけ再試行する
			}
			if (num < 100){	// 移動できた
				if (rv & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
					rv = GetFileAttributes(new_path);
					SetFileAttributes(new_path, rv | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
				}
				if (switch_b & 2)	// ゴミ箱に入れる
					delete_file_recycle(new_path);
				return;
			}
		}

		// 削除する
		if (rv & FILE_ATTRIBUTE_DIRECTORY){
			RemoveDirectory(file_path);	// フォルダを削除する
		} else {
			DeleteFile(file_path);	// 削除する
		}
	}
}

// ファイルを指定サイズに縮小する
int shorten_file(
	wchar_t *file_path,		// ファイル・パス
	__int64 new_size)
{
	wchar_t back_path[MAX_LEN];
	unsigned int rv, len, num;
	HANDLE hFileWrite;
	LARGE_INTEGER qwi;	// Quad Word Integer

	back_path[0] = 0;
	if (switch_b & 3){	// 既存のファイルを削除せずに別名で残す
		for (num = 1; num < 100; num++){
			swprintf(back_path, _countof(back_path), L"%s.%d", file_path, num);	// バックアップ・ファイルのパス
			if (GetFileAttributes(back_path) == INVALID_FILE_ATTRIBUTES)
				break;	// 存在しないなら OK
		}
		if (num >= 100)
			back_path[0] = 0;	// バックアップがいっぱいならそれ以上作らない
	}

	if (back_path[0] != 0){	// バックアップを作って置き換える
		wchar_t temp_path[MAX_LEN];
		// テンポラリー・ファイルを作ってファイル内容をコピーする
		get_temp_name(file_path, temp_path);
		hFileWrite = CreateFile(temp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFileWrite == INVALID_HANDLE_VALUE){	// 作れなかったら
			back_path[0] = 0;
		} else if (new_size <= 0){
			CloseHandle(hFileWrite);
		} else {
			// ファイル・サイズを指定サイズにする
			qwi.QuadPart = new_size;
			if (!SetFilePointerEx(hFileWrite, qwi, NULL, FILE_BEGIN)){
				back_path[0] = 0;
			} else if (!SetEndOfFile(hFileWrite)){
				back_path[0] = 0;
			} else {
				// ポインターを先頭に戻す
				qwi.QuadPart = 0;
				if (!SetFilePointerEx(hFileWrite, qwi, NULL, FILE_BEGIN)){
					back_path[0] = 0;
				} else {
					__int64 left_size = new_size;
					HANDLE hFileRead;
					hFileRead = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
					if (hFileRead == INVALID_HANDLE_VALUE){
						back_path[0] = 0;
					} else {	// 指定サイズまでコピーする
						unsigned char buf[IO_SIZE];
						while (left_size > 0){
							len = IO_SIZE;
							if (left_size < IO_SIZE)
								len = (unsigned int)left_size;
							left_size -= len;
							if (!ReadFile(hFileRead, buf, len, &rv, NULL) || (len != rv)){
								back_path[0] = 0;
								break;
							}
							if (!WriteFile(hFileWrite, buf, len, &rv, NULL)){
								back_path[0] = 0;
								break;
							}
						}
						CloseHandle(hFileRead);
					}
				}
			}
			CloseHandle(hFileWrite);
			if (back_path[0] == 0)	// 失敗したらテンポラリー・ファイルは削除する
				DeleteFile(temp_path);
		}

		if (back_path[0] != 0){	// バックアップを作って置き換える
			if (ReplaceFile(file_path, temp_path, back_path, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0) == 0){
				DeleteFile(temp_path);
				return 1;
			}
			if (switch_b & 2)	// バックアップをゴミ箱に入れる
				delete_file_recycle(back_path);
			return 0;
		}
	}

	num = GetFileAttributes(file_path);
	if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性なら、一旦その属性を解除する
		if (SetFileAttributes(file_path, num ^ FILE_ATTRIBUTE_READONLY) == 0)
			return 1;	// 読み取り専用属性を解除できなかった = 縮小もできない
	}

	// 直接開いて縮小する
	hFileWrite = CreateFile(file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFileWrite == INVALID_HANDLE_VALUE)
		return 1;
	// ファイル・サイズを指定サイズにする
	qwi.QuadPart = new_size;
	if (!SetFilePointerEx(hFileWrite, qwi, NULL, FILE_BEGIN)){
		CloseHandle(hFileWrite);
		return 1;
	}
	if (!SetEndOfFile(hFileWrite)){
		CloseHandle(hFileWrite);
		return 1;
	}
	CloseHandle(hFileWrite);

	if (num & FILE_ATTRIBUTE_READONLY){	// 読み取り専用属性を付け直す
		num = GetFileAttributes(file_path);
		SetFileAttributes(file_path, num | FILE_ATTRIBUTE_READONLY);	// 失敗してもエラーにしない
	}

	return 0;
}

// デバイス名かどうかを判定する
static int check_device_name(wchar_t *name, int len)
{
	if (len >= 3){
		if ((name[3] == 0) || (name[3] == '.') || (name[3] == '\\')){
			if (_wcsnicmp(name, L"CON", 3) == 0)
				return 1;
			if (_wcsnicmp(name, L"PRN", 3) == 0)
				return 1;
			if (_wcsnicmp(name, L"AUX", 3) == 0)
				return 1;
			if (_wcsnicmp(name, L"NUL", 3) == 0)
				return 1;
		}
		if (len >= 4){
			if ((name[4] == 0) || (name[4] == '.') || (name[4] == '\\')){
				if (_wcsnicmp(name, L"COM", 3) == 0){
					if ((name[3] >= 0x31) && (name[3] <= 0x39))
						return 1;
				}
				if (_wcsnicmp(name, L"LPT", 3) == 0){
					if ((name[3] >= 0x31) && (name[3] <= 0x39))
						return 1;
				}
			}
			if (len >= 6){
				if ((name[6] == 0) || (name[6] == '.') || (name[6] == '\\')){
					if (_wcsnicmp(name, L"CLOCK$", 6) == 0)
						return 1;
				}
			}
		}
	}

	return 0;
}

// ファイル名が有効か確かめて、問題があれば浄化する
// 戻り値 0=変更無し, +1=ディレクトリ記号, +2=浄化した, +4=文字数が変わった, 16=エラー
int sanitize_filename(
	wchar_t *name,		// 検査するファイル名
	file_ctx_r *files,	// 各ソース・ファイルの情報
	int num)			// 比較から除外するファイル番号
{
	int i, j, rv = 0, len = 0;

	// 制御文字 1〜31 (改行やタブなど) を削除する
	while (len < MAX_LEN){
		if (name[len] == 0){
			break;
		} else if (name[len] < 32){
			i = len;
			do {
				name[i] = name[i + 1];
				i++;
			} while (name[i] != 0);
			rv |= 4;
		} else {
			len++;
		}
	}
	if (len == 0)
		name[0] = 0;

	// WinOS ではファイル名に「\/:*?"<>|」の文字は使えないので置換する
	for (i = 0; i < len; i++){
		if ((name[i] == '\\') || (name[i] == ':') || (name[i] == '*') || (name[i] == '?') || (name[i] == '"')
				|| (name[i] == '<') || (name[i] == '>') || (name[i] == '|')){
			name[i] = '_';	// 「\,:,*,?,",<,>,|」 ->「_」
			rv |= 2;
		} else if (name[i] == '/'){	// ディレクトリ記号を変換する
			name[i] = '\\';	// 「/」 -> 「\」
			rv |= 1;
		}
	}

	// 末尾の「.」「 」を削除する
	while (len > 0){
		if ((name[len - 1] == '.') || (name[len - 1] == ' ')){
			name[len - 1] = 0;
			len--;
			rv |= 4;
		} else {
			break;
		}
	}

	// 「\」の前に「.」「 」があれば削除する (不正なファイル名とディレクトリ移動を防ぐ)
	i = len - 1;
	while (i > 0){
		if ((name[i] == '\\') && ((name[i - 1] == '.') || (name[i - 1] == ' '))){
			for (j = i - 1; j < len; j++)
				name[j] = name[j + 1];
			len--;
			rv |= 4;
		}
		i--;
	}

	// 連続した「\」を一文字にする
	i = 0;
	while (i < len){
		if ((name[i] == '\\') && (name[i + 1] == '\\')){
			for (j = i + 1; j < len; j++)
				name[j] = name[j + 1];
			len--;
			rv |= 4;
		} else {
			i++;
		}
	}

	// 先頭の「\」を削除する
	if (name[0] == '\\'){
		for (i = 0; i < len; i++)
			name[i] = name[i + 1];
		len--;
		rv |= 4;
	}

	// デバイス名は許可しない (サブ・ディレクトリも含める)
	i = len;
	while (i > 0){
		i--;
		if ((i == 0) || ((i > 0) && (name[i - 1] == '\\'))){
			if (check_device_name(name + i, len) != 0){
				if (len >= MAX_LEN - 1)
					return 16;
				for (j = len; j >= i; j--)
					name[j + 1] = name[j];
				name[i] = '_';	// 先頭に「_」を追加する
				len++;
				rv |= 4;
			}
		}
	}

	// 既に同名のファイルが存在するなら、末尾に「(値)」を追加する
	if (files != NULL){
		int k;
		if (len == 0){	// ファイル名が空になった場合は必ず追加する
			i = 0;
		} else {
			// 大文字と小文字を区別するシステム上で作られた PAR ファイルは Windows OS では問題になる
			for (i = 0; i < file_num; i++){
				if (i == num){
					if (rv & 6){
						continue;	// 元のファイル名とは比較しない
					} else {
						i = file_num;
						break;		// それまでのファイル名とだけ比較する
					}
				}
				if (files[i].name <= 0)
					continue;	// ファイル名が取得できなかったファイルとは比較しない
				if (_wcsicmp(name, list_buf + files[i].name) == 0)
					break;	// ファイル名が同じ
			}
		}
		if (i < file_num){
			if (len >= MAX_LEN - 3)
				return 16;
			k = 0;
			for (j = len - 1; j > 0; j--){
				if (name[j] == '.'){
					k = j;
					break;
				}
				if (name[j] == '\\'){	// サブ・ディレクトリは除外する
					if (j == len - 1)
						k = len - 1;	// フォルダなら
					break;
				}
			}
			if (k > 0){	// 拡張子があるなら、拡張子の前に追加する
				for (j = len; j >= k; j--)
					name[j + 3] = name[j];
				name[k    ] = '(';
				name[k + 2] = ')';
				k = k + 1;
			} else {	// 拡張子が無ければ末尾に追加する
				name[len    ] = '(';
				name[len + 2] = ')';
				name[len + 3] = 0;
				k = len + 1;
			}
			len += 3;
			rv |= 4;
			for (j = 1; j < 10; j++){
				name[k] = (unsigned short)(0x30 + j);
				for (i = 0; i < file_num; i++){
					if (i == num){
						if (rv & 6){
							continue;	// 元のファイル名とは比較しない
						} else {
							i = file_num;
							break;		// それまでのファイル名とだけ比較する
						}
					}
					if (files[i].name <= 0)
						continue;	// ファイル名が取得できなかったファイルとは比較しない
					if (_wcsicmp(name, list_buf + files[i].name) == 0)
						break;	// ファイル名が同じ
				}
				if (i == file_num)
					break;
			}
			if (j >= 10)
				return 16;	// 同名になるファイルが多過ぎる
		}
	}

	return rv;
}

// リカバリ・ファイルのパスから拡張子とボリューム番号を取り除く
void get_base_filename(
	wchar_t *file_path,		// リカバリ・ファイルのパス
	wchar_t *base_path,		// 基準ファイル名のパス
	wchar_t *file_ext)		// 拡張子 (存在するなら)
{
	wchar_t *tmp_p, *file_name;
	int len, match;

	file_ext[0] = 0;
	wcscpy(base_path, file_path);
	file_name = offset_file_name(base_path);
	tmp_p = wcsrchr(file_name, '.');
	if (tmp_p != NULL){	// 拡張子または「.vol*」が存在すれば取り除く
		// 拡張子が無い場合も考慮する
		if (_wcsnicmp(tmp_p, L".vol", 4) == 0){
			match = 0;
			len = 4;
			while (tmp_p[len] != 0){
				if ((tmp_p[len] >= 48) && (tmp_p[len] <= 57)){
					match |= 1;	// 数字は何個あってもよい
				} else if ((tmp_p[len] == '+') || (tmp_p[len] == '-') || (tmp_p[len] == '_')){
					match += 2;	// 記号は一個だけしかだめ
				} else {	// それ以外なら判定終了
					break;
				}
				len++;
			}
			if (match == 3)
				tmp_p[0] = 0;	// 取り除く
		}
		if (tmp_p[0] != 0){	// 拡張子があるなら
			if (wcslen(tmp_p) < EXT_LEN){
				wcscpy(file_ext, tmp_p);	// 拡張子を記録しておく
				tmp_p[0] = 0;	// 拡張子を取り除く
			}
		}
		// 更に「.vol*」があればそれも取り除く
		tmp_p = wcsrchr(file_name, '.');
		if (tmp_p != NULL){
			if (_wcsnicmp(tmp_p, L".vol", 4) == 0){
				match = 0;
				len = 4;
				while (tmp_p[len] != 0){
					if ((tmp_p[len] >= 48) && (tmp_p[len] <= 57)){
						match |= 1;	// 数字は何個あってもよい
					} else if ((tmp_p[len] == '+') || (tmp_p[len] == '-') || (tmp_p[len] == '_')){
						match += 2;	// 記号は一個だけしかだめ
					} else {	// それ以外なら判定終了
						break;
					}
					len++;
				}
				if (match == 3)
					tmp_p[0] = 0;	// 取り除く
			}
		}
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
// バイトごとに16進数表示にする、十分な領域を確保しておくこと
void byte_encode(wchar_t *out, unsigned char *in, int len)
{
	int i, c;

	for (i = 0; i < len; i++){
		c = in[i] >> 4;
		if (c < 10){	// 0 to 9
			*out = c + 48;
		} else {		// A to Z
			*out = c - 10 + 65;
		}
		out++;
		c = in[i] & 15;
		if (c < 10){	// 0 to 9
			*out = c + 48;
		} else {		// A to Z
			*out = c - 10 + 65;
		}
		out++;
	}
	*out = 0;
}

void byte_decode(unsigned char *out, wchar_t *in, int len)
{
	unsigned int c1, c2;

	while (len >= 2){
		c1 = *in;
		in++;
		if ((c1 >= 48) && (c1 <= 57)){			// 0 to 9
			c1 -= 48;
		} else if ((c1 >= 65) && (c1 <= 90)){	// A to Z
			c1 -= 65 - 10;
		} else {
			break;
		}
		c2 = *in;
		in++;
		if ((c2 >= 48) && (c2 <= 57)){			// 0 to 9
			c2 -= 48;
		} else if ((c2 >= 65) && (c2 <= 90)){	// A to Z
			c2 -= 65 - 10;
		} else {
			break;
		}
		*out = (unsigned char)((c1 << 4) | c2);
		out++;
		len -= 2;
	}
}
*/

// ハッシュ値を表示する
void print_hash(unsigned char hash[16])
{
	printf("%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
		hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7],
		hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
}

// 64-bit 整数の平方根を求める
// by n-trino, http://d.hatena.ne.jp/n-trino/20080225
unsigned int sqrt64(__int64 num)
{
	unsigned int i, result = 0;
	__int64 rest = 0, sum = 0;

	if (num <= 1)
		return 1;	// 1より小さい値が入力されることは無い

	for (i = 0; i < 32; i++){
		rest = ( rest << 2 ) | ( ( num >> 62 ) & 3 );
		num <<= 2;
		result <<= 1;
		sum <<= 1;
		if ( ( sum | 1 ) <= rest ){
			sum |= 1;
			rest -= sum;
			sum++;
			result |= 1;
		}
	}

	return result;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int cpu_num = 1;	// CPU/Core 個数が制限されてる場合は、上位に本来の数を置く
// /arch:SSE2, +1=SSSE3, +2=SSE4.1, +4=SSE4.2, +8=CLMUL, +16=AVX2, +128=JIT(SSE2), +256=Old
unsigned int cpu_flag = 0;
unsigned int memory_use;	// メモリー使用量 0=auto, 1〜7 -> 1/8 〜 7/8

static int count_bit(unsigned int value)
{
	int bitCount = 0;

	// Shift を使わない方法、計算量は平均 (n/2)
	while (value != 0){
		bitCount++;
		value &= (value - 1);
	}

	return bitCount;
}

// AMD CPU_ID
static int count_associativity(int bit_value)
{
	switch (bit_value & 0xF){
	case 1:
		return 1;
	case 2:
		return 2;
	case 4:
		return 4;
	case 6:
		return 8;
	case 8:
		return 16;
	case 10:
		return 32;
	case 11:
		return 48;
	case 12:
		return 64;
	case 13:
		return 96;
	case 14:
		return 128;
	default:
		return 256;
	}
}

void check_cpu(void)
{
	int core_count, use_count;
	unsigned int CPUInfo[4];
	unsigned int ProcessAffinityMask;
	unsigned int cache_limit, cache_size;
	unsigned int returnLength, byteOffset;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL, ptr;

	// CPU の拡張機能を調べる
	__cpuid(CPUInfo, 1);
	cpu_flag |= (CPUInfo[3] & (1 << 26)) >> 19;	// SSE2 対応か
	cpu_flag |= (CPUInfo[2] & (1 << 9)) >> 9;	// SSSE3 対応か
	cpu_flag |= (CPUInfo[2] & (1 << 19)) >> 18;	// SSE4.1 対応か
	cpu_flag |= (CPUInfo[2] & (1 << 20)) >> 18;	// SSE4.2 対応か
	cpu_flag |= (CPUInfo[2] & (1 << 1)) << 2;	// CLMUL 対応か
	if ((CPUInfo[2] & (1 << 28)) != 0){	// AVX 対応なら
		wchar_t path[MAX_PATH];
		byteOffset = 0;
		returnLength = GetModuleFileName(NULL, path, MAX_PATH);	// 実行ファイルと同じ場所に DLL が存在するか
		if ((returnLength > 0) && (returnLength < MAX_PATH - 17)){
			while (path[returnLength] != '\\')
				returnLength--;
			returnLength++;
#ifdef _WIN64	// 64-bit 版なら
			wcscpy(path + returnLength, L"par2j64_extra.dll");
#else
			wcscpy(path + returnLength, L"par2j_extra.dll");
#endif
			//printf("DLL path = '%S'\n", path);
			// DLL の PE checksum を検証する
			returnLength = MapFileAndCheckSum(path, &cache_limit, &cache_size);
			if ((returnLength == CHECKSUM_SUCCESS) && (cache_limit == cache_size)){
				byteOffset = GetVersion();
				byteOffset = ((byteOffset & 0x00FF) << 8) | ((byteOffset & 0xFF00) >> 8);
				//printf("OS version = %0X\n", byteOffset);
			}
		}
		if (byteOffset >= 0x0601){	// Windows 7 以降なら AVX2 の判定をする
			__cpuid(CPUInfo, 0);
			if (CPUInfo[0] >= 7){	// AVX2 用の基本命令領域があるなら
				__cpuidex(CPUInfo, 7, 0);
				cpu_flag |= (CPUInfo[1] & (1 << 5)) >> 1;	// AVX2 対応か
			}
		}
	}

	// 使用可能なコア個数とキャッシュ性能を調べる
	if (GetProcessAffinityMask(GetCurrentProcess(), (PDWORD_PTR)&ProcessAffinityMask, (PDWORD_PTR)&returnLength) != 0){
		cpu_num = count_bit(ProcessAffinityMask);
		//printf("ProcessAffinityMask = 0x%08x, available logical processor cores = %d\n\n", ProcessAffinityMask, cpu_num);
	}
	if (cpu_num <= 0){	// プロセスが使用可能なコア個数の取得に失敗したら総数を使う
		SYSTEM_INFO si;
		ProcessAffinityMask = 0xFFFFFFFF;
		GetSystemInfo(&si);
		cpu_num = si.dwNumberOfProcessors;
		//printf("total logical processor cores = %d\n\n", cpu_num);
	}
	core_count = cache_limit = 0;

	// CPU 情報を取得する
	returnLength = 0;
	if (GetLogicalProcessorInformation(buffer, &returnLength) == FALSE){
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER){
			buffer = malloc(returnLength);
			if (buffer != NULL){
				if (GetLogicalProcessorInformation(buffer, &returnLength) == FALSE){
					free(buffer);
					buffer = NULL;
				}
			}
		}
	}

	// 項目ごとに解析する
	if (buffer != NULL){
		byteOffset = 0;
		ptr = buffer;
		while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength){
			//printf("Relationship = %d, ProcessorMask = 0x%08x\n", ptr->Relationship, ptr->ProcessorMask);
			if (ptr->ProcessorMask & ProcessAffinityMask){
				switch (ptr->Relationship){
				case RelationProcessorCore:
					//printf("ProcessorCore.Flags = %d, ProcessorMask = 0x%08x\n", ptr->ProcessorCore.Flags, ptr->ProcessorMask);
					if (ptr->ProcessorCore.Flags){
						// Logical processors share functional units, as in Hyperthreading or SMT.
						core_count += 1;
					} else {
						// Logical processors do not share functional units.
						core_count += count_bit((unsigned int)(ptr->ProcessorMask));
					}
					break;
				case RelationCache:
					// Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache.
					if ((ptr->Cache.Level >= 2) && (ptr->Cache.Associativity != 0) && ((ptr->Cache.Type & 1) == 0)){
						//printf("Cache: Level = %d, Size = %d KB, Associativity = %d, Type = %d, Mask = 0x%02x\n", ptr->Cache.Level, ptr->Cache.Size >> 10, ptr->Cache.Associativity, ptr->Cache.Type, ptr->ProcessorMask);
						cache_size = ptr->Cache.Size / ptr->Cache.Associativity;
						if (cache_limit < cache_size)
							cache_limit = cache_size;
					}
					break;
				}
			}
			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			ptr++;
		}
		free(buffer);
		//printf("Number of available physical processor cores: %d\n", core_count);
		//printf("Limit size of Cache Blocking: %d KB\n", cache_limit >> 10);
	}

	if ((cache_limit == 0) && ((GetVersion() & 0xFF) < 6)){	// Windows Vista 以前なら CPUID を参照する
		// CPU ベンダー情報を識別する
		__cpuid(CPUInfo, 0);
		if ((CPUInfo[1] == 0x756E6547) && (CPUInfo[2] == 0x6C65746E) && (CPUInfo[3] == 0x49656E69)){
			// BIOS 設定で CPUID を制限してる場合や、
			// Pentium 3 や Pentium 4 の初期版 (Willamette & Northwood) では
			// Windows OS はキャッシュ・サイズを取得できない。
			unsigned char *ids;
			//printf("Intel\n");	//  Intel 専用情報
			__cpuid(CPUInfo, 2);
			//printf("CPUID #2: 0x%08X, 0x%08X, 0x%08X, 0x%08X\n", CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
			if (CPUInfo[0] & 0x80000000){
				CPUInfo[0] = 0;
			} else {
				CPUInfo[0] &= 0xFFFFFF00;
			}
			if (CPUInfo[1] & 0x80000000)
				CPUInfo[1] = 0;
			if (CPUInfo[2] & 0x80000000)
				CPUInfo[2] = 0;
			if (CPUInfo[3] & 0x80000000)
				CPUInfo[3] = 0;
			ids = (unsigned char *)&(CPUInfo[0]);
			for (byteOffset = 0; byteOffset < 16; byteOffset++){
				if (ids[byteOffset] != 0){
					cache_size = 0;
					switch (ids[byteOffset]){
					case 0x41:
					case 0x7A:
					case 0x82:
						cache_size = 32 << 10;	// 32 KB
						break;
					case 0x42:
					case 0x7B:
					case 0x83:
						cache_size = 64 << 10;	// 64 KB
						break;
					case 0x22:
					case 0x23:
					case 0x43:
					case 0x78:
					case 0x7C:
					case 0x84:
					case 0x86:
					case 0x87:
						cache_size = 128 << 10;	// 128 KB
						break;
					case 0x25:
					case 0x44:
					case 0x7D:
					case 0x85:
						cache_size = 256 << 10;	// 256 KB
						break;
					case 0x29:
					case 0x45:
						cache_size = 512 << 10;	// 512 KB
						break;
					}
					if (cache_limit < cache_size)
						cache_limit = cache_size;
				}
			}

		} else if ((CPUInfo[1] == 0x68747541) && (CPUInfo[2] == 0x444D4163) && (CPUInfo[3] == 0x69746E65)){
			//printf("AMD\n");	// AMD 専用情報
			__cpuid(CPUInfo, 0x80000000);
			//printf("max extended CPUID = 0x%08X\n", CPUInfo[0]);
			if (CPUInfo[0] >= 0x80000008){
				if (core_count == 0){
					__cpuid(CPUInfo, 0x80000008);
					core_count = (CPUInfo[2] & 0xFF) + 1;
					//printf("core count = %d (0x%08X)\n", core_count, CPUInfo[2]);
					if (core_count > cpu_num)	// 利用コア数までにする
						core_count = cpu_num;
				}
				__cpuid(CPUInfo, 0x80000006);
				//printf("L2 cache info = 0x%08x, size = %d KB, associativity = %d\n", CPUInfo[2], CPUInfo[2] >> 16, count_associativity(CPUInfo[2] >> 12));
				//printf("L3 cache info = 0x%08x, size = %d KB, associativity = %d\n", CPUInfo[3], (CPUInfo[3] >> 18) * 512, count_associativity(CPUInfo[3] >> 12));
				cache_size = ((CPUInfo[2] >> 16) << 10) / count_associativity(CPUInfo[2] >> 12);
				if (cache_limit < cache_size)
					cache_limit = cache_size;
				cache_size = ((CPUInfo[3] >> 18) << 19) / count_associativity(CPUInfo[3] >> 12);
				if (cache_limit < cache_size)
					cache_limit = cache_size;
			}
		}
		//printf("Limit size of Cache Blocking: %d KB\n", cache_limit >> 10);
	}

	if (core_count == 0){	// 物理コア数が不明なら、論理コア数と同じにする
		core_count = cpu_num;
		use_count = cpu_num;
	} else if (core_count < cpu_num){
		// 物理コア数が共有されてるなら、Hyper-Threading による論理コアを半分だけ使う
		use_count = core_count + (cpu_num - core_count) / 2;
	} else {
		use_count = core_count;
	}
	if (use_count > MAX_CPU)	// 利用するコア数が実装上の制限を越えないようにする
		use_count = MAX_CPU;
	//printf("Core count: logical, physical, use = %d, %d, %d\n", cpu_num, core_count, use_count);
	// 上位に論理コア数と物理コア数、下位に利用するコア数を配置する
	cpu_num = (cpu_num << 24) | (core_count << 16) | use_count;

	// cpu_flag の上位 17-bit にキャッシュの制限サイズを置く
	cpu_flag |= cache_limit & ~0x00007FFF;	// 32 KB 未満は無視する
}

// 空きメモリー量と制限値から使用できるメモリー量を計算する
size_t get_mem_size(size_t trial_alloc)
{
	size_t mem_size;
	MEMORYSTATUSEX statex;

	statex.dwLength = sizeof(statex);
	if (GlobalMemoryStatusEx(&statex)){
		unsigned __int64 mem_size64, cache_size;
/*
		printf("MemoryLoad    = %d\n", statex.dwMemoryLoad);
		printf("TotalPhys     = %10I64d\n", statex.ullTotalPhys);
		printf("AvailPhys     = %10I64d\n", statex.ullAvailPhys);
		printf("TotalPageFile = %10I64d\n", statex.ullTotalPageFile);
		printf("AvailPageFile = %10I64d\n", statex.ullAvailPageFile);
		printf("TotalVirtual  = %10I64d\n", statex.ullTotalVirtual);
		printf("AvailVirtual  = %10I64d\n", statex.ullAvailVirtual);
*/
		cache_size = statex.ullAvailPhys >> 3;	// available size / 8
		if (memory_use){	// manual setting
			mem_size64 = cache_size * memory_use;	// using size = available size / 8 * memory_use
		} else {	// auto setting (mostly for 32-bit OS)
			// disk cache size is range from 100MB to 200MB.
			cache_size += statex.ullTotalPhys >> 4;	// cache size = available size / 8 + total size / 16
			if (cache_size > statex.ullAvailPhys >> 2)
				cache_size = statex.ullAvailPhys >> 2;	// cache size should be less than 1/4 of available size.
			mem_size64 = statex.ullAvailPhys - cache_size;	// using size = available size - cache size
		}

		if (mem_size64 + 0x00200000 > statex.ullAvailVirtual)
			mem_size64 = statex.ullAvailVirtual - 0x00200000;	// keep 2MB for other task
#ifndef _WIN64	// 32-bit 版なら
		if (mem_size64 >= MAX_MEM_SIZE)	// 確保する最大サイズを 2GB までにする
			mem_size = MAX_MEM_SIZE;
#endif
		mem_size = (size_t)mem_size64 & ~(size_t)0xFFFF;	// multiple of 64KB

	} else {
		print_win32_err();
		mem_size = 0x20000000;	// メモリー量を認識できない場合は 512MB と仮定する
	}

	// try to allocate for test
	if (trial_alloc){
		unsigned char *tmp_p;
		if (mem_size > trial_alloc)	// no need to test over than required size
			mem_size = trial_alloc;
		//printf("\nget_mem_size: try %d MB ... ", mem_size >> 20);
		tmp_p = malloc(mem_size);
		while ((tmp_p == NULL) && (mem_size > 0x08000000)){	// if over than 128MB
#ifndef _WIN64	// 32-bit 版なら
			mem_size -= 0x01000000;	// reduce 16MB
#else
			mem_size -= 0x02000000;	// reduce 32MB
#endif
			tmp_p = malloc(mem_size);
		}
		if (tmp_p != NULL)
			free(tmp_p);
		//printf("%d MB ok\n", mem_size >> 20);
	}

	return mem_size;
}

// 記録装置の特性を調べるために、次の記事を参考にした。
//「SSD なら動作を変えるアプリケーションを作る」by NyaRuRu
//「SSDかどうかをC#から判別する 」by EMO

// Returns 0 if |physical_drive_path| has no seek penalty.
// Returns 1 otherwise.
// Returns 2 or 3 if fails to retrieve the status.
int check_seek_penalty(wchar_t *dir_path){	// ディレクトリを指定する
	wchar_t *drive_letter, logical_drive[8];
	unsigned int return_value, return_size;
	HANDLE handle;
	STORAGE_PROPERTY_QUERY query_seek_penalty;
	DEVICE_SEEK_PENALTY_DESCRIPTOR query_seek_penalty_desc;

	// ドライブ文字を取得する
	drive_letter = dir_path;
	if (wcsncmp(dir_path, L"\\\\?\\", PREFIX_LEN) == 0)
		drive_letter += PREFIX_LEN;
	if (drive_letter[1] != ':')
		return 2;	// 「C:\〜」のような形式にだけ対応する
	if ((drive_letter[0] >= 0x41) && (drive_letter[0] <= 0x5A)){	// 大文字の A〜Z なら
		logical_drive[4] = drive_letter[0];
	} else if ((drive_letter[0] >= 0x61) && (drive_letter[0] <= 0x7A)){	// 小文字の a〜z なら
		logical_drive[4] = drive_letter[0];
	} else {
		return 2;	// ドライブ文字でないならだめ
	}
	// "\\\\.\\C:" のフォーマットにする
	logical_drive[0] = '\\';
	logical_drive[1] = '\\';
	logical_drive[2] = '.';
	logical_drive[3] = '\\';
	logical_drive[5] = ':';
	logical_drive[6] = 0;

	// We do not need write permission.
	handle = CreateFile(logical_drive, FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return 2;

	return_size = 0;
	query_seek_penalty.PropertyId = 7;	// StorageDeviceSeekPenaltyProperty
	query_seek_penalty.QueryType = 0;	// PropertyStandardQuery
	return_value = DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY,
			&query_seek_penalty, sizeof(query_seek_penalty),
			&query_seek_penalty_desc, sizeof(query_seek_penalty_desc),
			&return_size, NULL);
	CloseHandle(handle);
	if (return_value == 0)
		return 3;	// failed to retrieve data.
/*	printf("Version = %d, Size = %d, IncursSeekPenalty = %d\n",
		query_seek_penalty_desc.Version,
		query_seek_penalty_desc.Size,
		query_seek_penalty_desc.IncursSeekPenalty);*/
	if (query_seek_penalty_desc.Size != sizeof(query_seek_penalty_desc))
		return 3;

	if (query_seek_penalty_desc.IncursSeekPenalty != 0)
		return 1;
	return 0;	// no seek penalty = SSD drive ?
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define MAX_NAME_LEN	69	// 経過表示のタイトルの最大文字数 (末尾の null 文字を含む)

int prog_last;	// 前回と同じ進捗状況は出力しないので記録しておく
int count_last;

// ファイル・パスを短縮されたファイル名だけにしてコピーする
// ASCII 文字は 1文字, それ以外は 2文字として数えることに注意！
static void copy_filename(wchar_t *out, wchar_t *in)
{
	int i, len, name_off, ext_off, ext_len;

	// ファイル名部分の文字数を数える
	name_off = 0;
	ext_off = 0;
	len = 0;
	for (i = 0; in[i] != 0; i++){
		if ((in[i] == '\\') && (in[i + 1] != 0)){
			len = 0;
			name_off = i + 1;
		} else {
			if (in[i] < 0x80){
				len += 1;
				if ((in[i] == '.') && (in[i + 1] != 0))
					ext_off = i;
			} else {
				len += 2;
				ext_off = 0;	// 拡張子は ASCII文字だけにする
			}
		}
	}
	if (len < MAX_NAME_LEN){
		wcscpy(out, in + name_off);
		return;
	}

	// 拡張子の長さを調べる
	ext_len = 0;
	if (ext_off > name_off){
		while (in[ext_off + ext_len] != 0)
			ext_len++;
		if (ext_len >= EXT_LEN)
			ext_len = 0;
	}

	// ファイル名が長すぎる場合は、末尾を省いて拡張子を追加する
	len = 0;
	for (i = 0; in[name_off + i] != 0; i++){
		if (in[name_off + i] < 0x80){
			if (len >= MAX_NAME_LEN - 2 - ext_len)
				break;
			len += 1;
		} else {
			if (len >= MAX_NAME_LEN - 3 - ext_len)
				break;
			len += 2;
		}
		out[i] = in[name_off + i];
	}
	out[i++] = '~';
	while (ext_len > 0){
		out[i++] = in[ext_off++];
		ext_len--;
	}
	out[i] = 0;
}

// 経過のパーセントを表示する
// 普段は 0 を返す、キャンセル時は 0以外
int print_progress(int prog_now)	// 表示する % 値
{
	if (prog_now < 0)	// 範囲外なら
		return 0;

	if (_kbhit()){	// キー入力があるか
		int ch = _getch();
		if ((ch == 'c') || (ch == 'C')){	// Cancel
			printf("\nCancel\n");
			return 2;
		}

		// 一時停止と再開の処理
		if ((ch == 'p') || (ch == 'P')){	// Pause
			printf(" Pause\r");	// パーセントを上書きする
			do {
				ch = _getch();	// 再度入力があるまで待つ、CPU 占有率 0%
				if ((ch == 'c') || (ch == 'C')){	// 停止中でもキャンセルは受け付ける
					printf("\nCancel\n");
					return 2;
				}
			} while ((ch != 'r') && (ch != 'R'));	// Resume
			prog_last = -1;	// Pause の文字を上書きする
		}
	}

	// 前回と同じ進捗状況は出力しない
	if (prog_now == prog_last)
		return 0;
	printf("%3d.%d%%\r", prog_now / 10, prog_now % 10);
	prog_last = prog_now;
	fflush(stdout);

	return 0;
}

// 経過のパーセントとテキストを表示する
void print_progress_text(int prog_now, char *text)
{
	if (prog_now < 0)	// 範囲外なら
		return;
	printf("%3d.%d%% : %s\r", prog_now / 10, prog_now % 10, text);
	prog_last = prog_now;
	fflush(stdout);
}

// 経過のパーセントや個数やファイル名を表示する
// 個数はマイナスなら表示しない
int print_progress_file(int prog_now, int count_now, wchar_t *file_name)
{
	if ((count_now >= 0) && (count_now != count_last)){
		printf("%d \r", count_now);	// 個数を表示する
		count_last = count_now;
		if (prog_now == prog_last)
			prog_last = prog_now + 1;	// 個数の文字を上書きする
	}

	if (prog_now < 0)	// 範囲外なら
		return 0;

	if (_kbhit()){	// キー入力があるか
		int ch = _getch();
		if ((ch == 'c') || (ch == 'C')){	// Cancel
			if (prog_last >= 0)
				printf("\n");
			printf("Cancel\n");
			return 2;
		}

		// 一時停止と再開の処理
		if ((ch == 'p') || (ch == 'P')){	// Pause
			printf(" Pause\r");	// パーセントを上書きする
			do {
				ch = _getch();	// 再度入力があるまで待つ、CPU 占有率 0%
				if ((ch == 'c') || (ch == 'C')){	// 停止中でもキャンセルは受け付ける
					printf("\nCancel\n");
					return 2;
				}
			} while ((ch != 'r') && (ch != 'R'));	// Resume
			if (prog_now == prog_last)
				prog_last = prog_now + 1;	// Pause の文字を上書きする
		}
	}

	if (prog_last < 0){	// 初めて経過を表示する時だけファイル名を表示する
		char text[MAX_NAME_LEN * 3];
		wchar_t short_name[MAX_NAME_LEN];

		copy_filename(short_name, file_name);
		utf16_to_cp(short_name, text, cp_output);
		printf("%3d.%d%% : \"%s\"\r", prog_now / 10, prog_now % 10, text);
	} else if (prog_now == prog_last){	// 前回と同じ進捗状況は出力しない
		return 0;
	} else {
		printf("%3d.%d%%\r", prog_now / 10, prog_now % 10);
	}
	prog_last = prog_now;
	fflush(stdout);

	return 0;
}

void print_progress_done(void)	// 終了と改行を表示する
{
	if (prog_last >= 0){	// そもそも経過表示がなかった場合は表示しない
		if (prog_last != 1000){
			printf("100.0%%\n");
		} else {
			printf("\n");
		}
		fflush(stdout);
		prog_last = -1;	// 進捗状況をリセットする
	}
}

// キャンセルと一時停止を行う
int cancel_progress(void)
{
	if (_kbhit()){	// キー入力があるか
		int ch = _getch();
		if ((ch == 'c') || (ch == 'C')){	// Cancel
			printf("Cancel\n");
			return 2;
		}

		// 一時停止と再開の処理
		if ((ch == 'p') || (ch == 'P')){	// Pause
			printf(" Pause\r");
			do {
				ch = _getch();	// 再度入力があるまで待つ、CPU 占有率 0%
				if ((ch == 'c') || (ch == 'C')){	// 停止中でもキャンセルは受け付ける
					printf("Cancel\n");
					return 2;
				}
			} while ((ch != 'r') && (ch != 'R'));	// Resume
		}
	}

	return 0;
}

// Win32 API のエラー・メッセージを表示する
void print_win32_err(void)
{
	LPVOID lpMsgBuf = NULL;

	if (cp_output == CP_UTF8){
		if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), 0, (LPWSTR) &lpMsgBuf, 0, NULL) > 0){
			char buf[MAX_LEN * 3];
			// エンコードを UTF-16 から UTF-8 に変換する
			utf16_to_utf8(lpMsgBuf, buf);
			printf("\n%s", buf);
		}
	} else {
		if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), 0, (LPSTR) &lpMsgBuf, 0, NULL) > 0){
			printf("\n%s", lpMsgBuf);
		}
	}
	if (lpMsgBuf != NULL)
		LocalFree(lpMsgBuf);
}

// ファイルをゴミ箱に移す
int delete_file_recycle(wchar_t *file_path)
{
	wchar_t temp_path[MAX_PATH + PREFIX_LEN];
	int rv;
	SHFILEOPSTRUCT FileOp;

	// "\\?\" が付いてると ShellAPI が動作しないので取り除く
	rv = len_without_prefix(file_path);
	if (rv >= MAX_PATH - 1){	// パスの文字数が MAX_PATH - 1 以下でないといけない
		// 短いパス名に変換してみる
		rv = GetShortPathName(file_path, temp_path, MAX_PATH + PREFIX_LEN);
		if ((rv == 0) || (rv >= MAX_PATH + PREFIX_LEN))
			return 1;	// パスが長すぎで短縮できない？
		// 更にプリフィックスを取り除く
		rv = copy_without_prefix(temp_path, temp_path);
		//printf("\n short path = '%S'\n", temp_path);
	} else {
		rv = copy_without_prefix(temp_path, file_path);
	}
	temp_path[rv + 1] = 0;	// 末尾にもう一個 null 文字を追加する

	memset(&FileOp, 0, sizeof(FileOp));
	FileOp.wFunc = FO_DELETE;
	FileOp.pFrom = temp_path;
	FileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	rv = SHFileOperation(&FileOp);
	if (FileOp.fAnyOperationsAborted == TRUE)
		rv = 1;	// 削除がキャンセルされた
//	if (rv != 0)
//		printf("\n SHFileOperation: error = %d\n", rv);
	return rv;
}

