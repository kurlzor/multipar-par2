// create.c
// Copyright : 2015-08-25 Yutaka Sawada
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

#include <stdio.h>

#include <windows.h>

#include "common2.h"
#include "phmd5.h"
#include "md5_crc.h"
#include "version.h"
#include "create.h"


// ソート時に項目を比較する
static int sort_cmp(const void *elem1, const void *elem2)
{
	const file_ctx_c *file1, *file2;
	int i;

	file1 = elem1;
	file2 = elem2;

	// サイズが 0 の空ファイルやフォルダは non-recovery set に含める
	if (file1->size == 0){
		if (file2->size > 0)
			return 1;
	} else {
		if (file2->size == 0)
			return -1;
	}

	// File ID の順に並び替える
	for (i = 15; i >= 0; i--){
		if (file1->id[i] > file2->id[i]){
			return 1; // 1 > 2
		} else if (file1->id[i] < file2->id[i]){
			return -1; // 1 < 2
		}
	}
	return 0; // 1 = 2
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// PAR 2.0 のパケット・ヘッダーを作成する
void set_packet_header(
	unsigned char *buf,			// ヘッダーを格納する 64バイトのバッファー
	unsigned char *set_id,		// Recovery Set ID、16バイト
	int type_num,				// そのパケットの型番号
	unsigned int body_size)	// パケットのデータ・サイズ
{
	// Magic sequence
	memcpy(buf, "PAR2\0PKT", 8);
	// Length of the entire packet
	body_size += 64;	// パケット・サイズにする
	memcpy(buf + 8, &body_size, 4);
	memset(buf + 12, 0, 4);
	// MD5 Hash of packet は後で計算する
	// Recovery Set ID
	memcpy(buf + 32, set_id, 16);
	// Type
	memcpy(buf + 48, "PAR 2.0\0", 8);
	memset(buf + 56, 0, 8);
	switch (type_num){
	case 1:		// Main packet
		memcpy(buf + 56, "Main", 4);
		break;
	case 2:		// File Description packet
		memcpy(buf + 56, "FileDesc", 8);
		break;
	case 3:		// Input File Slice Checksum packet
		memcpy(buf + 56, "IFSC", 4);
		break;
	case 4:		// Recovery Slice packet
		memcpy(buf + 56, "RecvSlic", 8);
		break;
	case 5:		// Creator packet
		memcpy(buf + 56, "Creator", 7);
		break;
	case 10:	// Unicode Filename packet
		memcpy(buf + 56, "UniFileN", 8);
		break;
	case 11:	// ASCII Comment packet
		memcpy(buf + 56, "CommASCI", 8);
		break;
	case 12:	// Unicode Comment packet
		memcpy(buf + 56, "CommUni", 7);
		break;
	}
}

// ソース・ファイルの情報を集める
int get_source_files(file_ctx_c *files)
{
	unsigned char work_buf[16 + 8 + (MAX_LEN * 3)];
	unsigned char hash0[16] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
							0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
	wchar_t file_name[MAX_LEN], file_path[MAX_LEN];
	int i, list_off = 0, num, len;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	wcscpy(file_path, base_dir);
	for (num = 0; num < file_num; num++){
		files[num].name = list_off;	// ファイル名の位置を記録する
		wcscpy(file_name, list_buf + list_off);
		//list_off += (wcslen(file_name) + 1);
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;

		// 属性を調べる
		wcscpy(file_path + base_len, file_name);
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			printf_cp("GetFileAttributesEx, %s\n", file_name);
			return 1;
		}
		if (AttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){	// フォルダなら
			memcpy(files[num].hash, hash0, 16);
			files[num].size = 0;
		} else {	// ファイルなら
			files[num].size = ((__int64)AttrData.nFileSizeHigh << 32) | (unsigned __int64)AttrData.nFileSizeLow;
			if (files[num].size == 0){	// 空のファイルなら
				memcpy(files[num].hash, hash0, 16);
			} else {
				// ファイルの先頭 16KB 分のハッシュ値を計算する
				if (file_md5_16(file_path, files[num].hash)){
					printf_cp("file_md5_16, %s\n", file_name);
					return 1;
				}
			}
		}

		// File ID を計算する
		memcpy(work_buf, files[num].hash, 16);
		memcpy(work_buf + 16, &(files[num].size), 8);
		unix_directory(file_name);	// 記録時のディレクトリ記号は「/」にする
		utf16_to_utf8(file_name, work_buf + 24);
		len = (int)strlen(work_buf + 24);	// File ID 計算時のファイル名のサイズは 4の倍数にしない
		data_md5(work_buf, 24 + len, files[num].id);
		// 他のファイルと同じ File ID になってないか調べる
		do {
			for (i = 0; i < num; i++){
				if (memcmp(files[num].id, files[i].id, 16) == 0){	// File ID が同じなら
					files[num].id[0] += 1;	// 最下位バイトを変化させる
					break;
				}
			}
		} while (i < num);	// File ID を必ずユニークな値にする
	}

	return 0;
}

