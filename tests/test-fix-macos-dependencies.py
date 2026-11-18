#!/usr/bin/env python3
"""Regression test for colliding macOS dylib install names."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[1]
relocator = repo / 'scripts/fix-macos-dependencies.py'
scratch = pathlib.Path(os.environ.get(
    'DESKPORT_BUILD_ROOT', pathlib.Path.home() / 'mygit/build/deskport')) / 'relocation-tests'
scratch.mkdir(parents=True, exist_ok=True)

def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def file_hashes(root):
    return {path.relative_to(root): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob('*') if path.is_file() and not path.is_symlink()}

with tempfile.TemporaryDirectory(prefix='deskport-macos-relocation-', dir=scratch) as temporary:
    root = pathlib.Path(temporary)
    app = root / 'Probe.app'
    executable_dir = app / 'Contents/MacOS'
    executable_dir.mkdir(parents=True)
    consumers = []
    for label, value in [('darwin', 7), ('gnu', 10)]:
        source = root / label
        source.mkdir()
        collision = source / 'libiconv.2.dylib'
        implementation = source / 'iconv.c'
        implementation.write_text(f'int {label}_iconv(void) {{ return {value}; }}\n')
        install_name = ('@loader_path/libiconv.2.dylib' if label == 'darwin'
                        else '@rpath/libiconv.2.dylib')
        run('xcrun', 'clang', '-dynamiclib', str(implementation),
            f'-Wl,-install_name,{install_name}', '-o', str(collision))
        consumer_source = source / f'consumer-{label}.c'
        consumer_source.write_text(
            f'extern int {label}_iconv(void);\n'
            f'int consumer_{label}(void) {{ return {label}_iconv(); }}\n')
        consumer = source / f'libconsumer-{label}.dylib'
        linker_arguments = (['-Wl,-rpath,' + str(source)] if label == 'gnu' else [])
        run('xcrun', 'clang', '-dynamiclib', str(consumer_source), str(collision),
            *linker_arguments, f'-Wl,-install_name,{consumer}', '-o', str(consumer))
        consumers.append(consumer)

    main_source = root / 'main.c'
    main_source.write_text(
        '#include <stdio.h>\n'
        'extern int consumer_darwin(void);\n'
        'extern int consumer_gnu(void);\n'
        'int main(void) { printf("%d\\n", consumer_darwin() + consumer_gnu()); return 0; }\n')
    probe = executable_dir / 'probe'
    run('xcrun', 'clang', str(main_source), *(str(path) for path in consumers), '-o', str(probe))

    run(sys.executable, str(relocator), str(app))
    first_files = sorted(path.name for path in (app / 'Contents/Frameworks').iterdir())
    iconv_files = [name for name in first_files if name.startswith('libiconv.2')]
    assert len(iconv_files) == 2, iconv_files
    result = run(str(probe), capture_output=True)
    assert result.stdout == '17\n', result.stdout

    darwin_links = subprocess.check_output(
        ['otool', '-L', str(app / 'Contents/Frameworks/libconsumer-darwin.dylib')], text=True)
    gnu_links = subprocess.check_output(
        ['otool', '-L', str(app / 'Contents/Frameworks/libconsumer-gnu.dylib')], text=True)
    darwin_iconv = next(line.strip().split(' (')[0] for line in darwin_links.splitlines()
                        if 'libiconv.2' in line)
    gnu_iconv = next(line.strip().split(' (')[0] for line in gnu_links.splitlines()
                     if 'libiconv.2' in line)
    assert darwin_iconv != gnu_iconv, (darwin_iconv, gnu_iconv)
    assert darwin_iconv.startswith('@loader_path/'), darwin_iconv
    assert gnu_iconv.startswith('@loader_path/'), gnu_iconv

    # A second pass must remain stable and must not create another collision copy.
    first_hashes = file_hashes(app)
    run(sys.executable, str(relocator), str(app))
    second_files = sorted(path.name for path in (app / 'Contents/Frameworks').iterdir())
    assert second_files == first_files, (first_files, second_files)
    assert file_hashes(app) == first_hashes
    result = run(str(probe), capture_output=True)
    assert result.stdout == '17\n', result.stdout

# Nix builds Apple's libiconv with converter data under absolute /nix/store
# paths. Packages must use the system copy so other Macs can convert text.
with tempfile.TemporaryDirectory(prefix='deskport-macos-system-iconv-', dir=scratch) as temporary:
    root = pathlib.Path(temporary)
    app = root / 'Probe.app'
    (app / 'Contents/MacOS').mkdir(parents=True)
    nix = root / 'nix-libiconv'
    nix.mkdir()
    fake = nix / 'libiconv.2.dylib'
    (nix / 'iconv.c').write_text(
        '#include <stddef.h>\n'
        'const char *data = "/nix/store/0000000000000000000000000000000-libiconv-113/share/i18n/csmapper";\n'
        'void *iconv_open(const char *a, const char *b) { (void)a; (void)b; (void)data; return (void *)-1; }\n'
        'size_t iconv(void *c, char **i, size_t *il, char **o, size_t *ol) { (void)c; (void)i; (void)il; (void)o; (void)ol; return (size_t)-1; }\n'
        'int iconv_close(void *c) { (void)c; return -1; }\n')
    run('xcrun', 'clang', '-dynamiclib', str(nix / 'iconv.c'), '-Wl,-install_name,@rpath/libiconv.2.dylib', '-o', str(fake))
    (root / 'convert.c').write_text(
        '#include <iconv.h>\n#include <stdio.h>\n#include <string.h>\n'
        'int main(void) { iconv_t c = iconv_open("UTF-8", "ISO-8859-1");\n'
        '  if (c == (iconv_t)-1) return 2;\n'
        '  char in[] = "\\xe9"; char out[8]; char *ip = in, *op = out; size_t il = 1, ol = sizeof out;\n'
        '  if (iconv(c, &ip, &il, &op, &ol) == (size_t)-1) return 3;\n'
        '  printf("%zu\\n", sizeof out - ol); iconv_close(c); return 0; }\n')
    probe = app / 'Contents/MacOS/probe'
    run('xcrun', 'clang', str(root / 'convert.c'), str(fake), '-Wl,-rpath,' + str(nix), '-o', str(probe))
    assert subprocess.run([str(probe)]).returncode == 2, 'fixture must fail with the build-machine libiconv'
    run(sys.executable, str(relocator), str(app))
    links = subprocess.check_output(['otool', '-L', str(probe)], text=True)
    assert '/usr/lib/libiconv.2.dylib' in links and '@loader_path' not in links.split('libiconv')[0][-40:], links
    frameworks = app / 'Contents/Frameworks'
    assert not [path for path in frameworks.iterdir() if path.name.startswith('libiconv')], list(frameworks.iterdir())
    result = run(str(probe), capture_output=True)
    assert result.stdout == '2\n', result.stdout

print('PASS: colliding dylib basenames retain distinct source ABIs and remain runnable; '
      'build-machine Apple libiconv is replaced by the system library')
