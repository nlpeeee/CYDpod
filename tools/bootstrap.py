"""Hash-pinned Windows bootstrap. Python 3.9+ standard library only.

All mutable state is in .tools/ and .deps/. No Git or device commands.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
LOCK = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))
TOOLS = ROOT / '.tools'
DEPS = ROOT / '.deps'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def fetch(item, name):
    dest = TOOLS / 'downloads' / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.exists() or digest(dest) != item['sha256']:
        print('Downloading ' + item['url'], flush=True)
        temporary = dest.with_suffix(dest.suffix + '.partial')
        urllib.request.urlretrieve(item['url'], temporary)
        if digest(temporary) != item['sha256']:
            raise RuntimeError('SHA256 mismatch: ' + name)
        temporary.replace(dest)
    return dest


def unzip(archive, target, strip_root=False):
    # Reject traversal and symlinks; never extract outside the designated folder.
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            parts = Path(member.filename).parts
            if strip_root:
                parts = parts[1:]
            if not parts:
                continue
            dest = target.joinpath(*parts).resolve()
            dest.relative_to(target.resolve())
            if member.external_attr >> 16 & 0o170000 == 0o120000:
                raise RuntimeError('Unexpected archive symlink')
            if member.is_dir():
                dest.mkdir(parents=True, exist_ok=True)
            else:
                dest.parent.mkdir(parents=True, exist_ok=True)
                with z.open(member) as src, dest.open('wb') as out:
                    shutil.copyfileobj(src, out)


def main():
    if os.name != 'nt':
        raise RuntimeError('This lock targets Windows x86_64')
    # Inspect existing caches read-only before any downloads. Reuse only matching
    # archives, never share or modify an existing global installation.
    cache_roots = [Path.home() / 'AppData/Local/Arduino15',
                   Path.home() / 'Documents/Arduino', Path.home() / '.arduino15',
                   Path.home() / '.platformio']
    print('Existing cache inspection: ' + ', '.join(str(p) +
          (' (present)' if p.exists() else ' (absent)') for p in cache_roots))
    TOOLS.mkdir(exist_ok=True)
    DEPS.mkdir(exist_ok=True)
    cli_archive = fetch(LOCK['cli'], 'cli.zip')
    cli_dir = TOOLS / 'arduino-cli'
    if not (cli_dir / 'arduino-cli.exe').exists() or digest(cli_dir / 'arduino-cli.exe') != LOCK['cli']['executableSha256']:
        unzip(cli_archive, cli_dir)
    cli = cli_dir / 'arduino-cli.exe'
    data = DEPS / 'arduino'
    data.mkdir(exist_ok=True)
    for index in LOCK['indexes']:
        source = ROOT / index['path']
        if digest(source) != index['sha256']:
            raise RuntimeError('Pinned index changed: ' + str(source))
        shutil.copyfile(source, data / source.name)
    # Empty library index prevents fetching an unrelated moving library index.
    (data / 'library_index.json').write_text('{"libraries": []}\n', encoding='utf-8')
    for package in LOCK['tools']:
        for system in package['systems']:
            # Arduino itself verifies these pinned archive checksums on install.
            for cache in cache_roots[:3]:
                for folder in [cache / 'staging/packages', cache / 'staging']:
                    candidate = folder / system['archiveFileName']
                    if candidate.is_file() and digest(candidate) == system['checksum'].split(':')[1].lower():
                        stage = TOOLS / 'downloads/arduino/packages'
                        stage.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(candidate, stage / candidate.name)
    config = TOOLS / 'arduino-cli.yaml'
    paths = {k: str(v).replace('\\', '/') for k, v in {
        'data': data, 'downloads': TOOLS / 'downloads/arduino',
        'user': DEPS / 'sketchbook'}.items()}
    config.write_text('board_manager:\n  additional_urls:\n'
                      '    - https://espressif.github.io/arduino-esp32/package_esp32_index.json\n'
                      'directories:\n' + ''.join('  ' + k + ': ' + json.dumps(v) + '\n'
                      for k, v in paths.items()) + 'build_cache:\n  path: ' +
                      json.dumps(str(TOOLS / 'build-cache').replace('\\', '/')) + '\n', encoding='utf-8')
    env = os.environ.copy()
    for key, value in paths.items():
        env['ARDUINO_DIRECTORIES_' + key.upper()] = value
    tmp = TOOLS / 'tmp'
    tmp.mkdir(exist_ok=True)
    env['TEMP'] = env['TMP'] = str(tmp)
    version = subprocess.check_output([str(cli), '--config-file', str(config), 'version'], text=True, env=env)
    if ('Version: ' + LOCK['cli']['version']) not in version:
        raise RuntimeError('Unexpected Arduino CLI: ' + version)
    # No update-index: install from the committed, reduced official indexes.
    subprocess.run([str(cli), '--config-file', str(config), 'core', 'install',
                    LOCK['core']['id'] + '@' + LOCK['core']['version']], env=env, check=True)
    for item in LOCK['libraries']:
        archive = fetch(item, item['name'] + '.zip')
        target = DEPS / 'sketchbook/libraries' / item['name']
        # Reconstruct libraries from checked archives so setup also repairs
        # accidental local edits. Only our isolated library folder is replaced.
        target.resolve().relative_to(DEPS.resolve())
        if target.exists():
            shutil.rmtree(target)
        unzip(archive, target, strip_root=True)
    config_helix = DEPS / 'sketchbook/libraries/arduino-libhelix/src/ConfigHelix.h'
    content = config_helix.read_text()
    original = '#ifndef ESP8266\n#  define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR'
    replacement = ('#if !defined(ESP8266) && !defined(CYDPOD_AAC_LC_ONLY)\n'
                   '// CYDpod: opt-in LC-only experiment; stock upstream behavior is unchanged.\n'
                   '#  define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR')
    config_helix.write_text(content.replace(original, replacement))
    if digest(config_helix) != LOCK['patches'][0]['sha256']:
        raise RuntimeError('Unexpected Helix config patch result')
    tft = ROOT / LOCK['tft']['path']
    if digest(tft) != LOCK['tft']['sha256']:
        raise RuntimeError('TFT config differs from lock')
    shutil.copyfile(tft, DEPS / 'sketchbook/libraries/TFT_eSPI/User_Setup.h')
    print('Pinned local dependencies ready. Run powershell -File tools/check.ps1')


if __name__ == '__main__':
    main()