// 共通パケットを作成する
int set_common_packet(
	unsigned char *buf,
	int *packet_num,			// 共通パケットの数
	int switch_u,				// ユニコードのファイル名も記録する
	file_ctx_c *files)
{
	char ascii_buf[MAX_LEN * 3];
	unsigned char set_id[16], file_hash[16], *main_packet_buf;
	unsigned char hash0[16] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
							0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
	wchar_t file_name[MAX_LEN], file_path[MAX_LEN];
	int i, err, off, off2, num, len, data_size, main_packet_size;
	__int64 prog_now = 0;

	printf("\n");
	print_progress_text(0, "Computing file hash");
	wcscpy(file_path, base_dir);

	// ファイルを File ID の順に並び替える
	qsort(files, file_num, sizeof(file_ctx_c), sort_cmp);

	// 最初に Main packet を作成しておく
	main_packet_size = 12 + (file_num * 16);
	main_packet_buf = (unsigned char *)malloc(main_packet_size);
	if (main_packet_buf == NULL){
		printf("malloc, %d\n", main_packet_size);
		return 1;
	}
	memcpy(main_packet_buf, &block_size, 4);
	memset(main_packet_buf + 4, 0, 4);
	memcpy(main_packet_buf + 8, &entity_num, 4);
	for (num = 0; num < file_num; num++)	// 並び替えられた順序で File ID をコピーする
		memcpy(main_packet_buf + (12 + (16 * num)), files[num].id, 16);
	data_md5(main_packet_buf, main_packet_size, set_id);	// Recovery Set ID を計算する
	*packet_num = 1;

	// ファイルごとのパケットを作成する
	off = 0;
	for (num = 0; num < file_num; num++){
		// File Description packet
		off2 = off;	// 位置を記録しておく
		memcpy(buf + (off + 64), files[num].id, 16);
		memcpy(buf + (off + 64 + 32), files[num].hash, 16);	// 全体のハッシュ値は後で書き込む
		memcpy(buf + (off + 64 + 48), &(files[num].size), 8);
		// ファイル名を UTF-8 に変換する
		wcscpy(file_name, list_buf + files[num].name);
		wcscpy(file_path + base_len, file_name);
		unix_directory(file_name);	// 記録時のディレクトリ記号は「/」にする
		utf16_to_utf8(file_name, ascii_buf);
		len = (int)strlen(ascii_buf);
		for (i = len + 1; (i < len + 4) || (i < MAX_LEN * 3); i++)
			ascii_buf[i] = 0;
		len = (len + 3) & 0xFFFFFFFC;
		memcpy(buf + (off + 64 + 56), ascii_buf, len);
		data_size = 56 + len;
		set_packet_header(buf + off, set_id, 2, data_size);	// パケット・ヘッダーを作成する
		//data_md5(buf + (off + 32), 32 + data_size, buf + (off + 16));	// パケットの MD5 を計算する
		off += (64 + data_size);
		(*packet_num)++;

		if (files[num].size > 0){	// Input File Slice Checksum packet
			// QuickPar はなぜかサイズが 0 のファイルに対してもチェックサムのパケットが必要・・・
			// 無意味なパケットを避けるために non-recovery set を使った方がいい?
			memcpy(buf + (off + 64), files[num].id, 16);
			// ファイルの MD5 ハッシュ値とブロックのチェックサムを同時に計算する
			err = file_hash_crc(file_path, files[num].size, file_hash, buf + (off + 64 + 16), &prog_now);
			if (err){
				if (err == 1)
					printf_cp("file_hash_crc, %s\n", file_name);
				off = err;
				goto error_end;
			}
			len = (int)((files[num].size + (__int64)block_size - 1) / block_size);
			// ファイルの MD5-16k はもう不要なので、ブロックの CRC-32 に変更する
			memset(files[num].hash, 0, 16);
			for (i = 0; i < len; i++){	// XOR して 16バイトに減らす
				memcpy(&err, buf + (off + 64 + 16 + 20 * i + 16), 4);
				((unsigned int *)files[num].hash)[i & 3] ^= err;
			}
//			printf("%d: 0x%08X %08X %08X %08X\n", num,
//				((unsigned int *)files[num].hash)[0], ((unsigned int *)files[num].hash)[1],
//				((unsigned int *)files[num].hash)[2], ((unsigned int *)files[num].hash)[3]);
			i = 16 + 20 * len;	// パケット内容のサイズ
			set_packet_header(buf + off, set_id, 3, i);	// パケット・ヘッダーを作成する
			data_md5(buf + (off + 32), 32 + i, buf + (off + 16));	// パケットの MD5 を計算する
			off += (64 + i);
			(*packet_num)++;
		} else {
			memcpy(file_hash, hash0, 16);
		}

		// File Description packet の続き
		memcpy(buf + (off2 + 64 + 16), file_hash, 16);	// 計算したハッシュ値をここで書き込む
		data_md5(buf + (off2 + 32), 32 + data_size, buf + (off2 + 16));	// パケットの MD5 を計算する

		if (switch_u != 0){	// ユニコードのファイル名
			// 指定があってもファイル名が ASCII 文字だけならユニコードのパケットは作らない
			len = (int)wcslen(file_name);
			for (i = 0; i < len; i++){
				if (file_name[i] != ascii_buf[i])
					break;
			}
			if (i < len){
				// Unicode Filename packet
				memcpy(buf + (off + 64), files[num].id, 16);
				len = (int)wcslen(file_name) * 2;
				memcpy(buf + (off + 64 + 16), file_name, len);
				if (len & 3)
					memset(buf + (off + 64 + 16 + len), 0, 2);
				data_size = 16 + ((len + 3) & 0xFFFFFFFC);
				set_packet_header(buf + off, set_id, 10, data_size);	// パケット・ヘッダーを作成する
				data_md5(buf + (off + 32), 32 + data_size, buf + (off + 16));	// パケットの MD5 を計算する
				off += (64 + data_size);
				(*packet_num)++;
			}
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	// Main packet
	set_packet_header(buf + off, set_id, 1, main_packet_size);	// パケット・ヘッダーを作成する
	memcpy(buf + (off + 64), main_packet_buf, main_packet_size);
	data_md5(buf + (off + 32), 32 + main_packet_size, buf + (off + 16));	// パケットの MD5 を計算する
	off += (64 + main_packet_size);

error_end:
	free(main_packet_buf);
	return off;
}

// ソース・ファイルの情報から共通パケットのサイズを計算する
int measure_common_packet(
	int *packet_num,			// 共通パケットの数
	int switch_u)				// ユニコードのファイル名も記録する
{
	char ascii_buf[MAX_LEN * 3];
	wchar_t *file_name, file_path[MAX_LEN];
	int i, list_off = 0, off = 0, len, data_size;
	__int64 file_size;
	WIN32_FILE_ATTRIBUTE_DATA AttrData;

	wcscpy(file_path, base_dir);

	// 最初に Main packet を作成しておく
	data_size = 12 + (file_num * 16);
	off += (64 + data_size);
	*packet_num = 1;

	// Item index for debug output
	if (split_size == 0){
		printf("\n         Size  Slice :  Filename\n");
	} else {
		printf("\n         Size  Slice Split :  Filename\n");
	}

	// ファイルごとのパケットを作成する
	while (list_off < list_len){
		file_name = list_buf + list_off;
		//list_off += (wcslen(file_name) + 1);
		while (list_buf[list_off] != 0)
			list_off++;
		list_off++;

		// 属性を調べる
		wcscpy(file_path + base_len, file_name);
		if (!GetFileAttributesEx(file_path, GetFileExInfoStandard, &AttrData)){
			print_win32_err();
			printf_cp("GetFileAttributesEx, %s\n", file_name);
			return 1;
		}
		if (AttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){	// フォルダなら
			file_size = 0;
		} else {	// ファイルなら
			file_size = ((__int64)AttrData.nFileSizeHigh << 32) | (unsigned __int64)AttrData.nFileSizeLow;
		}
		// Line for debug output
		i = 0;
		if (file_size > 0)
			i = (int)((file_size + block_size - 1) / block_size);	// ブロック数
		printf("%13I64d %6d", file_size, i);
		if (split_size != 0){
			i = (int)((file_size + split_size - 1) / split_size);	// 分割数
			printf(" %5d", i);
		}
		printf_cp(" : \"%s\"\n", file_name);

		// File Description packet
		// ファイル名を UTF-8 に変換する
		utf16_to_utf8(file_name, ascii_buf);
		len = (int)strlen(ascii_buf);
		len = (len + 3) & 0xFFFFFFFC;
		data_size = 56 + len;
		off += (64 + data_size);
		(*packet_num)++;

		if (file_size > 0){	// Input File Slice Checksum packet
			data_size = 16;
			data_size += (int)(((file_size + block_size - 1) / block_size) * 20);
			off += (64 + data_size);
			(*packet_num)++;
		}

		if (switch_u != 0){	// ユニコードのファイル名
			// 指定があってもファイル名が ASCII 文字だけならユニコードのパケットは作らない
			len = (int)wcslen(file_name);
			for (i = 0; i < len; i++){
				if (file_name[i] != ascii_buf[i]){
					break;
				}
			}
			if (i < len){
				// Unicode Filename packet
				len = (int)wcslen(file_name) * 2;
				data_size = 16 + ((len + 3) & 0xFFFFFFFC);
				off += (64 + data_size);
				(*packet_num)++;
			}
		}
	}

	return off;
}

// 末尾パケットを作成する
int set_footer_packet(
	unsigned char *buf,
	wchar_t *par_comment,		// コメント
	unsigned char *set_id)		// Recovery Set ID
{
	unsigned char ascii_buf[COMMENT_LEN * 3];
	int off = 0, len, data_size;

	if (par_comment[0] != 0){	// ASCII と Unicode のどちらか一方だけを記録する
		data_size = 0;
		// 7-bit ASCII 文字以外が存在するかどうかを調べる
		len = 0;
		while (par_comment[len] != 0){
			if ((par_comment[len] & 0xFF80) != 0){
				data_size = 3;
				break;
			}
			len++;
		}
		if (data_size == 0){
			// ASCII への変換に失敗した場合はユニコード・パケットを記録する
			if (!WideCharToMultiByte(CP_ACP, 0, par_comment, -1, ascii_buf, COMMENT_LEN * 3, NULL, &len) || len){
				data_size = 1;
			} else if (wcslen(par_comment) != strlen(ascii_buf)){	// 文字数とバイト数が一致しないなら
				data_size = 2;
			}
		}
		if (data_size == 0){
			// ASCII Comment packet
			len = (int)strlen(ascii_buf);
			memcpy(buf + (off + 64), ascii_buf, len);
			if (len & 3)
				memset(buf + (off + 64 + len), 0, 3);
			data_size = (len + 3) & 0xFFFFFFFC;
			set_packet_header(buf + off, set_id, 11, data_size);	// パケット・ヘッダーを作成する
			data_md5(buf + (off + 32), 32 + data_size, buf + (off + 16));	// パケットの MD5 を計算する
			off += (64 + data_size);
		} else {
			// Unicode Comment packet
			memset(buf + (off + 64), 0, 16);
			len = (int)wcslen(par_comment) * 2;
			memcpy(buf + (off + 64 + 16), par_comment, len);
			if (len & 3)
				memset(buf + (off + 64 + 16 + len), 0, 2);
			data_size = 16 + ((len + 3) & 0xFFFFFFFC);
			set_packet_header(buf + off, set_id, 12, data_size);	// パケット・ヘッダーを作成する
			data_md5(buf + (off + 32), 32 + data_size, buf + (off + 16));	// パケットの MD5 を計算する
			off += (64 + data_size);
		}
	}

	// Creator packet
	sprintf(ascii_buf, "par2j v%x.%x.%x\0", CLIENT_VERSION >> 12,
		(CLIENT_VERSION >> 8) & 0xF, (CLIENT_VERSION >> 4) & 0xF);
	len = (int)strlen(ascii_buf);
	memcpy(buf + (off + 64), ascii_buf, len);
	if (len & 3)
		memset(buf + (off + 64 + len), 0, 3);
	data_size = (len + 3) & 0xFFFFFFFC;
	//strcpy(buf + (off + 64), "par2j v1.1.6\0");
	//data_size = 12;
	set_packet_header(buf + off, set_id, 5, data_size);	// パケット・ヘッダーを作成する
	data_md5(buf + (off + 32), 32 + data_size, buf + (off + 16));	// パケットの MD5 を計算する
	off += (64 + data_size);

	return off;
}

// 末尾パケットのサイズを計算する
int measure_footer_packet(
	wchar_t *par_comment)		// コメント
{
	unsigned char ascii_buf[COMMENT_LEN * 3];
	int off = 0, len, data_size;

	if (par_comment[0] != 0){	// ASCII と Unicode のどちらか一方だけを記録する
		data_size = 0;
		// ASCII への変換に失敗した場合はユニコード・パケットを記録する
		if (!WideCharToMultiByte(CP_ACP, 0, par_comment, -1, ascii_buf, COMMENT_LEN * 3, NULL, &len) || len){
			data_size = 1;
		} else if (wcslen(par_comment) != strlen(ascii_buf)){	// 文字数とバイト数が一致しないなら
			data_size = 2;
		}

		if (data_size == 0){
			// ASCII Comment packet
			len = (int)strlen(ascii_buf);
			data_size = (len + 3) & 0xFFFFFFFC;
			off += (64 + data_size);
		} else {
			// Unicode Comment packet
			len = (int)wcslen(par_comment) * 2;
			data_size = 16 + ((len + 3) & 0xFFFFFFFC);
			off += (64 + data_size);
		}
	}

	// Creator packet
	//len = strlen("par2j v*.*.*\0");
	//data_size = (len + 3) & 0xFFFFFFFC;
	off += (64 + 12);

	return off;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// ボリューム番号の桁数を求める
static int calc_max_num(
	int block_distri,	// パリティ・ブロックの分配方法 (3-bit目は番号の付け方)
	int *max_num2)		// 最大スライス個数の桁数
{
	int i, j, num;
	int exp_num, block_start, block_count, block_start_max, block_count_max = 0;

	if (block_distri >> 2){	// ファイル番号にする、vol_1, vol_2, vol_3, ...
		i = recovery_num;
		for (j = 1; j < 10; j++){
			i /= 10;
			if (i == 0)
				break;
		}
		return j;	// ファイル数の桁数
	}

	block_start = 0;
	block_count = 0;
	exp_num = 1;
	for (num = 0; num < recovery_num; num++){
		// リカバリ・ファイルのファイル名
		switch (block_distri & 3){
		case 0:	// 同じ数なら
			block_count = parity_num / recovery_num;
			j = parity_num % recovery_num;	// 割り切れない場合は
			if ((j > 0) && (num < j))
				block_count++;
			break;
		case 1:	// 倍々で異なる数にする
			j = ((exp_num * parity_num) + (recovery_limit / 2)) / recovery_limit;
			if (j <= block_count)
				j = block_count + 1;	// 必ず先のとは異なる
			block_count = j;
			if (num + 1 == recovery_num){
				if (block_start + block_count != parity_num)
					block_count = parity_num - block_start;
			}
			exp_num *= 2;
			break;
		case 2:	// 1,2,4,8,16 と2の乗数なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				exp_num *= 2;
			}
			break;
		case 3:	// 1,1,2,5,10,10,20,50 という decimal weights sizing scheme なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				switch (num % 4){
				case 1:
				case 3:
					exp_num = exp_num * 2;
					break;
				case 2:
					exp_num = (exp_num / 2) * 5;
					break;
				}
			}
			break;
		}
		if (block_start + block_count > parity_num)
			block_count = parity_num - block_start;
		if (block_count_max < block_count)
			block_count_max = block_count;
		block_start += block_count;
	}
	block_start_max = first_num + block_start - block_count;	// 最後に足した分を引く
	for (j = 1; j < 10; j++){
		block_start_max /= 10;
		if (block_start_max == 0)
			break;
	}
	block_start_max = j;
	for (j = 1; j < 10; j++){
		block_count_max /= 10;
		if (block_count_max == 0)
			break;
	}
	*max_num2 = j;

	return block_start_max;
}

