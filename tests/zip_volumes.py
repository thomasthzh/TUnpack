"""Native PKZIP disk-relative volumes, distinct from .zip.001 byte slicing.

Only generated fixtures are written; every input and output is retained.
"""
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'build/bin/unpack_cli.exe'
RUN = ROOT / 'test-data' / ('zip-volumes-' + time.strftime('%Y%m%d-%H%M%S'))
PAYLOAD = {'data.bin': bytes(range(256)) * 900, '说明.txt': '分卷正文'.encode(), 'empty.txt': b''}
ORIGINALS = {}


def copy_fixture(source, target):
    shutil.copyfile(source, target)
    ORIGINALS[target] = target.read_bytes()


def split_zip(folder, data, zip64=False):
    folder.mkdir(parents=True)
    end = data.rfind(b'PK\x05\x06')
    directory_size, directory_at = struct.unpack_from('<II', data, end + 12)
    # Adding the spanning marker shifts all local/central positions by four.
    body = bytearray(b'PK\x07\x08' + data[:end])
    span = 65536
    last_disk = len(body) // span
    entries = 0
    at = directory_at + 4
    while at < len(body):
        assert body[at:at + 4] == b'PK\x01\x02'
        local = struct.unpack_from('<I', body, at + 42)[0] + 4
        struct.pack_into('<H', body, at + 34, local // span)
        struct.pack_into('<I', body, at + 42, local % span)
        name, extra, comment = struct.unpack_from('<3H', body, at + 28)
        at += 46 + name + extra + comment
        entries += 1
    directory_disk, directory_offset = divmod(directory_at + 4, span)
    assert directory_disk == last_disk  # This fixture keeps all CD records together.
    eocd = struct.pack('<4s4H2IH', b'PK\x05\x06', last_disk, directory_disk,
                       entries, entries, directory_size, directory_offset, 0)
    if zip64:
        record_offset = len(body) % span
        body += struct.pack('<4sQ2H2I4Q', b'PK\x06\x06', 44, 45, 45, last_disk,
                            directory_disk, entries, entries, directory_size, directory_offset)
        body += struct.pack('<4sIQI', b'PK\x06\x07', last_disk, record_offset, last_disk + 1)
    body += eocd
    paths = []
    for disk in range(last_disk + 1):
        path = folder / ('sample.zip' if disk == last_disk else f'sample.z{disk + 1:02}')
        path.write_bytes(body[disk * span:(disk + 1) * span])
        ORIGINALS[path] = path.read_bytes()
        paths.append(path)
    return paths


def run(command, paths, expected=True, extra=()):
    proc = subprocess.run([str(CLI), command, *map(str, paths), '--keep', '--shallow',
                           '--book', str(RUN / 'isolated.dpapi'), '--out', str(RUN / 'outputs'),
                           *extra], capture_output=True, timeout=40)
    result = json.loads(proc.stdout.decode('utf8'))
    assert result['ok'] == expected and proc.returncode == (0 if expected else 1), result
    return result


def main():
    RUN.mkdir(parents=True)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_STORED) as archive:
        for name, data in PAYLOAD.items():
            archive.writestr(name, data)
    reports = []
    for zip64 in [False, True]:
        paths = split_zip(RUN / ('zip64' if zip64 else 'zip32'), stream.getvalue(), zip64)
        for entry in [paths[-1], paths[0], paths[1]]:
            result = run('inspect', [entry], extra=['--verify'])
            assert result['type'] == 'zip' and result['status'] == 'verified', result
            assert {str(Path(p)) for p in result['sources']} == set(map(str, paths)), result
            reports.append({'zip64': zip64, 'entry': entry.name, 'verified': True})
        scan = run('scan', [paths[0].parent], extra=['--flat'])
        assert scan['archives'] == 1 and len(scan['entries'][0]['files']) == len(paths), scan
        result = run('extract', [paths[1]])
        for name, data in PAYLOAD.items():
            assert (Path(result['output']) / name).read_bytes() == data
        renamed = RUN / ('manual64' if zip64 else 'manual32')
        renamed.mkdir()
        explicit = []
        for i, source in enumerate(paths):
            target = renamed / f'piece-{i}.docx'
            copy_fixture(source, target)
            explicit.append(target)
        result = run('inspect', explicit, extra=['--verify'])
        assert set(result['sources']) == set(map(str, explicit)), result
        restored = run('restore', explicit)
        restored_paths = [Path(restored['output']) / ('archive.zip' if i + 1 == len(paths) else f'archive.z{i + 1:02}') for i in range(len(paths))]
        assert all(p.read_bytes() == source.read_bytes() for p, source in zip(restored_paths, paths))
        assert run('inspect', [restored_paths[-1]], extra=['--verify'])['status'] == 'verified'
        for absent in [0, 1]:
            missing = RUN / (f'missing{64 if zip64 else 32}-{absent}')
            missing.mkdir()
            for i, source in enumerate(paths):
                if i != absent:
                    copy_fixture(source, missing / source.name)
            result = run('inspect', [missing / paths[-1].name], expected=False)
            assert result['type'] == 'zip' and result['status'] == 'missing_volume' and f'z{absent + 1:02}' in result['note'], result
        # A same-stem RAR must not capture valid ZIP .zNN volumes.
        copy_fixture(ROOT / 'tests/fixtures/single_file/clouds.jpg.rar4.rar', paths[0].with_suffix('.rar'))
        assert run('inspect', [paths[1]])['type'] == 'zip'
    missing_tail = RUN / 'missing-tail'
    missing_tail.mkdir()
    for source in paths[:-1]:
        copy_fixture(source, missing_tail / source.name)
    for source in paths[:2]:
        result = run('inspect', [missing_tail / source.name], expected=False)
        assert result['type'] == 'zip' and result['status'] == 'missing_volume' and '.zip' in result['note'], result
    # The mature SDK must still validate encrypted data across disk boundaries.
    public_password = 'public-native-zip-password'
    source = RUN / 'encrypted-data.bin'
    source.write_bytes(PAYLOAD['data.bin'])
    whole = RUN / 'encrypted-whole.zip'
    subprocess.run([str(ROOT / 'tools/7zip-zs-26.05/7z.exe'), 'a', '-tzip', '-mx=0',
                    '-mem=ZipCrypto', '-p' + public_password, str(whole), str(source)],
                   capture_output=True, check=True)
    encrypted = split_zip(RUN / 'encrypted', whole.read_bytes(), True)
    metadata = run('inspect', [encrypted[-1]])
    assert metadata['encrypted'] and not metadata['password_checked'] and metadata['attempts'] == 0
    assert run('extract', [encrypted[-1]], expected=False)['status'] == 'password_required'
    candidates = RUN / 'public-passwords.txt'
    candidates.write_text('public-wrong\n' + public_password + '\n', encoding='utf8')
    run('import-passwords', [candidates])
    result = run('extract', [encrypted[1]])
    assert result['encrypted'] and result['password_checked'] and (Path(result['output']) / source.name).read_bytes() == source.read_bytes(), result
    flow = RUN / 'flow-inbox'
    flow.mkdir()
    for source in paths:
        copy_fixture(source, flow / source.name)
    (RUN / 'expected-data.bin').write_bytes(PAYLOAD['data.bin'])
    subprocess.run([str(ROOT / 'build/bin/flow_checks.exe'), str(RUN), str(ROOT / 'tests/fixtures'), str(flow)], check=True)
    assert ORIGINALS and all(p.exists() and p.read_bytes() == data for p, data in ORIGINALS.items())
    report = ROOT / 'docs/evidence/zip-native-volume/generated.json'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({'entry_checks': reports, 'encrypted': True, 'restore_preserves_disks': True,
                                 'scan_single_group': True, 'missing_parts': True, 'rar_collision': True,
                                 'production_queue_mixed_drop_merges_one_task': True,
                                 'original_sources_compared': len(ORIGINALS), 'all_generated_sources_retained': True}, indent=2), encoding='utf8')
    print('Native ZIP32/ZIP64: tail/first/middle/manual, scan, extract/restore, missing parts, RAR collision and saved encrypted candidate passed; all fixtures retained.')


if __name__ == '__main__':
    main()
