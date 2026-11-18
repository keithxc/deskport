"""Checked build output paths. Shared mygit policy owns disk identity and reserves.

This adapter validates exact outputs separately from the shared *base* root.
It does not intercept raw tools or GUI builds. No directories are made by Storage.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
PROJECT = 'deskport'
ROOT = Path(__file__).resolve().parents[1]
OUTPUT_ENV = (
    'DESKPORT_BUILD_ROOT', 'DESKPORT_MACOS_BUILD_DIR', 'DESKPORT_MACOS_DIST_DIR',
    'DESKPORT_LINUX_WORK', 'DESKPORT_DEPENDENCY_CACHE_DIR',
    'DESKPORT_HOST_BUILD_DIR', 'DESKPORT_WINDOWS_BUILD_ROOT', 'DESKPORT_WINDOWS_AUDIT_DIR',
    'DESKPORT_WINDOWS_INPUT_CACHE', 'DESKPORT_WINDOWS_DEPENDENCY_BUILD',
    'DESKPORT_WINDOWS_DEPENDENCY_ROOT', 'DESKPORT_WINDOWS_DEPENDENCY_CACHE',
    'DESKPORT_WORK_DIR', 'DESKPORT_DIST_DIR', 'DESKPORT_LOG_DIR', 'DESKPORT_TEST_DIR',
    'DESKPORT_GENERATED_DIR', 'DESKPORT_DERIVED_DATA_DIR', 'DESKPORT_ARCHIVE_PATH',
    'DESKPORT_OPENSSL_WORK_DIR', 'DESKPORT_DEPENDENCY_SOURCES_DIR',
    'DESKPORT_CLIPBOARD_TEST_DIR', 'DESKPORT_TEST_WORK_DIR', 'DESKPORT_X11_TEST_OUTPUT',
    'DESKPORT_TEST_SOCKET_ROOT', 'DESKPORT_TEST_OUTPUT_EVENTS',
    'PACKAGE_OUT', 'PACKAGE_STAGE', 'GRADLE_USER_HOME',
)
OUTPUT_FLAGS = {'--output', '--output-dir', '--work-dir', '--work', '--cache',
                '--sources', '--json', '--dependency-cache', '--attachments-dir',
                '--runtime-root', '--socket-root', '--report', '--result', '--log-output',
                '--output-directory', '--output-prefix', '--results'}


def on_mm4():
    return platform.node().split('.')[0].lower() == 'mm4'


def shared_helper():
    candidates = [parent / 'tools/build-paths.py' for parent in (ROOT.parent, *ROOT.parents)]
    try:
        import pwd
        account_home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    except (ImportError, AttributeError, KeyError):
        account_home = Path.home()
    candidates.append(account_home / 'mygit/tools/build-paths.py')
    canonical = next((p for p in candidates if p.is_file()), None)
    override = os.environ.get('MYGIT_PATH_RESOLVER')
    if override:
        selected = Path(override).expanduser()
        if on_mm4() and (canonical is None or selected.resolve() != canonical.resolve()):
            raise ValueError('mm4 requires the installed shared mygit build resolver')
        canonical = selected if selected.is_file() else None
    if canonical is None:
        if on_mm4():
            raise ValueError('Missing shared mygit build resolver on mm4; no fallback')
        return None
    spec = importlib.util.spec_from_file_location('_deskport_shared_paths', canonical)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Storage:
    def __init__(self, task=None, target=None):
        self.helper = shared_helper()
        policy_api = self.helper and all(callable(getattr(self.helper, name, None)) for name in
                                         ('host_policy', 'workspace_root', 'verify_external_root'))
        if on_mm4() and not policy_api:
            raise ValueError('Current shared external storage policy API is required on mm4')
        self.policy = self.helper.host_policy(self.helper.workspace_root()) if policy_api else {}
        self.policy_api = bool(policy_api)
        self._verified_external = False
        self.required = on_mm4() or self.policy.get('require_external_build_root', False)
        if on_mm4() and not self.policy.get('require_external_build_root'):
            raise ValueError('mm4 external storage policy is unavailable; no fallback')
        task = task or os.environ.get('DESKPORT_BUILD_RUN') or os.environ.get('DESKPORT_RUN_ID') or 'dev'
        target = target or os.environ.get('DESKPORT_STORAGE_PLATFORM') or (
            self.helper.native_platform() if self.helper else platform.system().lower())
        if self.helper:
            # Only configured base roots go through paths(); never exact outputs.
            self.paths = self.helper.paths(argparse.Namespace(project=PROJECT, checkout=ROOT,
                task=task, platform=target, build_root=None))
        else:
            base = Path(os.environ.get('MYGIT_BUILD_ROOT', Path.home() / 'mygit/build'))
            # Keep portable fallback independent of the mm4 disk and CLI tools.
            import zlib
            checkout = base / PROJECT / (ROOT.name + '-' + str(zlib.crc32(str(ROOT).encode())))
            self.paths = dict(root=base / PROJECT, checkout=checkout, task=checkout / task,
                              platform=checkout / task / target)
            self.paths.update({p: self.paths['platform'] / p for p in
                ('work', 'cache', 'tests', 'logs', 'dist', 'sources', 'evidence', 'releases', 'rollback')})
            # CI and Nix sandboxes often have a deliberately unwritable HOME.
            # Preserve the desktop project-root convention, but use the host's
            # existing temporary area for auxiliary caches when no helper exists.
            portable = Path(tempfile.gettempdir()) / ('deskport-' + PROJECT)
            self.paths.update({p: portable / p for p in ('work', 'cache', 'tests', 'logs', 'dist', 'sources', 'evidence', 'releases', 'rollback')})

    def validate(self, value, *, app=False, generated=False):
        raw = Path(value).expanduser()
        if not raw.is_absolute():
            raw = Path.cwd() / raw
        if '..' in raw.parts:
            raise ValueError('Output cannot contain parent traversal: ' + str(raw))
        # One existing, explicitly approved compatibility alias; no other link.
        alias = ROOT / 'Build.noindex'
        approved = Path(self.policy.get('archive_mount', '/__unconfigured__')) / (
            'data/mygit-build/deskport-client/migration-20261105.noindex/Build.noindex')
        allowed_alias = (PROJECT == 'deskport-client' and alias.is_symlink()
                         and alias.resolve() == approved and raw.is_relative_to(alias))
        for ancestor in (raw, *raw.parents):
            if ancestor.is_symlink() and not (allowed_alias and ancestor == alias):
                # System aliases preserve /tmp compatibility away from mm4.
                if not self.required and ancestor in (Path('/tmp'), Path('/var')):
                    continue
                raise ValueError('Symlink in output path: ' + str(ancestor))
        path = raw.resolve()
        if allowed_alias:
            for ancestor in (path, *path.parents):
                if ancestor.is_symlink():
                    raise ValueError('Symlink in approved generated target: ' + str(ancestor))
        if any(p == '.git' for p in path.parts) or path.is_relative_to(Path('/nix/store')):
            raise ValueError('Output cannot use Git metadata or Nix: ' + str(path))
        if path in (Path('/'), Path.home(), ROOT, ROOT.parent):
            raise ValueError('Output is not a build directory: ' + str(path))
        # Portable native generated projects historically live in the checkout.
        if self.required:
            mount = Path(self.policy.get('archive_mount', '/__unconfigured__'))
            if path == mount or not path.is_relative_to(mount):
                raise ValueError('Output must be on the verified external volume: ' + str(path))
            for ancestor in (path, *path.parents):
                if ancestor == mount:
                    break
                if ancestor.exists() and os.path.ismount(ancestor):
                    raise ValueError('Nested mount in output path: ' + str(ancestor))
            # All paths in this adapter invocation use the same configured
            # physical volume. Verify one exact output using the shared policy;
            # paths() has already verified the configured base and reserve.
            if not self._verified_external:
                self.helper.verify_external_root(path, self.policy)
                self._verified_external = True
            for ancestor in (path, *path.parents):
                if (ancestor / '.git').exists() or (ancestor / '.git').is_symlink() or (
                    (ancestor / 'HEAD').is_file() and (ancestor / 'objects').is_dir()):
                    raise ValueError('Output inside a source repository: ' + str(path))
            if app and not any(p.endswith('.noindex') for p in path.parts):
                raise ValueError('Application output needs a .noindex ancestor: ' + str(path))
        elif self.policy_api and path.is_relative_to(Path('/Volumes')):
            self.helper.verify_external_root(path, self.policy)
        # A project source tree is not a disposable build destination. Preserve
        # the historical non-mm4 generated client tree for portable checkouts.
        if not self.required:
            portable_generated = (PROJECT == 'deskport-client' and
                                  path.is_relative_to(ROOT / 'Build.noindex'))
            if not portable_generated:
                for ancestor in (path, *path.parents):
                    if (ancestor / '.git').exists() or (ancestor / '.git').is_symlink():
                        raise ValueError('Output inside a source repository: ' + str(path))
        return path

    def generated(self):
        override = os.environ.get('DESKPORT_GENERATED_DIR')
        legacy = ROOT / 'Build.noindex'
        default = legacy if legacy.is_symlink() or not self.required else self.paths['work'] / 'Generated.noindex'
        return self.validate(override or default, app=True, generated=True)

    def environment(self, environ=None):
        environ = os.environ if environ is None else environ
        # Validate all caller-selected output locations before even creating tmp.
        for key in OUTPUT_ENV:
            if environ.get(key):
                self.validate(environ[key], app=key in (
                    'DESKPORT_MACOS_BUILD_DIR', 'DESKPORT_MACOS_DIST_DIR',
                    'DESKPORT_DERIVED_DATA_DIR', 'DESKPORT_ARCHIVE_PATH'))
        for item in shlex.split(environ.get('JAVA_TOOL_OPTIONS', '')):
            if item.startswith('-Djava.io.tmpdir='):
                self.validate(item.split('=', 1)[1])
        p = self.paths
        env = {f'MYGIT_{key.upper()}_DIR': str(value) for key, value in p.items()}
        env['MYGIT_BUILD_ROOT'] = str(p['root'].parent)
        cache = p['cache']
        # Darwin limits AF_UNIX paths to 103 bytes. Keep per-checkout/task
        # isolation without falling back to the internal /tmp filesystem.
        identity = hashlib.sha256(str(p['platform']).encode()).hexdigest()[:10]
        label = {'deskport': 'dp', 'deskport-client': 'dc', 'deskport-core': 'core'}[PROJECT]
        tmp = (p['root'].parent / 's.noindex' / (label + '-' + identity) if self.helper
               else Path(tempfile.gettempdir()) / ('s-' + label + '-' + identity))
        env.update(TMPDIR=str(tmp), TMP=str(tmp), TEMP=str(tmp),
            PYTHONPYCACHEPREFIX=str(cache / 'pycache'), PYTHONDONTWRITEBYTECODE='1',
            CLANG_MODULE_CACHE_PATH=str(cache / 'clang-modules'),
            SWIFT_MODULECACHE_PATH=str(cache / 'swift-modules'),
            SWIFT_MODULE_CACHE_PATH=str(cache / 'swift-modules'),
            COMPILATION_CACHE_CAS_PATH=str(cache / 'cas'),
            CCACHE_DIR=str(cache / 'ccache'), SCCACHE_DIR=str(cache / 'sccache'),
            CARGO_TARGET_DIR=str(p['work'] / 'target.noindex'),
            XDG_CACHE_HOME=str(cache / 'xdg'),
            SWIFTPM_MODULECACHE_OVERRIDE=str(cache / 'swift-modules'),
            SWIFTPM_CUSTOM_CACHE_PATH=str(cache / 'swiftpm/cache'),
            SWIFTPM_CUSTOM_CONFIG_PATH=str(cache / 'swiftpm/config'),
            SWIFTPM_CUSTOM_SECURITY_PATH=str(cache / 'swiftpm/security'))
        for key in ('CCACHE_DIR', 'SCCACHE_DIR', 'CARGO_TARGET_DIR'):
            if environ.get(key):
                env[key] = str(self.validate(environ[key]))
        if PROJECT == 'deskport':
            env['DESKPORT_BUILD_ROOT'] = str(self.validate(environ.get('DESKPORT_BUILD_ROOT', p['root'])))
        if PROJECT == 'deskport-client':
            generated = self.generated()
            env.update(DESKPORT_GENERATED_DIR=str(generated),
                DESKPORT_DERIVED_DATA_DIR=str(self.validate(environ.get('DESKPORT_DERIVED_DATA_DIR', generated / 'AppleClient'), app=True)),
                DESKPORT_OPENSSL_WORK_DIR=str(self.validate(environ.get('DESKPORT_OPENSSL_WORK_DIR', generated / 'OpenSSLWithSymbols'))),
                DESKPORT_DEPENDENCY_SOURCES_DIR=str(self.validate(environ.get('DESKPORT_DEPENDENCY_SOURCES_DIR', generated / 'SourceAudit'))),
                GRADLE_USER_HOME=str(self.validate(environ.get('GRADLE_USER_HOME', p['checkout'] / 'dev/android/cache/gradle'))))
        for value in env.values():
            if value.startswith('/'):
                self.validate(value)
        return env

    def prepare(self, env):
        # Called only after every explicit output and tool argv has been checked.
        for key in ('TMPDIR', 'CLANG_MODULE_CACHE_PATH', 'SWIFT_MODULECACHE_PATH',
                    'COMPILATION_CACHE_CAS_PATH'):
            self.validate(env[key]).mkdir(parents=True, exist_ok=True)


def check_arguments(storage, argv):
    name = Path(sys.argv[0]).name
    positional = [str(a) for a in argv]
    if name == 'export-store-screenshots.py':
        # argparse permits options before or after the two positional paths.
        paths = []
        skip = False
        for item in positional:
            if skip:
                skip = False
                continue
            flag, equals, _ = item.partition('=')
            if flag in ('--device', '--languages', '--scenes', '--attachments-dir'):
                skip = not equals
            elif not item.startswith('-'):
                paths.append(item)
        if len(paths) >= 2:
            storage.validate(paths[1])
    if name == 'pc-regression.py' and len(positional) >= 2 and positional[0] in ('fixture', 'matrix-init', 'matrix-record'):
        storage.validate(positional[1])
    if name == 'prepare-host-source.py' and positional and not positional[0].startswith('-'):
        storage.validate(positional[0])
    if name == 'extract-appimage.py' and len(positional) >= 2:
        storage.validate(positional[1])
    if name == 'sign-openssl-ios.py' and positional and not positional[0].startswith('-'):
        storage.validate(Path(positional[0]).expanduser().absolute().parent)
    for index, item in enumerate(argv):
        flag, sep, value = str(item).partition('=')
        if flag in OUTPUT_FLAGS or (flag == '--stage' and name == 'android_native_deps.py'):

            if not sep:
                if index + 1 >= len(argv):
                    raise ValueError('Missing output argument: ' + flag)
                value = argv[index + 1]
            storage.validate(value, app=name == 'prepare.py' and flag == '--output')


def bootstrap(argv=None):
    storage = Storage()
    check_arguments(storage, sys.argv[1:] if argv is None else argv)
    env = storage.environment()
    os.environ.update(env)
    tempfile.tempdir = None
    storage.prepare(env)
    return storage


def xcode_command(storage, args):
    args = [str(x) for x in args]
    path_flags = {'-derivedDataPath': True, '-archivePath': True, '-resultBundlePath': False,
                  '-clonedSourcePackagesDirPath': False, '-packageCachePath': False,
                  '-output': False, '-resultStreamPath': False, '-exportPath': True}
    settings = {'SYMROOT', 'OBJROOT', 'DSTROOT', 'CONFIGURATION_BUILD_DIR', 'CONFIGURATION_TEMP_DIR',
                'PROJECT_TEMP_DIR', 'SHARED_PRECOMPS_DIR', 'MODULE_CACHE_DIR', 'CLANG_MODULE_CACHE_PATH',
                'SWIFT_MODULE_CACHE_PATH', 'COMPILATION_CACHE_CAS_PATH'}
    present = set()
    for i, item in enumerate(args):
        flag, equals, value = item.partition('=')
        if flag in path_flags:
            if not equals and i + 1 >= len(args):
                raise ValueError('Missing Xcode output argument: ' + flag)
            value = value if equals else args[i + 1]
            storage.validate(value, app=path_flags[flag])
            present.add(flag)
        if equals and (flag in settings or flag.endswith('_DIR') or flag.endswith('_PATH')):
            # Toolchain/signing inputs are selectors, not generated output.
            if flag in settings:
                storage.validate(value, app=flag in {'SYMROOT', 'CONFIGURATION_BUILD_DIR'})
    if '-version' not in args and '-help' not in args and '-create-xcframework' not in args:
        cache = storage.paths['cache']
        defaults = {'-derivedDataPath': cache / 'DerivedData.noindex',
                    '-clonedSourcePackagesDirPath': cache / 'SourcePackages',
                    '-packageCachePath': cache / 'swift-packages'}
        for flag, value in defaults.items():
            if flag not in present:
                args += [flag, str(storage.validate(value, app=flag == '-derivedDataPath'))]
        args += ['CLANG_MODULE_CACHE_PATH=' + str(cache / 'clang-modules'),
                 'SWIFT_MODULE_CACHE_PATH=' + str(cache / 'swift-modules'),
                 'COMPILATION_CACHE_CAS_PATH=' + str(cache / 'cas')]
    return ['xcodebuild', *args]


def gradle_command(storage, args):
    args = [str(a) for a in args]
    path_flags = {'-g', '--gradle-user-home', '--project-cache-dir', '-p', '--project-dir'}
    seen = set()
    for i, item in enumerate(args):
        flag, sep, value = item.partition('=')
        if flag in path_flags:
            if not sep and i + 1 >= len(args):
                raise ValueError('Missing Gradle output argument: ' + flag)
            value = value if sep else args[i + 1]
            storage.validate(value)
            seen.add(flag)
        elif item.startswith('-g') and not item.startswith('--') and len(item) > 2:
            storage.validate(item[2:]); seen.add('-g')
        elif item.startswith('-p') and not item.startswith('--') and len(item) > 2:
            storage.validate(item[2:]); seen.add('-p')
        elif item.startswith(('-Dgradle.user.home=', '-Djava.io.tmpdir=', '-Dorg.gradle.project.buildDir=')):
            storage.validate(item.split('=', 1)[1])
            if item.startswith('-Dgradle.user.home='):
                seen.add('--gradle-user-home')
        elif storage.required and (flag in ('-I', '--init-script') or item.startswith('-I')):
            raise ValueError('Custom Gradle init scripts bypass checked output configuration')
    if '--project-cache-dir' not in seen:
        args += ['--project-cache-dir', str(storage.paths['cache'] / 'gradle-project')]
    if not seen.intersection({'-g', '--gradle-user-home'}):
        args += ['--gradle-user-home', str(storage.validate(os.environ.get('GRADLE_USER_HOME', storage.paths['cache'] / 'gradle')))]
    return args


def output(value, *, app=False):
    return Storage().validate(value, app=app)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('shell', 'prepare', 'validate', 'validate-app', 'validate-legacy', 'validate-legacy-app', 'xcode', 'gradle', 'gradle-check'))
    parser.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    storage = Storage()
    if args.command in ('validate', 'validate-app', 'validate-legacy', 'validate-legacy-app'):
        for value in args.args:
            if args.command.startswith('validate-legacy') and not storage.required:
                print(value)
            else:
                print(storage.validate(value, app=args.command in ('validate-app', 'validate-legacy-app')))
        return
    if args.command == 'gradle-check':
        gradle_command(storage, args.args[1:] if args.args[:1] == ['--'] else args.args)
        storage.environment()
        return
    if args.command == 'gradle':
        command = gradle_command(storage, args.args[1:] if args.args[:1] == ['--'] else args.args)
        env = storage.environment()
        storage.prepare(env)
        options = shlex.split(os.environ.get('JAVA_TOOL_OPTIONS', ''))
        for item in options:
            if item.startswith('-Djava.io.tmpdir='):
                storage.validate(item.split('=', 1)[1])
        options += ['-Djava.io.tmpdir=' + env['TMPDIR']]
        env['JAVA_TOOL_OPTIONS'] = shlex.join(options)
        raise SystemExit(subprocess.run(command, env={**os.environ, **env}).returncode)
    if args.command == 'xcode':
        command = xcode_command(storage, args.args[1:] if args.args[:1] == ['--'] else args.args)
        env = storage.environment()
        storage.prepare(env)
        raise SystemExit(subprocess.run(command, env={**os.environ, **env}).returncode)
    env = storage.environment()
    if args.command == 'prepare':
        storage.prepare(env)
    else:
        for key, value in env.items():
            print('export ' + key + '=' + shlex.quote(value))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit('Build storage refused: ' + str(error))
