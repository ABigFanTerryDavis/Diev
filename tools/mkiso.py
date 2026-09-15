#!/usr/bin/env python3
"""EOS OS - minimal El Torito ISO9660 generator (no xorriso needed).

Usage: mkiso.py <boot.bin> <kernel.bin> <license-file> <out.iso>

Layout (all LBAs in 2048-byte sectors):
  LBA  0-15 : zeros (system area)
  LBA 16    : Primary Volume Descriptor (root dir at ROOT_LBA)
  LBA 17    : Boot Record ("EL TORITO SPECIFICATION", catalog at LBA 19)
  LBA 18    : Volume Descriptor Set Terminator
  LBA 19    : Boot Catalog (validation + no-emulation initial entry)
  LBA 20-37 : Boot image = boot.bin + kernel.bin padded to 36864 bytes
              (BIOS loads 72 x 512B sectors to 0x7C00)
  LBA 38+   : KERNEL.BIN, LICENSE, README.TXT file extents
  LBA ..    : root directory extent, then LE + BE path tables

Directory records use plain ISO9660 uppercase names ("NAME.EXT;1").
Directory record date: fixed 2026-09-14 UTC (reproducible builds).
"""
import struct
import sys

SECTOR = 2048
BOOT_IMG_LBA = 20
BOOT_IMG_SECTORS_512 = 72  # 36864 bytes: 512 boot + 71*512 kernel area


def both16(v):
    return struct.pack('<H', v) + struct.pack('>H', v)


def both32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)


def dir_record(extent, size, flags, name_bytes, date):
    # 34 bytes base + identifier
    rec = bytearray()
    rec.append(0)  # length placeholder
    rec.append(0)  # ext attr length
    rec += both32(extent)
    rec += both32(size)
    rec += date  # 7 bytes
    rec.append(flags)
    rec.append(0)  # file unit size (interleave)
    rec.append(0)  # interleave gap
    rec += both16(1)  # volume seq
    rec.append(len(name_bytes))
    rec += name_bytes
    if len(name_bytes) % 2 == 0:
        rec.append(0)  # pad to even length... (spec: pad if needed)
    rec[0] = len(rec)
    return bytes(rec)