// ランダムに書き込むには管理者権限が必要
//#define RANDOM_ACCESS

// リカバリ・ファイルを作成して共通パケットをコピーする
int create_recovery_file(
	wchar_t *recovery_path,		// 作業用
	int packet_limit,			// リカバリ・ファイルのパケット繰り返しの制限
	int block_distri,			// パリティ・ブロックの分配方法 (3-bit目は番号の付け方)
	int packet_num,				// 共通パケットの数
	unsigned char *common_buf,	// 共通パケットのバッファー
	int common_size,			// 共通パケットのバッファー・サイズ
	unsigned char *footer_buf,	// 末尾パケットのバッファー
	int footer_size,			// 末尾パケットのバッファー・サイズ
	HANDLE *rcv_hFile,			// 各リカバリ・ファイルのハンドル
	parity_ctx_c *p_blk)		// 各パリティ・ブロックの情報
{
	wchar_t recovery_base[MAX_LEN], file_ext[EXT_LEN], *tmp_p;
	int i, j, num;
	int exp_num, block_start, block_count, block_start_max, block_count_max;
	int repeat_max, packet_to, packet_from, packet_size, common_off;
	unsigned int time_last, prog_num = 0;
	__int64 file_off;
#ifdef RANDOM_ACCESS
	HANDLE hToken;
#endif

	print_progress_text(0, "Constructing recovery file");
	time_last = GetTickCount();

#ifdef RANDOM_ACCESS
	// SE_MANAGE_VOLUME_NAME 権限を有効にする
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken) != 0){
		LUID luid;
		if (LookupPrivilegeValue(NULL, SE_MANAGE_VOLUME_NAME, &luid ) != 0){
			TOKEN_PRIVILEGES tp;
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Luid = luid;
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL);
			if (GetLastError() != ERROR_SUCCESS){
				print_win32_err();
			} else {
				printf("\nSE_MANAGE_VOLUME_NAME: ok\n");
			}
		}
		CloseHandle(hToken);
	}
