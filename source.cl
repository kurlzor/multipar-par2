void calc_table(__local uint *mtab, int id, int factor)
{
	int i, sum = 0;

	for (i = 0; i < 8; i++){
		sum = (id & (1 << i)) ? (sum ^ factor) : sum;
		factor = (factor & 0x8000) ? ((factor << 1) ^ 0x1100B) : (factor << 1);
	}
	mtab[id] = sum;

	sum = (sum << 4) ^ (((sum << 16) >> 31) & (0x1100B << 3)) ^ (((sum << 17) >> 31) & (0x1100B << 2)) ^ (((sum << 18) >> 31) & (0x1100B << 1)) ^ (((sum << 19) >> 31) &  0x1100B);
	sum = (sum << 4) ^ (((sum << 16) >> 31) & (0x1100B << 3)) ^ (((sum << 17) >> 31) & (0x1100B << 2)) ^ (((sum << 18) >> 31) & (0x1100B << 1)) ^ (((sum << 19) >> 31) &  0x1100B);

	mtab[id + 256] = sum;
}

__kernel void method0(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	__local uint mtab[512],
	int blk_num)
{
	int i, blk;
	uint sum;
	const int work_id = get_global_id(0);
	const int work_size = get_global_size(0);
	const int table_id = get_local_id(0) & 255;

	blk = get_global_id(1);
	dst += BLK_SIZE * blk;
	factors += blk_num * blk;

	for (i = work_id; i < BLK_SIZE; i += work_size)
		dst[i] = 0;

	for (blk = 0; blk < blk_num; blk++){
		calc_table(mtab, table_id, factors[blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			sum = src[i];
			dst[i] ^= mtab[(uchar)sum] ^ mtab[256 + (uchar)(sum >> 8)] ^ ((mtab[(uchar)(sum >> 16)] ^ mtab[256 + (sum >> 24)]) << 16);
		}
		src += BLK_SIZE;
		barrier(CLK_LOCAL_MEM_FENCE);
	}
}

__kernel void method1(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	__local uint mtab[512],
	int blk_num,
	int src_off)
{
	__global uint *blk_src;
	int i, blk, chk_size, remain;
	uint sum;
	const int work_id = get_global_id(0);
	const int work_size = get_global_size(0);
	const int table_id = get_local_id(0) & 255;

	src += src_off;
	blk = get_global_id(1);
	dst += BLK_SIZE * blk;
	factors += blk_num * blk;

	remain = BLK_SIZE;
	chk_size = CHK_SIZE;
	while (remain > 0){
		if (chk_size > remain)
			chk_size = remain;

		for (i = work_id; i < chk_size; i += work_size)
			dst[i] = 0;

		blk_src = src;
		for (blk = 0; blk < blk_num; blk++){
			calc_table(mtab, table_id, factors[blk]);
			barrier(CLK_LOCAL_MEM_FENCE);

			for (i = work_id; i < chk_size; i += work_size){
				sum = blk_src[i];
				dst[i] ^= mtab[(uchar)sum] ^ mtab[256 + (uchar)(sum >> 8)] ^ ((mtab[(uchar)(sum >> 16)] ^ mtab[256 + (sum >> 24)]) << 16);
			}
			blk_src += BLK_SIZE;
			barrier(CLK_LOCAL_MEM_FENCE);
		}

		src += CHK_SIZE;
		dst += CHK_SIZE;
		remain -= CHK_SIZE;
	}
}

__kernel void method2(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	__local uint mtab[512],
	int blk_num)
{
	int i, blk, pos;
	uint lo, hi, sum1, sum2;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0) & 255;

	blk = get_global_id(1);
	dst += BLK_SIZE * blk;
	factors += blk_num * blk;

	for (i = work_id; i < BLK_SIZE; i += work_size){
		dst[i    ] = 0;
		dst[i + 1] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		calc_table(mtab, table_id, factors[blk]);
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			pos = (i & ~7) + ((i & 7) >> 1);
			lo = src[pos    ];
			hi = src[pos + 4];
			sum1 = mtab[(uchar)lo] ^ mtab[256 + (uchar)hi] ^
					((mtab[(uchar)(lo >> 16)] ^ mtab[256 + (uchar)(hi >> 16)]) << 16);
			sum2 = mtab[(uchar)(lo >> 8)] ^ mtab[256 + (uchar)(hi >> 8)] ^
					((mtab[lo >> 24] ^ mtab[256 + (hi >> 24)]) << 16);
			dst[pos    ] ^= (sum1 & 0x00FF00FF) | ((sum2 & 0x00FF00FF) << 8);
			dst[pos + 4] ^= ((sum1 & 0xFF00FF00) >> 8) | (sum2 & 0xFF00FF00);
		}
		src += BLK_SIZE;
		barrier(CLK_LOCAL_MEM_FENCE);
	}
}

