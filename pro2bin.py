#!/usr/bin/env python

import sys
import click
import string
import random

def parseversion():
    return "3.0.5.1"

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
    with open(hexfile, "r") as fr:
        with open(binfile, "wb") as fw:
            fw.write(parseversion().encode() + b'\0')
            for byte in hex2bytes(fr):
                fw.write(bytes([byte]))

if __name__ == "__main__":
    main()