#endif

	// リカバリ・ファイルの拡張子には指定されたものを使う
	file_ext[0] = 0;
	wcscpy(recovery_base, recovery_file);
	tmp_p = offset_file_name(recovery_base);
	tmp_p = wcsrchr(tmp_p, '.');
	if (tmp_p != NULL){
		if (wcslen(tmp_p) < EXT_LEN){
			wcscpy(file_ext, tmp_p);	// 拡張子を記録しておく
			*tmp_p = 0;	// 拡張子を取り除く
		}
	}

	// ボリューム番号の桁数を求める
	block_start_max = calc_max_num(block_distri, &block_count_max);

	// リカバリ・ファイルを作成して共通パケットを書き込む
	block_start = 0;
	block_count = 0;
	exp_num = 1;
	for (num = 0; num < recovery_num; num++){
		// リカバリ・ファイルのファイル名
		switch (block_distri & 3){
		case 0:	// 同じ数なら
			block_count = parity_num / recovery_num;
			j = parity_num % recovery_num;	// 割り切れない場合は
			if ((j > 0) && (num < j))
				block_count++;
			break;
		case 1:	// 倍々で異なる数にする
			j = ((exp_num * parity_num) + (recovery_limit / 2)) / recovery_limit;
			if (j <= block_count)
				j = block_count + 1;	// 必ず先のとは異なる
			block_count = j;
			if (num + 1 == recovery_num){
				if (block_start + block_count != parity_num)
					block_count = parity_num - block_start;
			}
			exp_num *= 2;
			break;
		case 2:	// 1,2,4,8,16 と2の乗数なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				exp_num *= 2;
			}
			break;
		case 3:	// 1,1,2,5,10,10,20,50 という decimal weights sizing scheme なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				switch (num % 4){
				case 1:
				case 3:
					exp_num = exp_num * 2;
					break;
				case 2:
					exp_num = (exp_num / 2) * 5;
					break;
				}
			}
			break;
		}
		if (block_start + block_count > parity_num)
			block_count = parity_num - block_start;
		if (block_distri >> 2){
			// ファイル番号にする、vol_1, vol_2, vol_3, ...
			swprintf(recovery_path, MAX_LEN, L"%s.vol_%0*d%s", recovery_base, block_start_max, num + 1, file_ext);
		} else {
			// QuickPar方式、volXX+YY
			swprintf(recovery_path, MAX_LEN, L"%s.vol%0*d+%0*d%s", recovery_base, block_start_max,
				first_num + block_start, block_count_max, block_count, file_ext);
		}
		// リカバリ・ファイルを開く
		//move_away_file(recovery_path);	// 既存のファイルをどかす
		rcv_hFile[num] = CreateFile(recovery_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (rcv_hFile[num] == INVALID_HANDLE_VALUE){
			print_win32_err();
			rcv_hFile[num] = NULL;
			printf_cp("cannot create file, %s\n", recovery_path);
			return 1;
		}

		// パケットの繰り返し回数を計算する
		repeat_max = 1;
		for (j = 2; j <= block_count; j *= 2)	// 繰り返し回数は log2(block_count)
			repeat_max++;
		if ((packet_limit > 0) && (repeat_max > packet_limit))
			repeat_max = packet_limit;	// 繰り返し回数を制限する
		// リカバリ・ファイルの大きさを計算する
		file_off = (__int64)(68 + block_size) * block_count;
		file_off += (common_size * repeat_max) + footer_size;
		// そのサイズにする
		if (!SetFilePointerEx(rcv_hFile[num], *((PLARGE_INTEGER)&file_off), NULL, FILE_BEGIN)){
			print_win32_err();
			return 1;
		}
		if (!SetEndOfFile(rcv_hFile[num])){
			print_win32_err();
			return 1;
		}
#ifdef RANDOM_ACCESS
		if (!SetFileValidData(rcv_hFile[num], file_off)){
			print_win32_err();
			return 1;
		}
#endif

		file_off = 0;
		repeat_max *= packet_num;	// リカバリ・ファイルの共通パケットの数
		packet_from = 0;
		common_off = 0;

		// Recovery Slice packet は後から書き込む
		for (j = block_start; j < block_start + block_count; j++){
			prog_num++;
			// 経過表示
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((prog_num * 1000) / parity_num))
					return 2;
				time_last = GetTickCount();
			}

			// Recovery Slice packet
			file_off += 68;
			p_blk[j].file = num;
			p_blk[j].off = file_off;	// 開始位置だけ記録しておく
			file_off += block_size;

			// どれだけの共通パケットを書き込むか
			packet_size = 0;
			packet_to = (int)((__int64)repeat_max * (j - block_start + 1) / block_count);
			while (packet_to - packet_from > 0){
				memcpy(&i, common_buf + (common_off + (packet_size + 8)), 4);	// そのパケットのデータ・サイズを調べる
				packet_size += i;
				packet_from++;
			}
			if (packet_size > 0){	// 共通パケットを書き込む
				if (file_write_data(rcv_hFile[num], file_off, common_buf + common_off, packet_size)){
					printf_cp("file_write_data, %s\n", recovery_path);
					return 1;
				}
				// オフセットが半分を超えたら戻しておく
				common_off += packet_size;
				if (common_off >= common_size)
					common_off -= common_size;
				file_off += packet_size;
			}
		}
		block_start += block_count;

		// 末尾パケットを書き込む
		if (file_write_data(rcv_hFile[num], file_off, footer_buf, footer_size)){
			printf_cp("file_write_data, %s\n", recovery_path);
			return 1;
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	return 0;
}