__kernel void method3(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	__local uint mtab[512],
	int blk_num,
	int src_off)
{
	__global uint *blk_src;
	int i, blk, chk_size, remain, pos;
	uint lo, hi, sum1, sum2;
	const int work_id = get_global_id(0) * 2;
	const int work_size = get_global_size(0) * 2;
	const int table_id = get_local_id(0) & 255;

	src += src_off;
	blk = get_global_id(1);
	dst += BLK_SIZE * blk;
	factors += blk_num * blk;

	remain = BLK_SIZE;
	chk_size = CHK_SIZE;
	while (remain > 0){
		if (chk_size > remain)
			chk_size = remain;

		for (i = work_id; i < chk_size; i += work_size){
			dst[i    ] = 0;
			dst[i + 1] = 0;
		}

		blk_src = src;
		for (blk = 0; blk < blk_num; blk++){
			calc_table(mtab, table_id, factors[blk]);
			barrier(CLK_LOCAL_MEM_FENCE);

			for (i = work_id; i < chk_size; i += work_size){
				pos = (i & ~7) + ((i & 7) >> 1);
				lo = blk_src[pos    ];
				hi = blk_src[pos + 4];
				sum1 = mtab[(uchar)lo] ^ mtab[256 + (uchar)hi] ^
						((mtab[(uchar)(lo >> 16)] ^ mtab[256 + (uchar)(hi >> 16)]) << 16);
				sum2 = mtab[(uchar)(lo >> 8)] ^ mtab[256 + (uchar)(hi >> 8)] ^
						((mtab[lo >> 24] ^ mtab[256 + (hi >> 24)]) << 16);
				dst[pos    ] ^= (sum1 & 0x00FF00FF) | ((sum2 & 0x00FF00FF) << 8);
				dst[pos + 4] ^= ((sum1 & 0xFF00FF00) >> 8) | (sum2 & 0xFF00FF00);
			}
			blk_src += BLK_SIZE;
			barrier(CLK_LOCAL_MEM_FENCE);
		}

		src += CHK_SIZE;
		dst += CHK_SIZE;
		remain -= CHK_SIZE;
	}
}

__kernel void method4(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	__local int table[16],
	int blk_num)
{
	int i, j, blk, pos, sht, mask;
	uint sum1, sum2, sum3, sum4;
	const int work_id = get_global_id(0) * 4;
	const int work_size = get_global_size(0) * 4;

	blk = get_global_id(1);
	dst += BLK_SIZE * blk;
	factors += blk_num * blk;

	for (i = work_id; i < BLK_SIZE; i += work_size){
		dst[i    ] = 0;
		dst[i + 1] = 0;
		dst[i + 2] = 0;
		dst[i + 3] = 0;
	}

	for (blk = 0; blk < blk_num; blk++){
		if (get_local_id(0) == 0){
			pos = factors[blk];
			table[0] = pos;
			for (j = 1; j < 16; j++){
				pos = (pos << 1) ^ (((pos << 16) >> 31) & 0x1100B);
				table[j] = pos;
			}
		}
		barrier(CLK_LOCAL_MEM_FENCE);

		for (i = work_id; i < BLK_SIZE; i += work_size){
			pos = (i & ~63) + 60;
			sht = 16 + ((i >> 2) & 15);

			sum1 = 0;
			sum2 = 0;
			sum3 = 0;
			sum4 = 0;
			for (j = 0; j < 16; j++){
				mask = (table[j] << sht) >> 31;
				sum1 ^= mask & src[pos    ];
				sum2 ^= mask & src[pos + 1];
				sum3 ^= mask & src[pos + 2];
				sum4 ^= mask & src[pos + 3];
				pos -= 4;
			}
			dst[i    ] ^= sum1;
			dst[i + 1] ^= sum2;
			dst[i + 2] ^= sum3;
			dst[i + 3] ^= sum4;
		}
		src += BLK_SIZE;
		barrier(CLK_LOCAL_MEM_FENCE);
	}
}

__kernel void method5(
	__global uint *src,
	__global uint *dst,
	__global ushort *factors,
	__local int table[16],
	int blk_num,
	int src_off)
{
	__global uint *blk_src;
	int i, j, blk, chk_size, remain, pos, sht, mask;
	uint sum1, sum2, sum3, sum4;
	const int work_id = get_global_id(0) * 4;
	const int work_size = get_global_size(0) * 4;

	src += src_off;
	blk = get_global_id(1);
	dst += BLK_SIZE * blk;
	factors += blk_num * blk;

	remain = BLK_SIZE;
	chk_size = CHK_SIZE;
	while (remain > 0){
		if (chk_size > remain)
			chk_size = remain;

		for (i = work_id; i < chk_size; i += work_size){
			dst[i    ] = 0;
			dst[i + 1] = 0;
			dst[i + 2] = 0;
			dst[i + 3] = 0;
		}

		blk_src = src;
		for (blk = 0; blk < blk_num; blk++){
			if (get_local_id(0) == 0){
				pos = factors[blk];
				table[0] = pos;
				for (j = 1; j < 16; j++){
					pos = (pos << 1) ^ (((pos << 16) >> 31) & 0x1100B);
					table[j] = pos;
				}
			}
			barrier(CLK_LOCAL_MEM_FENCE);

			for (i = work_id; i < chk_size; i += work_size){
				pos = (i & ~63) + 60;
				sht = 16 + ((i >> 2) & 15);

				sum1 = 0;
				sum2 = 0;
				sum3 = 0;
				sum4 = 0;
				for (j = 0; j < 16; j++){
					mask = (table[j] << sht) >> 31;
					sum1 ^= mask & blk_src[pos    ];
					sum2 ^= mask & blk_src[pos + 1];
					sum3 ^= mask & blk_src[pos + 2];
					sum4 ^= mask & blk_src[pos + 3];
					pos -= 4;
				}
				dst[i    ] ^= sum1;
				dst[i + 1] ^= sum2;
				dst[i + 2] ^= sum3;
				dst[i + 3] ^= sum4;
			}
			blk_src += BLK_SIZE;
			barrier(CLK_LOCAL_MEM_FENCE);
		}

		src += CHK_SIZE;
		dst += CHK_SIZE;
		remain -= CHK_SIZE;
	}
}
