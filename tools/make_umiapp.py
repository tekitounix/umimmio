#!/usr/bin/env python3
"""
make_umiapp.py - Create .umiapp binary with header

Usage:
    python3 make_umiapp.py input.bin output.umiapp [options]

Options:
    --name NAME         Application name (for display, max 32 chars)
    --stack SIZE        Stack size in bytes (default: 8192)
    --heap SIZE         Heap size in bytes (default: 0)
    --target TYPE       Target type: user, dev, release (default: user)
    --entry OFFSET      Entry point offset (default: 0, auto-detect)
    --sign KEY_FILE     Sign with Ed25519 private key (required for release)

The tool creates a .umiapp file with the following structure:
    - 128-byte AppHeader
    - Raw binary content

AppHeader format (128 bytes):
    [0:4]    magic           = 0x414D4955 ('UMIA')
    [4:8]    abi_version     = 1
    [8:12]   target          = 0=user, 1=dev, 2=release
    [12:16]  flags           = 0 (reserved)
    [16:20]  entry_offset    = offset to _start from header
    [20:24]  process_offset  = 0 (filled by loader)
    [24:28]  text_size       = code section size
    [28:32]  rodata_size     = rodata section size
    [32:36]  data_size       = initialized data size
    [36:40]  bss_size        = 0 (no .bss in binary)
    [40:44]  stack_size      = required stack
    [44:48]  heap_size       = required heap
    [48:52]  crc32           = CRC32 of sections
    [52:56]  total_size      = header + binary size
    [56:120] signature       = Ed25519 signature (64 bytes)
    [120:128] reserved       = 0
"""

import argparse
import struct
import sys
from pathlib import Path

# CRC32 polynomial (IEEE 802.3)
CRC32_POLY = 0xEDB88320

def crc32(data: bytes) -> int:
    """Calculate CRC32 checksum (IEEE 802.3)"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ CRC32_POLY
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF


def create_app_header(
    binary_data: bytes,
    *,
    name: str = "UmiApp",
    stack_size: int = 8192,
    heap_size: int = 0,
    target: int = 0,
    entry_offset: int = 0,
) -> bytes:
    """Create 128-byte AppHeader for .umiapp binary"""
    
    # Constants
    MAGIC = 0x414D4955  # 'UMIA'
    ABI_VERSION = 1
    FLAGS = 0
    HEADER_SIZE = 128
    
    # Sizes (simplified: all binary is treated as .text)
    text_size = len(binary_data)
    rodata_size = 0
    data_size = 0
    bss_size = 0
    
    # Entry offset includes header size
    if entry_offset == 0:
        entry_offset = HEADER_SIZE  # Default: start of binary after header
    else:
        entry_offset += HEADER_SIZE
    
    process_offset = 0  # Filled by kernel loader
    
    # Calculate CRC32 of binary content
    crc = crc32(binary_data)
    
    # Total size = header + binary
    total_size = HEADER_SIZE + len(binary_data)
    
    # Build header (must be exactly 128 bytes)
    # Format: little-endian
    header = struct.pack(
        '<'        # Little-endian
        'I'        # magic (4)
        'I'        # abi_version (4)
        'I'        # target (4)
        'I'        # flags (4)
        'I'        # entry_offset (4)
        'I'        # process_offset (4)
        'I'        # text_size (4)
        'I'        # rodata_size (4)
        'I'        # data_size (4)
        'I'        # bss_size (4)
        'I'        # stack_size (4)
        'I'        # heap_size (4)
        'I'        # crc32 (4)
        'I'        # total_size (4)
        '64s'      # signature (64)
        '8s',      # reserved (8)
        MAGIC,
        ABI_VERSION,
        target,
        FLAGS,
        entry_offset,
        process_offset,
        text_size,
        rodata_size,
        data_size,
        bss_size,
        stack_size,
        heap_size,
        crc,
        total_size,
        b'\x00' * 64,  # signature (empty for unsigned)
        b'\x00' * 8,   # reserved
    )
    
    assert len(header) == HEADER_SIZE, f"Header size mismatch: {len(header)} != {HEADER_SIZE}"
    return header


def parse_size(s: str) -> int:
    """Parse size string with K/M suffix"""
    s = s.upper().strip()
    if s.endswith('K'):
        return int(s[:-1]) * 1024
    elif s.endswith('M'):
        return int(s[:-1]) * 1024 * 1024
    else:
        return int(s)


def main():
    parser = argparse.ArgumentParser(
        description='Create .umiapp binary with header',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('input', help='Input binary file')
    parser.add_argument('output', help='Output .umiapp file')
    parser.add_argument('--name', default='UmiApp', help='Application name')
    parser.add_argument('--stack', type=parse_size, default=8192, help='Stack size')
    parser.add_argument('--heap', type=parse_size, default=0, help='Heap size')
    parser.add_argument('--target', choices=['user', 'dev', 'release'], default='user',
                        help='Target type')
    parser.add_argument('--entry', type=lambda x: int(x, 0), default=0,
                        help='Entry point offset')
    parser.add_argument('--sign', metavar='KEY_FILE', help='Ed25519 private key for signing')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    # Read input binary
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        return 1
    
    binary_data = input_path.read_bytes()
    
    if args.verbose:
        print(f"Input binary: {len(binary_data)} bytes")
    
    # Map target string to enum value
    target_map = {'user': 0, 'dev': 1, 'release': 2}
    target = target_map[args.target]
    
    # Create header
    header = create_app_header(
        binary_data,
        name=args.name,
        stack_size=args.stack,
        heap_size=args.heap,
        target=target,
        entry_offset=args.entry,
    )
    
    # Sign if requested (placeholder - needs Ed25519 library)
    if args.sign:
        print("Warning: Signing not implemented yet", file=sys.stderr)
    
    # Write output
    output_path = Path(args.output)
    with output_path.open('wb') as f:
        f.write(header)
        f.write(binary_data)
    
    # Print summary
    total_size = len(header) + len(binary_data)
    crc = crc32(binary_data)
    
    if args.verbose:
        print(f"\n=== {args.name} ===")
        print(f"Header size:  {len(header)} bytes")
        print(f"Binary size:  {len(binary_data)} bytes")
        print(f"Total size:   {total_size} bytes")
        print(f"Stack size:   {args.stack} bytes")
        print(f"Heap size:    {args.heap} bytes")
        print(f"Target:       {args.target}")
        print(f"CRC32:        0x{crc:08X}")
        print(f"Output:       {args.output}")
    else:
        print(f"Created {args.output} ({total_size} bytes, CRC32=0x{crc:08X})")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
