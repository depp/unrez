#!/usr/bin/env python3
import argparse
import pathlib
import re
import subprocess
import sys

VARS = ['cflags', 'ldflags', 'libs']

ENV = {
    'cflags': '-std=c90 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64',
}

CONFIGS = {
    'debug': {
        'cflags': '-g',
    },
    'release': {
        'cflags': '-O2',
    },
    'asan': {
        'cflags': '-g -O2 -fsanitize=address',
        'ldflags': '-fsanitize=address',
    },
    'ubsan': {
        'cflags': '-g -O2 -fsanitize=undefined',
        'ldflags': '-fsanitize=undefined',
    },
}

SPECIAL = re.compile(r'[$ ]')
SPECIAL2 = re.compile(r'[$ :]')

def escape(s, *, is_output=False):
    def repl(m):
        return '$' + m.group(0)
    return SPECIAL.sub(repl, s)

def escapeflags(flags):
    return ' '.join(escape(flag) for flag in flags)

RULES = '''\
rule gen
  command = {gen}
build build.ninja: gen gen.py
cc = {cc}
cflags = {cflags}
ldflags = {ldflags}
libs = {libs}
pic_cflags = -fpic $cflags
unrez_cflags = {png_cflags} $cflags
unrez_libs = {png_libs} $libs
rule c
  command = $cc -o $out $in -c -MMD -MF $out.d $cflags
  depfile = $out.d
rule link
  command = $cc $ldflags -o $out $in $libs
rule ar
  command = ar rcs $out $in
'''

LIB_SOURCES = '''
appledouble.c
data.c
error.c
forkedfile.c
macbinary.c
pict.c
pixdata.c
resourcefork.c
'''.split()

EXE_TARGETS = [
('unrez',
 ['cflags = $unrez_cflags'],
 ['libs = $unrez_libs'],
 [], '''
cat.c
info.c
ls.c
opts.c
pictdump.c
png.c
resx.c
size.c
unrez.c
util.c
'''.split()),
('size_test', [], [], [], '''
size.c
size_test.c
'''.split()),
]

def run():
    p = argparse.ArgumentParser(
        'gen.py',
        description='Generate the Ninja build script for UnRez.',
        allow_abbrev=False)
    p.add_argument('-warn', action='store_true',
                   help='emit more warnings')
    p.add_argument('-werror', action='store_true',
                   help='treat warnings as errors')
    p.add_argument('-config', choices=sorted(CONFIGS),
                   default='release',
                   help='set build configuration')
    p.add_argument('-cc', help='set the C compiler', default='cc')
    p.add_argument('-cflags', help='set additional C compiler flags')
    p.add_argument('-ldflags', help='set additional linker flags')
    p.add_argument('-libs', help='set additional libraries')

    args = p.parse_args()
    env = {}
    config = CONFIGS[args.config]
    for var in VARS:
        value = []
        for x in [ENV.get(var), config.get(var), getattr(args, var)]:
            if x:
                value.extend(x.split())
        env[var] = value

    png_cflags = subprocess.check_output(
        ['pkg-config', 'libpng', '--cflags']).decode('ASCII').strip()
    png_libs = subprocess.check_output(
        ['pkg-config', 'libpng', '--libs']).decode('ASCII').strip()

    srcdir = pathlib.Path(sys.argv[0]).parent
    with open('build.ninja', 'w') as fp:
        cflags = ['-iquote', str(srcdir / 'include')]
        if args.warn:
            cflags.extend(
                '-Wall -Wextra -Wpointer-arith -Wwrite-strings '
                '-Wmissing-prototypes -Wstrict-prototypes'
                .split())
        if args.werror:
            cflags.append('-Werror')
        cflags.extend(env['cflags'])
        fp.write(RULES.format(
            gen=escapeflags([sys.executable] + sys.argv),
            cc=escape(args.cc),
            cflags=escapeflags(cflags),
            ldflags=escapeflags(env['ldflags']),
            libs=escapeflags(env['libs']),
            png_cflags=png_cflags,
            png_libs=png_libs,
        ))

        lib_objs = []
        pic_objs = []
        for src in LIB_SOURCES:
            src = pathlib.Path(src)
            lib_obj = pathlib.Path('obj/lib', src.with_suffix('.o'))
            pic_obj = pathlib.Path('obj/pic', src.with_suffix('.o'))
            src = srcdir.joinpath('lib', src)
            fp.write(
                'build {obj}: c {src}\n'
                'build {pic_obj}: c {src}\n'
                '  cflags = $pic_cflags\n'
                .format(
                    src=src,
                    obj=lib_obj,
                    pic_obj=pic_obj,
                ))
            lib_objs.append(lib_obj)
            pic_objs.append(pic_obj)
        fp.write(
            'build libunrez.a: ar {lib_objs}\n'
            'build libunrez.so: link {pic_objs}\n'
            '  ldflags = -shared -fpic $ldflags\n'
            .format(
                lib_objs=' '.join(str(x) for x in lib_objs),
                pic_objs=' '.join(str(x) for x in pic_objs),
            ))

        for target, cvars, lvars, extra, sources in EXE_TARGETS:
            objdir = pathlib.Path('obj', target)
            objs = []
            for src in sources:
                src = pathlib.Path(src)
                obj = objdir.joinpath(src.with_suffix('.o'))
                src = srcdir.joinpath('src', src)
                fp.write('build {}: c {}\n'.format(obj, src))
                for cvar in cvars:
                    fp.write('  {}\n'.format(cvar))
                objs.append(obj)
            if target == 'unrez':
                objs.extend(lib_objs)
            fp.write('build {}: link {}\n'.format(
                target, ' '.join(str(obj) for obj in objs + extra)))
            for lvar in lvars:
                fp.write('  {}\n'.format(lvar))
        fp.write('default unrez\n')

if __name__ == '__main__':
    run()