// 作成中のリカバリ・ファイルを削除する
void delete_recovery_file(
	wchar_t *recovery_path,		// 作業用
	int block_distri,			// パリティ・ブロックの分配方法 (3-bit目は番号の付け方)
	int switch_p,				// インデックス・ファイルを作らない
	HANDLE *rcv_hFile)			// 各リカバリ・ファイルのハンドル
{
	wchar_t recovery_base[MAX_LEN], file_ext[EXT_LEN], *tmp_p;
	int j, num;
	int exp_num, block_start, block_count, block_start_max, block_count_max;

	if (switch_p == 0)
		DeleteFile(recovery_file);	// インデックス・ファイルを削除する

	// リカバリ・ファイルの拡張子には指定されたものを使う
	file_ext[0] = 0;
	wcscpy(recovery_base, recovery_file);
	tmp_p = offset_file_name(recovery_base);
	tmp_p = wcsrchr(tmp_p, '.');
	if (tmp_p != NULL){
		if (wcslen(tmp_p) < EXT_LEN){
			wcscpy(file_ext, tmp_p);	// 拡張子を記録しておく
			*tmp_p = 0;	// 拡張子を取り除く
		}
	}

	// ボリューム番号の桁数を求める
	block_start_max = calc_max_num(block_distri, &block_count_max);

	// リカバリ・ファイルを削除する
	block_start = 0;
	block_count = 0;
	exp_num = 1;
	for (num = 0; num < recovery_num; num++){
		// リカバリ・ファイルのファイル名
		switch (block_distri & 3){
		case 0:	// 同じ数なら
			block_count = parity_num / recovery_num;
			j = parity_num % recovery_num;	// 割り切れない場合は
			if ((j > 0) && (num < j))
				block_count++;
			break;
		case 1:	// 倍々で異なる数にする
			j = ((exp_num * parity_num) + (recovery_limit / 2)) / recovery_limit;
			if (j <= block_count)
				j = block_count + 1;	// 必ず先のとは異なる
			block_count = j;
			if (num + 1 == recovery_num){
				if (block_start + block_count != parity_num)
					block_count = parity_num - block_start;
			}
			exp_num *= 2;
			break;
		case 2:	// 1,2,4,8,16 と2の乗数なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				exp_num *= 2;
			}
			break;
		case 3:	// 1,1,2,5,10,10,20,50 という decimal weights sizing scheme なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				switch (num % 4){
				case 1:
				case 3:
					exp_num = exp_num * 2;
					break;
				case 2:
					exp_num = (exp_num / 2) * 5;
					break;
				}
			}
			break;
		}
		if (block_start + block_count > parity_num)
			block_count = parity_num - block_start;
		if (block_distri >> 2){
			// ファイル番号にする、vol_1, vol_2, vol_3, ...
			swprintf(recovery_path, MAX_LEN, L"%s.vol_%0*d%s", recovery_base, block_start_max, num + 1, file_ext);
		} else {
			// QuickPar方式、volXX+YY
			swprintf(recovery_path, MAX_LEN, L"%s.vol%0*d+%0*d%s", recovery_base, block_start_max,
				first_num + block_start, block_count_max, block_count, file_ext);
		}
		block_start += block_count;

		if (rcv_hFile[num] != NULL){	// ファイルが作成済みなら (開いていれば)
			CloseHandle(rcv_hFile[num]);
			rcv_hFile[num] = NULL;
			DeleteFile(recovery_path);	// 途中までのリカバリ・ファイルを削除する
		}
	}
}

