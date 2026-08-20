#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: Apache-2.0
#
# Patches the address main() was linked at into the placeholder sentinel
# bytes in main.c, so the sample can print it at runtime alongside the
# address main() actually executes from once self-relocation has run.

import argparse
import struct
import subprocess

SENTINEL = bytes([0xef, 0xbe, 0xad, 0xde, 0xbe, 0xba, 0xfe, 0xca])


def main_link_address(nm, elf_file):
    output = subprocess.check_output([nm, elf_file], text=True)
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1] == "main":
            return int(fields[0], 16)
    raise RuntimeError(f"symbol 'main' not found in {elf_file}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("elf_file")
    args = parser.parse_args()

    addr = main_link_address(args.nm, args.elf_file)
    patched = struct.pack("<Q", addr)

    with open(args.elf_file, "r+b") as f:
        data = f.read()
        offset = data.find(SENTINEL)
        if offset < 0:
            raise RuntimeError("sentinel not found, was main.c changed?")
        if data.find(SENTINEL, offset + 1) >= 0:
            raise RuntimeError("sentinel is not unique in the image")
        f.seek(offset)
        f.write(patched)


if __name__ == "__main__":
    main()
