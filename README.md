# UnRez

UnRez extracts data from Macintosh resource forks and converts it to modern formats. UnRez UnRez also converts QuickDraw PICT files to PNG.

UnRez does not need to run on a Macintosh. It can extract resources from MacBinary or AppleDouble files transparently.

## Examples

To extract PICT resources from a file, use the `pict2png` tool with `-all-picts`:

    $ ls
    my_file.bin
    $ unrez pict2png my_file.bin -dir out -all-picts
    Writing my_file.bin.128.png...
    Writing my_file.bin.129.png...
    Writing my_file.bin.130.png...
    $ ls out
    my_file.bin.128.png
    my_file.bin.129.png
    my_file.bin.130.png

## Building

You need Python 3, Ninja, LibPNG, and pkg-config. Once you have these all installed, configure and install:

    $ ./gen.py
    $ ninja

This should create the `unrez` executable, and `libunrez.a`. Documentation for the library is available in its header file.

## Limitations

Only a subset of QuickDraw pictures are supported. This is because QuickDraw pictures can be very complex. Internally, they consist of a series of opcodes for drawing commands. You could create a picture in code by recording drawing commands and having QuickDraw play them back later.

Almost none of that is supported. Unrez looks for an opcode with a bitmap, and extracts the bitmap.

Unrez also only supports certain bitmap opcodes. There is no support for monochrome bitmaps. However, Unrez can correctly extract 8-bit, 16-bit, and 32-bit bitmaps, both packed and unpacked.