// リカバリ・ファイルのサイズを計算する
void measure_recovery_file(
	wchar_t *recovery_path,		// 作業用 (最初はコメントが入ってる)
	int packet_limit,			// リカバリ・ファイルのパケット繰り返しの制限
	int block_distri,			// パリティ・ブロックの分配方法
	int packet_num,				// 共通パケットの数
	int common_size,			// 共通パケットのバッファー・サイズ
	int footer_size,			// 末尾パケットのバッファー・サイズ
	int switch_p)				// インデックス・ファイルを作らない
{
	char ascii_buf[MAX_LEN * 3];
	wchar_t recovery_base[MAX_LEN], file_ext[EXT_LEN], *tmp_p;
	int j, num;
	int exp_num, block_start, block_count, block_start_max, block_count_max;
	int footer_num, packet_count, repeat_max;
	__int64 file_size;

	get_file_name(recovery_file, recovery_base);	// ファイル名だけにする
	footer_num = 1;
	if (recovery_path[0] != 0)
		footer_num++;

	total_file_size = 0;	// リカバリ・ファイルの合計サイズを計算する
	if (switch_p == 0){
		// Index File を先に表示する
		file_size = common_size + footer_size;
		total_file_size += file_size;
		packet_count = packet_num + footer_num;
		utf16_to_cp(recovery_base, ascii_buf, cp_output);
		printf("%13I64d %7d      0 : \"%s\"\n", file_size, packet_count, ascii_buf);
	}
	if (parity_num == 0)	// パリティ・ブロックを作らない場合はここで終わる
		return;

	// リカバリ・ファイルの拡張子には指定されたものを使う
	file_ext[0] = 0;
	tmp_p = offset_file_name(recovery_base);
	tmp_p = wcsrchr(tmp_p, '.');
	if (tmp_p != NULL){
		if (wcslen(tmp_p) < EXT_LEN){
			wcscpy(file_ext, tmp_p);	// 拡張子を記録しておく
			*tmp_p = 0;	// 拡張子を取り除く
		}
	}

	// ボリューム番号の桁数を求める
	block_start_max = calc_max_num(block_distri, &block_count_max);

	// リカバリ・ファイルを作成して共通パケットを書き込む
	block_start = 0;
	block_count = 0;
	exp_num = 1;
	for (num = 0; num < recovery_num; num++){
		// リカバリ・ファイルのファイル名
		switch (block_distri & 3){
		case 0:	// 同じ数なら
			block_count = parity_num / recovery_num;
			j = parity_num % recovery_num;	// 割り切れない場合は
			if ((j > 0) && (num < j))
				block_count++;
			break;
		case 1:	// 倍々で異なる数にする
			j = ((exp_num * parity_num) + (recovery_limit / 2)) / recovery_limit;
			if (j <= block_count)
				j = block_count + 1;	// 必ず先のとは異なる
			block_count = j;
			if (num + 1 == recovery_num){
				if (block_start + block_count != parity_num)
					block_count = parity_num - block_start;
			}
			exp_num *= 2;
			break;
		case 2:	// 1,2,4,8,16 と2の乗数なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				exp_num *= 2;
			}
			break;
		case 3:	// 1,1,2,5,10,10,20,50 という decimal weights sizing scheme なら
			block_count = exp_num;
			if (block_count >= recovery_limit){
				block_count = recovery_limit;
			} else {
				switch (num % 4){
				case 1:
				case 3:
					exp_num = exp_num * 2;
					break;
				case 2:
					exp_num = (exp_num / 2) * 5;
					break;
				}
			}
			break;
		}
		if (block_start + block_count > parity_num)
			block_count = parity_num - block_start;
		if (block_distri >> 2){
			// ファイル番号にする、vol_1, vol_2, vol_3, ...
			swprintf(recovery_path, MAX_LEN, L"%s.vol_%0*d%s", recovery_base, block_start_max, num + 1, file_ext);
		} else {
			// QuickPar方式、volXX+YY
			swprintf(recovery_path, MAX_LEN, L"%s.vol%0*d+%0*d%s", recovery_base, block_start_max,
				first_num + block_start, block_count_max, block_count, file_ext);
		}
		utf16_to_cp(recovery_path, ascii_buf, cp_output);

		// パケットの繰り返し回数を計算する
		repeat_max = 1;
		for (j = 2; j <= block_count; j *= 2)	// 繰り返し回数は log2(block_count)
			repeat_max++;
		if ((packet_limit > 0) && (repeat_max > packet_limit))
			repeat_max = packet_limit;	// 繰り返し回数を制限する
		// リカバリ・ファイルの大きさを計算する
		file_size = (__int64)(68 + block_size) * block_count;
		file_size += (common_size * repeat_max) + footer_size;
		total_file_size += file_size;
		packet_count = repeat_max * packet_num;	// リカバリ・ファイルの共通パケットの数
		packet_count += footer_num + block_count;
		printf("%13I64d %7d %6d : \"%s\"\n", file_size, packet_count, block_count, ascii_buf);

		block_start += block_count;
	}
}

