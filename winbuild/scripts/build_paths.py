#!/usr/bin/env python3
"""Windows run paths and persistent, recipe-keyed dependency storage.

The run owns application/host objects and packages. Only the explicitly listed
third-party trees link to the persistent cache; no cache path references a run.
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import build_storage
from host_boost import source_path as boost_source_path

SCRIPTS = Path(__file__).resolve().parent
SOURCE = Path(os.environ.get('DESKPORT_WINDOWS_SOURCE_ROOT', SCRIPTS.parents[1])).resolve()
CACHE_RECIPES = (
    'flake.lock', 'host/vendor/sunshine.json', 'winbuild/inputs.json',
    'winbuild/scripts/shell.nix', 'winbuild/scripts/env.sh',
    'winbuild/scripts/build-deps.sh', 'winbuild/scripts/build-vulkan-deps.sh',
    'winbuild/scripts/build-qt.sh', 'winbuild/scripts/build-host-deps.sh',
    'winbuild/scripts/build-host-media.sh', 'winbuild/scripts/build-host-datachannel.sh',
    'winbuild/scripts/make-case-shims.sh', 'winbuild/scripts/mingw-toolchain.cmake',
    'winbuild/scripts/mingw-host-toolchain.cmake', 'winbuild/scripts/meson-cross.txt',
    'winbuild/scripts/host-vendored-deps.json',
)
# These are third-party inputs, their compiled libraries, or toolchain notices.
# Never add build-app, sunshine-prepared, host-build-prepared or helper EXEs.
SHARED_DIRECTORIES = (
    'source-inputs', 'work', 'prefix', 'qt-static', 'compat-include', 'toolshim',
    'toolchain-notices', 'full/prefix', 'full/media-source', 'full/media-prefix',
    'full/curl', 'full/curl-build', 'full/miniupnpc', 'full/miniupnpc-build',
    'full/minhook', 'full/minhook-build', 'full/onevpl', 'full/onevpl-build',
    'full/x264-build', 'full/x265-build', 'full/svt-build', 'full/ffmpeg-build',
    'full/cbs-build', 'full/libdatachannel', 'full/libdatachannel-build',
    'full/amf', 'full/vdd', 'full/upstream-assets', 'full/host-vendored-deps',
    'full/cpm-source-cache',
)


def recipe_key(source=SOURCE):
    digest = hashlib.sha256(b'deskport-windows-dependencies-v1\0')
    for relative in CACHE_RECIPES:
        digest.update(relative.encode() + b'\0' + (source / relative).read_bytes())
    # Overrides must not silently reuse binaries compiled from different inputs.
    for name, value in sorted(os.environ.items()):
        if not name.startswith('S_') and name != 'DESKPORT_HOST_TOOLCHAIN_FILE':
            continue
        path = Path(value).expanduser().resolve()
        digest.update(name.encode() + b'\0' + str(path).encode() + b'\0')
        if path.is_file():
            digest.update(hashlib.sha256(path.read_bytes()).digest())
        elif path.is_dir():
            for child in sorted(path.rglob('*')):
                if child.is_file() and '.git' not in child.relative_to(path).parts:
                    digest.update(str(child.relative_to(path)).encode() + b'\0')
                    digest.update(hashlib.sha256(child.read_bytes()).digest())
        else:
            raise SystemExit(f'Missing Windows dependency override {name}: {path}')
    return digest.hexdigest()[:24]


def compatible_cache(cache_base):
    """Reuse absolute-path caches when only the application core pin changed.

    Verify the old recipe identity and every dependency input before reuse.
    The retained scripts/toolchain path is deliberately not moved or replaced.
    """
    def dependency_lock(source):
        lock = json.loads((source / 'flake.lock').read_text())
        lock['nodes'].pop('deskport-core', None)
        return lock
    for candidate in sorted(cache_base.glob('*')):
        link = candidate / 'scripts'
        if not link.is_symlink() or not (candidate / 'qt-static/lib').is_dir():
            continue
        old_source = link.resolve().parents[1]
        try:
            if recipe_key(old_source) != candidate.name:
                continue
            if dependency_lock(old_source) != dependency_lock(SOURCE):
                continue
            if all((old_source / p).read_bytes() == (SOURCE / p).read_bytes()
                   for p in CACHE_RECIPES if p != 'flake.lock'):
                return candidate
        except (OSError, ValueError, KeyError):
            continue
    return None


def common_paths():
    names = ('DESKPORT_CHECKOUT_BUILD_ROOT', 'DESKPORT_RUN_ID', 'DESKPORT_WORK_DIR',
             'DESKPORT_DIST_DIR', 'DESKPORT_LOG_DIR', 'DESKPORT_DEPENDENCY_CACHE_DIR')
    script = 'export DESKPORT_TARGET=windows; source "$1"; ' + ''.join(
        'printf "%s\\0" "${' + name + ':-}"; ' for name in names)
    env = dict(os.environ)
    env.setdefault('DESKPORT_RUN_ID', 'dev')
    result = subprocess.run(['bash', '-c', script, 'build_paths', str(SOURCE / 'scripts/build-paths.sh')],
                            env=env, check=True, capture_output=True)
    return dict(zip(names, result.stdout.decode().rstrip('\0').split('\0')))


def paths():
    storage = build_storage.Storage()
    common = common_paths()
    checkout = Path(common['DESKPORT_CHECKOUT_BUILD_ROOT']).resolve()
    run_id = common.get('DESKPORT_RUN_ID') or 'dev'
    # Keep the same validation for old callers as the shared path resolver.
    if not run_id or run_id in ('.', '..') or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-' for c in run_id):
        raise SystemExit('DESKPORT_RUN_ID must contain only letters, digits, dot, underscore or dash')
    run = checkout / 'runs.noindex' / run_id / 'windows'
    work = Path(os.environ.get('DESKPORT_WINDOWS_BUILD_ROOT') or common.get('DESKPORT_WORK_DIR') or run / 'work').resolve()
    dist = Path(os.environ.get('PACKAGE_OUT') or common.get('DESKPORT_DIST_DIR') or run / 'dist').resolve()
    logs = Path(os.environ.get('DESKPORT_LOG_DIR') or common.get('DESKPORT_LOG_DIR') or run / 'logs').resolve()
    cache_base = Path(common.get('DESKPORT_DEPENDENCY_CACHE_DIR') or checkout / 'dependencies.noindex/windows').resolve()
    cache = cache_base / recipe_key()
    # A shared policy revision cannot change Qt, FFmpeg or the host toolchain.
    cache = compatible_cache(cache_base) or cache
    downloads = Path(os.environ.get('DESKPORT_WINDOWS_INPUT_CACHE') or cache_base / 'downloads').resolve()
    for label, path in (('work', work), ('dist', dist), ('logs', logs), ('dependency cache', cache_base), ('input cache', downloads)):
        storage.validate(path)
        if path == SOURCE or SOURCE in path.parents:
            raise SystemExit(f'Windows {label} must be outside the source checkout: {path}')
    run_parent = checkout / 'runs.noindex'
    if cache_base == run_parent or run_parent in cache_base.parents or cache_base == work or work in cache_base.parents:
        raise SystemExit('Windows dependency cache must not be inside a build run')
    if downloads == run_parent or run_parent in downloads.parents or downloads == work or work in downloads.parents:
        raise SystemExit('Windows input cache must not be inside a build run')
    return dict(source=SOURCE, work=work, dist=dist, logs=logs, cache=cache,
                cache_base=cache_base, downloads=downloads, run_id=run_id)


def checked_link(link, target):
    link.parent.mkdir(parents=True, exist_ok=True)
    if link.is_symlink():
        if link.resolve() != target.resolve():
            raise SystemExit(f'Refusing unexpected Windows cache link: {link} -> {os.readlink(link)}')
        return
    if link.exists():
        raise SystemExit(f'Refusing to replace existing Windows output: {link}')
    try:
        link.symlink_to(target)
    except FileExistsError:
        if not link.is_symlink() or link.resolve() != target.resolve():
            raise


def initialize(config):
    with cache_lock(config):
        return initialize_locked(config)


def initialize_locked(config):
    work, cache = config['work'], config['cache']
    for key in ('work', 'logs', 'cache', 'downloads'):
        config[key].mkdir(parents=True, exist_ok=True)
    for relative in ('source-inputs', 'work', 'prefix', 'qt-static', 'compat-include', 'toolshim', 'toolchain-notices'):
        (cache / relative).mkdir(parents=True, exist_ok=True)
    checked_link(work / 'scripts', SCRIPTS)
    if cache != compatible_cache(config['cache_base']):
        checked_link(cache / 'scripts', SCRIPTS)
    links = {'scripts': str(SCRIPTS)}
    for relative in SHARED_DIRECTORIES:
        # Do not create leaf source/build trees here: recipes use their absence
        # to decide when extraction/configuration is needed.
        target = cache / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        checked_link(work / relative, target)
        links[relative] = str(target)
    manifest = json.loads((SOURCE / 'winbuild/inputs.json').read_text())
    for entry in manifest['inputs']:
        relative = entry.get('copy')
        if relative and relative.startswith('full/'):
            target = cache / relative
            checked_link(work / relative, target)
            links[relative] = str(target)
    checked_link(work / 'input-cache', config['downloads'])
    checked_link(cache / 'input-cache', config['downloads'])
    links['input-cache'] = str(config['downloads'])
    record = dict(schema=1, source=str(SOURCE), work=str(work), cache=str(cache), links=links)
    marker = work / '.deskport-windows-paths.json'
    if marker.exists() and json.loads(marker.read_text()) != record:
        raise SystemExit(f'Windows run dependency identity changed; use a new run ID: {marker}')
    temporary = marker.with_suffix('.tmp')
    temporary.write_text(json.dumps(record, indent=2) + '\n')
    temporary.replace(marker)
    return config


def hold_cache_lock(config):
    """Return a live lock handle for a standalone Python entry point."""
    if os.environ.get('DESKPORT_WINDOWS_CACHE_LOCK_HELD') == '1':
        return None
    import fcntl
    config['cache'].mkdir(parents=True, exist_ok=True)
    lock = (config['cache'] / '.build.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX)
    return lock


@contextlib.contextmanager
def cache_lock(config):
    lock = hold_cache_lock(config)
    try:
        yield
    finally:
        if lock is not None:
            lock.close()


def shell(config):
    values = {
        'DESKPORT_WINDOWS_SOURCE_ROOT': config['source'],
        'DESKPORT_WINDOWS_BUILD_ROOT': config['work'],
        'DESKPORT_WINDOWS_DEPENDENCY_ROOT': config['cache'],
        'DESKPORT_WINDOWS_INPUT_CACHE': config['downloads'],
        'DESKPORT_HOST_DEP_CACHE': config['cache'] / 'full/host-vendored-deps',
        'DESKPORT_HOST_BOOST_SOURCE_DIR': boost_source_path(config['cache']),
        'DESKPORT_RUN_ID': config['run_id'],
        'DESKPORT_WINDOWS_AUDIT_DIR': os.environ.get('DESKPORT_WINDOWS_AUDIT_DIR', config['work'] / 'audit'),
        'DESKPORT_LOG_DIR': config['logs'],
        'PACKAGE_OUT': config['dist'],
        'PACKAGE_STAGE': os.environ.get('PACKAGE_STAGE', config['work'] / 'stage'),
        'DESKPORT_HOST_SOURCE_DIR': os.environ.get('DESKPORT_HOST_SOURCE_DIR', config['work'] / 'full/sunshine-prepared'),
        'DESKPORT_HOST_BUILD_DIR': os.environ.get('DESKPORT_HOST_BUILD_DIR', config['work'] / 'full/host-build-prepared'),
    }
    for name, value in values.items():
        print(f'export {name}={shlex.quote(str(value))}')


if __name__ == '__main__':
    config = paths()
    if '--initialize' in sys.argv:
        initialize(config)
    if '--shell' in sys.argv:
        shell(config)
    else:
        print(json.dumps({k: str(v) for k, v in config.items()}, indent=2))
