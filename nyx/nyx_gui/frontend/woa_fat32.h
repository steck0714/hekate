/*
 * Minimal FAT32 formatter for the Windows on ARM EFI System Partition (ESP).
 *
 * Hekate's f_mkfs() can only format the first (HOS) partition, so the ESP gets
 * its filesystem from this small header-only formatter. 512B sectors only.
 *
 * The caller provides a write callback (0 = success) and a scratch buffer.
 * That keeps this code independent of the SD driver and testable on a PC.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef WOA_FAT32_H
#define WOA_FAT32_H

#include <utils/types.h>

#define WOA_FAT32_MIN_SCT 204800 // 100MB. Windows minimum for a 512B sector ESP.

typedef int (*woa_wr_fn)(void *ctx, u32 lba, u32 cnt, const void *buf);

static inline void _woa_w16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static inline void _woa_w32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static inline void _woa_zero(u8 *p, u32 len)
{
	for (u32 i = 0; i < len; i++)
		p[i] = 0;
}

/*
 * Formats [start_lba, start_lba + sectors) as FAT32.
 * zbuf: scratch buffer of zbuf_sct * 512 bytes (gets zeroed here).
 * Returns 0 on success, negative on error.
 */
static int woa_fat32_format(woa_wr_fn wr, void *ctx, u32 start_lba, u32 sectors,
	u32 vol_id, u8 *zbuf, u32 zbuf_sct)
{
	if (sectors < WOA_FAT32_MIN_SCT || !zbuf_sct)
		return -1;

	// Pick the biggest cluster size that still keeps FAT32 cluster count valid (>= 65525).
	u32 spc = 8;
	while (spc > 1 && (sectors - 32) / spc < 70000)
		spc >>= 1;

	u32 fat_sct    = ((sectors / spc + 2) * 4 + 511) / 512;
	u32 data_start = 32 + 2 * fat_sct;
	u32 clusters   = (sectors - data_start) / spc;
	if (clusters < 65525)
		return -2;

	// Zero reserved area, both FATs and the root directory cluster (cluster 2).
	_woa_zero(zbuf, zbuf_sct * 512);
	u32 zero_total = data_start + spc;
	for (u32 done = 0; done < zero_total; )
	{
		u32 n = zero_total - done;
		if (n > zbuf_sct)
			n = zbuf_sct;
		if (wr(ctx, start_lba + done, n, zbuf))
			return -3;
		done += n;
	}

	u8 sec[512];

	// Boot sector.
	_woa_zero(sec, 512);
	sec[0] = 0xEB; sec[1] = 0x58; sec[2] = 0x90;
	const char oem[] = "MSDOS5.0";
	for (u32 i = 0; i < 8; i++)
		sec[3 + i] = oem[i];
	_woa_w16(&sec[11], 512);          // Bytes per sector.
	sec[13] = spc;                    // Sectors per cluster.
	_woa_w16(&sec[14], 32);           // Reserved sectors.
	sec[16] = 2;                      // Number of FATs.
	sec[21] = 0xF8;                   // Media: fixed disk.
	_woa_w16(&sec[24], 63);           // Sectors per track.
	_woa_w16(&sec[26], 255);          // Heads.
	_woa_w32(&sec[28], start_lba);    // Hidden sectors.
	_woa_w32(&sec[32], sectors);      // Total sectors.
	_woa_w32(&sec[36], fat_sct);      // Sectors per FAT.
	_woa_w32(&sec[44], 2);            // Root cluster.
	_woa_w16(&sec[48], 1);            // FSInfo sector.
	_woa_w16(&sec[50], 6);            // Backup boot sector.
	sec[64] = 0x80;                   // Drive number.
	sec[66] = 0x29;                   // Extended boot signature.
	_woa_w32(&sec[67], vol_id);
	const char label[] = "ESP        ";
	for (u32 i = 0; i < 11; i++)
		sec[71 + i] = label[i];
	const char fstype[] = "FAT32   ";
	for (u32 i = 0; i < 8; i++)
		sec[82 + i] = fstype[i];
	sec[510] = 0x55; sec[511] = 0xAA;
	if (wr(ctx, start_lba + 0, 1, sec) || wr(ctx, start_lba + 6, 1, sec))
		return -4;

	// FSInfo sector.
	_woa_zero(sec, 512);
	_woa_w32(&sec[0],   0x41615252);
	_woa_w32(&sec[484], 0x61417272);
	_woa_w32(&sec[488], clusters - 1); // Free clusters (root dir uses one).
	_woa_w32(&sec[492], 3);            // Next free cluster hint.
	_woa_w32(&sec[508], 0xAA550000);
	if (wr(ctx, start_lba + 1, 1, sec) || wr(ctx, start_lba + 7, 1, sec))
		return -5;

	// First FAT sector of both FATs.
	_woa_zero(sec, 512);
	_woa_w32(&sec[0], 0x0FFFFFF8); // Media descriptor.
	_woa_w32(&sec[4], 0x0FFFFFFF); // Reserved.
	_woa_w32(&sec[8], 0x0FFFFFFF); // Root directory: end of chain.
	if (wr(ctx, start_lba + 32, 1, sec) || wr(ctx, start_lba + 32 + fat_sct, 1, sec))
		return -6;

	// Root directory: volume label entry (matches the boot sector label).
	_woa_zero(sec, 512);
	for (u32 i = 0; i < 11; i++)
		sec[i] = label[i];
	sec[11] = 0x08; // ATTR_VOLUME_ID.
	if (wr(ctx, start_lba + data_start, 1, sec))
		return -7;

	return 0;
}

#endif