// ソース・ファイルを分割する (分割サイズは split_size で指定する)
int split_files(
	file_ctx_c *files,
	int *cur_num, int *cur_id)	// エラー発生時は、その時点でのファイル番号と分割番号が戻る
{
	unsigned char buf[IO_SIZE];
	wchar_t file_path[MAX_LEN], recovery_dir[MAX_LEN];
	int num, id, split_num, split_max, total_num, dir_len;
	unsigned int rv, len, split_left;
	unsigned int time_last, prog_num = 0;
	__int64 num8, file_left;
	HANDLE hFile, hFile_src;

	// 分割したファイルをどこに保存するか
	get_base_dir(recovery_file, recovery_dir);
	if (compare_directory(base_dir, recovery_dir) == 0){	// 分割先が同じ場所ならコピーしない
		recovery_dir[0] = 0;
		dir_len = base_len;
	} else {
		dir_len = (int)wcslen(recovery_dir);
		//printf_cp("\n save_path = %s \n", recovery_dir);
	}

	// うまく分割できるか確かめる
	total_num = 0;
	for (num = 0; num < file_num; num++){
		if (files[num].size > (__int64)split_size){
			num8 = (files[num].size + (__int64)split_size - 1) / split_size;
			if (num8 > 99999){	// 分割個数が 99999を超えたらエラー
				printf("too many split file, %d\n", num);
				*cur_num = -1;
				*cur_id = 0;
				return 1;
			}
			total_num += (int)num8;
		} else if (recovery_dir[0] != 0){	// 分割先が異なる場合だけコピーする
			total_num++;
		}
	}
	if (total_num == 0)
		return 0;	// 分割する必要なし

	// 分割サイズよりも大きなファイルだけ分割する
	printf("\n");
	print_progress_text(0, "Spliting file");
	time_last = GetTickCount();
	for (num = 0; num < file_num; num++){
		if (files[num].size > (__int64)split_size){	// 分割する
			file_left = files[num].size;
			num8 = (file_left + (__int64)split_size - 1) / split_size;
			split_num = (int)num8;
			if (split_num <= 999){
				split_max = 3;	// 標準では 001〜999 の 999分割までだから 3桁
			} else if (split_num <= 9999){
				split_max = 4;
			} else {	// ソース・ブロックは 32768個以下だから分割数もそれ以下のはず
				split_max = 5;
			}

			// ソース・ファイルを開く
			wcscpy(file_path, base_dir);
			wcscpy(file_path + base_len, list_buf + files[num].name);
			hFile_src = CreateFile(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (hFile_src == INVALID_HANDLE_VALUE){
				print_win32_err();
				printf_cp("cannot open file, %s\n", list_buf + files[num].name);
				*cur_num = num;
				*cur_id = 0;
				return 1;
			}
			if (recovery_dir[0] != 0)
				wcscpy(file_path, recovery_dir);	// 異なる場所に保存する
			for (id = 1; id <= split_num; id++){
				swprintf(file_path + dir_len, _countof(file_path) - dir_len, L"%s.%0*d", list_buf + files[num].name, split_max, id);
				hFile = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile == INVALID_HANDLE_VALUE){
					print_win32_err();
					CloseHandle(hFile_src);
					printf_cp("cannot create file, %s\n", file_path);
					*cur_num = num;
					*cur_id = id - 1;
					return 1;
				}
				// ファイルを分割サイズごとに書き込んでいく
				split_left = split_size;
				if ((__int64)split_left > file_left)
					split_left = (unsigned int)file_left;
				file_left -= split_left;
				while (split_left){
					len = IO_SIZE;
					if (split_left < IO_SIZE)
						len = split_left;
					split_left -= len;
					if (!ReadFile(hFile_src, buf, len, &rv, NULL) || (len != rv)){
						print_win32_err();
						CloseHandle(hFile_src);
						CloseHandle(hFile);
						printf("ReadFile, input file %d\n", num);
						*cur_num = num;
						*cur_id = id;
						return 1;
					}
					if (!WriteFile(hFile, buf, len, &rv, NULL)){
						print_win32_err();
						CloseHandle(hFile_src);
						CloseHandle(hFile);
						printf("WriteFile, split file %d.%03d", num, id);
						*cur_num = num;
						*cur_id = id;
						return 1;
					}
				}
				CloseHandle(hFile);

				// 経過表示
				prog_num++;
				if (GetTickCount() - time_last >= UPDATE_TIME){
					if (print_progress((prog_num * 1000) / total_num)){
						CloseHandle(hFile_src);
						*cur_num = num;
						*cur_id = id;
						return 2;
					}
					time_last = GetTickCount();
				}
			}

		} else if (recovery_dir[0] != 0){	// 小さなファイルはそのままコピーする
			// ソース・ファイルのパス
			wcscpy(file_path, base_dir);
			wcscpy(file_path + base_len, list_buf + files[num].name);
			// コピー先のパス
			wcscpy(recovery_dir + dir_len, list_buf + files[num].name);
			rv = CopyFile(file_path, recovery_dir, FALSE);
			if (rv != 0){	// 読み取り専用属性を解除しておく
				len = GetFileAttributes(recovery_dir);
				if ((len & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY)) != 0){
					len &= ~(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY);
					rv = SetFileAttributes(recovery_dir, len);
				}
			}
			if (rv == 0){
				printf_cp("cannot copy file, %s\n", recovery_dir);
				*cur_num = num;
				*cur_id = 0;
				return 1;
			}
			recovery_dir[dir_len] = 0;	// ディレクトリに戻しておく

			// 経過表示
			prog_num++;
			if (GetTickCount() - time_last >= UPDATE_TIME){
				if (print_progress((prog_num * 1000) / total_num)){
					*cur_num = num;
					*cur_id = 0;
					return 2;
				}
				time_last = GetTickCount();
			}
		}
	}
	print_progress_done();	// 改行して行の先頭に戻しておく

	return 0;
}

