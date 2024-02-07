#!/usr/bin/env python

import sys
import click
import string
import random
import re

# TODO: Don't hardcode this, have ZapChargerProMCU as a submodule in the repo and
# use relative path
VERSION_FILE = "/Users/smj/dev/ZapChargerProMCU/smart/smart/src/Versions.c"
VERSION_MATCH = r'const char\* swVersion = "(\d+\.\d+.\d+.\d+)"'

def parse_version():
    version = None
    with open(VERSION_FILE) as f:
        for line in f.readlines():
            match = re.search(VERSION_MATCH, line)
            if match:
                version = match.group(1)
    return version

def hex2bytes(file):
    for line in file.read().splitlines():
        addr, data = line.split(":")
        addr = int(addr, 16)

        for b in [(addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff, 0, 0]:
            yield b

        for i in range(len(data) // 2):
            yield int(data[i*2:i*2+2], 16)

@click.command()
@click.argument("hexfile", type=click.Path(exists=True))
@click.argument("binfile")
def main(hexfile, binfile):
    version = parse_version()
    if not version:
        raise click.UsageError("Can't find version in version file!")

    print("Hex    : %s" % hexfile)
    print("Bin    : %s" % binfile)
    print("Version: %s" % version)

    with open(hexfile, "r") as fr:
        with open(binfile, "wb") as fw:
            fw.write(version.encode() + b'\0')
            for byte in hex2bytes(fr):
                fw.write(bytes([byte]))

if __name__ == "__main__":
    main()
