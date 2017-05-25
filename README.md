# UnRez

UnRez extracts data from Macintosh resource forks and converts it to modern formats. UnRez also converts QuickDraw PICT files to PNG.

On old Macs, files were divided into two forks: the data fork and the resource fork. The data fork was an ordinary stream of bytes, the same way files work on other systems. If you copied a file from Windows or Unix, or downloaded a file from the internet, it would contain a data fork only. The resource fork was a separate stream with a particular format. It contained resources like pictures, icons, and sounds, as well as certain types of data like strings and program code. A single file’s resource fork is like a miniature filesystem.

Since the release of Mac OS X, resource forks have been phased out. Some of the APIs are deprecated, and others are missing entirely. UnRez gives you a tool to extract your data without relying on Apple’s APIs. UnRez works fine on Linux.

## Poking Around in Old Applications

You’ll see some interesting things if you poke around the resource forks of old applications. It was common for games to store their graphics as PICT resources and their sounds as SND resources, since it meant that the developers didn’t have to worry about image or sound file formats. Sometimes, you’d find hidden messages from the developer.

For example, if you look inside the resource fork for Bungie’s Marathon, you’ll see the terminal text for each terminal as a separate resource. You could read the story of the entire game, except it was obfuscated with a simple XOR pattern. For Ambrosia’s Escape Velocity, all of the quests, ships, weapons, star systems, and planets were encoded in resources. With a resource editor, you could make extensive mods for the game, or even remake it entirely.

## Resource Types

There are countless types of resources. UnRez can extract any resource as data, but it can also convert certain resource types into more accessible formats.

A `PICT` resource contains a QuickDraw picture, which was a common format for storing images on Macs until it was superseded by PNG and JPEG. A `.pict` (sometimes `.pct`) file used the same format as `PICT` resources, except the file format has an additional header and stores its data in the data fork. QuickDraw pictures were actually stored as a sequence of drawing commands, and fully interpreting them requires replicating the entire QuickDraw graphics library. Fortunately, most pictures only contain a bitmap image. UnRez can extract the bitmap image and convert it to a PNG. More complicated pictures are not understood.

The `SND` resource contains a sound. There are several different ways that sounds were encoded on old Macs. If the sound uses linear PCM, then it can be extracted as a WAVE file. This applies to most sounds. Other types of sounds are not supported.

## Accessing Resource Forks

If you want to extract data from a resource fork, the first step is to identify how the resource fork is stored and how to access it.

### Filesystem Support

If your filesystem supports resource forks, there are two ways to access the resource fork for a file named `file`.

* `file/rsrc` works on Linux (for HFS and HFS+ filesystems) and older versions of macOS, but in macOS 10.7 and newer it does not work.

* `file/..namedfork/rsrc` works on macOS.

UnRez supports both of these methods for accessing resource forks transparently.

### AppleDouble and AppleSingle

AppleDouble works by encoding the resource fork and some metadata as a separate, normal file. For a file named `file`, the paired AppleDouble file will be named `._file`. This format is by far the most common encoding encountered today. AppleDouble files will appear when you create tar or zip files on a Mac, and they also appear when you create files on a network drive. When you extract an archive or browse a network drive on a Mac, the AppleDouble files will be seamlessly merged with their paired files, so you ordinarily won’t see them. You will see them on other systems when you extract archives from a Mac or look at a network drive that has files from a Mac.

AppleDouble files usually don’t contain resource forks these days. Instead, they just contain some extra metadata for the corresponding file.

AppleSingle is similar to AppleDouble, but encodes the resource fork, metadata, and data fork in one file instead of two separate files.

UnRez decodes AppleDouble and AppleSingle files transparently.

### MacBinary

MacBinary encodes the resource fork, data fork, and metadata of a file into a single file. The main reason people used this was so they could send files over the internet. This was especially true for programs, which could not run without the resource fork and metadata.

Files encoded with MacBinary have the `.bin` extension, which is unfortunately used for lots of other formats too.

UnRez decodes MacBinary files transparently.

### BinHex 4.0

BinHex encodes the resource fork, data fork, and metadata of a file into a single ASCII text file. BinHex was mainly used for sending files over the internet, and since it’s ASCII text, it could be sent using old email systems that couldn’t handle binary data. BinHex uses an encoding scheme similar to Base64, but with a different set of characters.

UnRez does not yet support BinHex.

### Rez

Rez was a developer tool for old Macs which created resources from source code. This was essential for application development, since certain resources were necessary for applications to run. With the Rez language you would define types, and then define resources using those types. The Mac SDK came with a library of types in Rez for creating things like menus and dialog boxes which the GUI toolkit could understand.

Rez came with a complementary program, DeRez, which used the type definitions to parse the resources in a file’s resource fork and output source code.

UnRez does not support Rez files, and it won’t.

### Resource Files

Sometimes you will see a file with the extension `.rsrc`. These resource files were often used during software development on old Macs, like Rez files, but they were edited directly instead of being compiled from source code. This became more common once ResEdit was available, which provided a GUI for editing a file’s resource fork.

Sometimes, a `.rsrc` file will contain the resource fork of a file in the file’s data fork. This is non-standard, but it makes it possible for the file to stay intact after it’s transferred to a different system.

UnRez supports `.rsrc` files, as long as the resource fork is accessible in one of the supported ways.

### Archives

Sometimes the file you want is in an archive, and that archive could be further encoded with `.hqx` or `.bin`. Common archives are `.img`, `.sea`, and `.sit`. UnRez can extract resources from the archives, but not resources from files inside the archives.