// 分割されたソース・ファイルを削除する
void delete_split_files(
	file_ctx_c *files,
	int max_num, int max_id)	// エラー発生時のファイル番号と分割番号
{
	wchar_t file_path[MAX_LEN], recovery_dir[MAX_LEN];
	int num, id, split_num, split_max, dir_len;
	__int64 num8, file_left;

	// 分割したファイルをどこに保存するか
	get_base_dir(recovery_file, recovery_dir);
	if (compare_directory(base_dir, recovery_dir) == 0){	// 分割先が同じ場所ならコピーしない
		recovery_dir[0] = 0;
		dir_len = base_len;
	} else {
		dir_len = (int)wcslen(recovery_dir);
	}

	// 分割サイズよりも大きなファイルだけ分割する
	for (num = 0; num <= max_num; num++){
		if (files[num].size > (__int64)split_size){	// 分割されたファイルを削除する
			file_left = files[num].size;
			num8 = (file_left + (__int64)split_size - 1) / split_size;
			split_num = (int)num8;	// 分割個数は 99999 以下のはず
			if (split_num <= 999){
				split_max = 3;	// 標準では 001〜999 の 999分割までだから 3桁
			} else if (split_num <= 9999){
				split_max = 4;
			} else if (split_num <= 99999){
				split_max = 5;
			}
			if (num == max_num)
				split_num = max_id;	// 中断した所まで削除する
			for (id = 1; id <= split_num; id++){
				if (recovery_dir[0] != 0){
					wcscpy(file_path, recovery_dir);	// 異なる場所に保存した
				} else {
					wcscpy(file_path, base_dir);
				}
				swprintf(file_path + dir_len, _countof(file_path) - dir_len, L"%s.%0*d", list_buf + files[num].name, split_max, id);
				if (!DeleteFile(file_path))	// 既に分割してるソース・ファイルを削除する
					return;	// 削除に失敗したら出る
			}

		} else if (recovery_dir[0] != 0){	// 別の場所にコピーした場合だけ削除する
			wcscpy(recovery_dir + dir_len, list_buf + files[num].name);	// コピー先のパス
			if (!DeleteFile(recovery_dir))	// コピーしたソース・ファイルを削除する
				return;	// 削除に失敗したら出る
			recovery_dir[dir_len] = 0;	// ディレクトリに戻しておく
		}
	}
}