def main():
    boot_path, kernel_path, license_path, out_path = sys.argv[1:5]

    boot = open(boot_path, 'rb').read()
    assert len(boot) == 512, f'boot.bin must be 512 bytes, got {len(boot)}'
    assert boot[510] == 0x55 and boot[511] == 0xAA, 'missing boot signature'
    kernel = open(kernel_path, 'rb').read()
    assert len(kernel) <= 48 * 512, \
        f'kernel too big for bootloader copy window: {len(kernel)} > {48 * 512}'
    # ^^^ bootloader copies 48 sectors to [0x1000..0x7000); bigger would
    # overwrite its own stack/code. Grow past this -> stage2 loader.
    lic = open(license_path, 'rb').read()
    readme = (b'EOS v0.0.8 boot CD.\r\n'
              b'Boot image: BOOT.BIN (El Torito, no emulation).\r\n'
              b'KERNEL.BIN: flat 32-bit kernel, loaded at 0x1000.\r\n'
              b'Run: qemu-system-x86_64 -cdrom eos.iso -boot order=d\r\n')

    files = [
        (b'KERNEL.BIN;1', kernel),
        (b'LICENSE;1', lic),
        (b'README.TXT;1', readme),
    ]

    # --- extents ---
    lba = 38
    extents = []
    for _, data in files:
        extents.append((lba, len(data)))
        lba += (len(data) + SECTOR - 1) // SECTOR
    root_lba = lba
    lba += 1
    path_lba_le = lba
    lba += 1
    path_lba_be = lba
    lba += 1
    total_sectors = lba

    # --- root directory ---
    date7 = bytes([126, 8, 14, 12, 0, 0, 0])  # 2026-09-14 12:00 UTC+0
    root = bytearray()
    root += dir_record(root_lba, SECTOR, 2, b'\x00', date7)
    root += dir_record(root_lba, SECTOR, 2, b'\x01', date7)
    for (name, _), (ext, size) in zip(files, extents):
        root += dir_record(ext, size, 0, name, date7)
    root = bytes(root).ljust(SECTOR, b'\x00')

    # --- path tables (root only) ---
    pt_le = struct.pack('<BBIH', 1, 0, root_lba, 1) + b'\x00\x00'
    pt_be = struct.pack('>BBIH', 1, 0, root_lba, 1) + b'\x00\x00'
    pt_le = pt_le.ljust(SECTOR, b'\x00')
    pt_be = pt_be.ljust(SECTOR, b'\x00')
    pt_size = len(pt_le.rstrip(b'\x00')) or 12

    # --- PVD ---
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[8:8 + 32] = b'EOS_OS'.ljust(32)
    pvd[40:40 + 32] = b'EOS_OS'.ljust(32)
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)   # vol set size
    pvd[124:128] = both16(1)   # vol seq number
    pvd[128:132] = both16(SECTOR)  # logical block size
    pvd[132:140] = both32(pt_size)  # path table size
    pvd[140:144] = struct.pack('<I', path_lba_le)
    pvd[144:148] = struct.pack('<I', 0)
    pvd[148:152] = struct.pack('>I', path_lba_be)
    pvd[152:156] = struct.pack('>I', 0)
    pvd[156:156 + 34] = dir_record(root_lba, SECTOR, 2, b'\x00', date7)
    pvd[813:813 + 17] = b'2026091412000000\x00'
    pvd[830:830 + 17] = b'2026091412000000\x00'
    pvd[847:847 + 17] = b'0000000000000000\x00'
    pvd[864] = 1
    pvd[881:889] = b'        '

    # --- Boot Record ---
    br = bytearray(SECTOR)
    br[0] = 0
    br[1:6] = b'CD001'
    br[6] = 1
    br[7:7 + 23] = b'EL TORITO SPECIFICATION'
    br[71:75] = struct.pack('<I', 19)  # boot catalog LBA

    # --- Terminator ---
    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b'CD001'
    term[6] = 1

    # --- Boot Catalog ---
    cat = bytearray(SECTOR)
    # validation entry
    cat[0] = 1       # header ID
    cat[1] = 0       # x86 platform
    cat[4:4 + 24] = b'EOS OS'.ljust(24, b'\x00')
    s = sum(struct.unpack('<16H', bytes(cat[0:32])))
    cat[28:30] = struct.pack('<H', (-s) & 0xFFFF)
    cat[30] = 0x55
    cat[31] = 0xAA
    # initial/default entry
    cat[32] = 0x88   # bootable
    cat[33] = 0      # no emulation
    cat[34:36] = struct.pack('<H', 0x07C0)  # load segment
    cat[36] = 0      # system type
    cat[38:40] = struct.pack('<H', BOOT_IMG_SECTORS_512)
    cat[40:44] = struct.pack('<I', BOOT_IMG_LBA)

    # --- boot image ---
    bootimg = (boot + kernel).ljust(BOOT_IMG_SECTORS_512 * 512, b'\x00')
    assert len(bootimg) == BOOT_IMG_SECTORS_512 * 512

    # --- assemble ---
    img = bytearray(total_sectors * SECTOR)
    img[16 * SECTOR:17 * SECTOR] = pvd
    img[17 * SECTOR:18 * SECTOR] = br
    img[18 * SECTOR:19 * SECTOR] = term
    img[19 * SECTOR:20 * SECTOR] = cat
    img[BOOT_IMG_LBA * SECTOR:(BOOT_IMG_LBA + 18) * SECTOR] = bootimg
    for (ext, _), (_, data) in zip(extents, files):
        img[ext * SECTOR:ext * SECTOR + len(data)] = data
    img[root_lba * SECTOR:(root_lba + 1) * SECTOR] = root
    img[path_lba_le * SECTOR:(path_lba_le + 1) * SECTOR] = pt_le
    img[path_lba_be * SECTOR:(path_lba_be + 1) * SECTOR] = pt_be

    open(out_path, 'wb').write(img)
    print(f'eos.iso: {len(img)} bytes, {total_sectors} sectors, '
          f'boot LBA {BOOT_IMG_LBA}, kernel {len(kernel)} bytes')


if __name__ == '__main__':
    main()
