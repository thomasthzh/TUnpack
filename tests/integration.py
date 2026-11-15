"""Real archive tests. Creates and retains its own fixtures; never deletes inputs."""
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/bin/unpack_cli.exe"
SEVEN = Path(r"C:\TOOL\7-Zip-Zstandard\7z.exe")
RUN = ROOT / "test-data" / time.strftime("%Y%m%d-%H%M%S")
RUN.mkdir(parents=True, exist_ok=False)
OUTPUT = RUN / "outputs"
OUTPUT.mkdir()
REPORT = []


def zipbytes(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    return stream.getvalue()


def fixture(name, data):
    path = RUN / name
    path.write_bytes(data)
    return path


def run(command, files, *, expected=True, password=None, extra=(), book=None):
    args = [str(CLI), command, *map(str, files), "--out", str(OUTPUT), "--keep", "--book", str(book or RUN / "isolated.dpapi"), *extra]
    if password is not None:
        pwfile = RUN / "sample-password.txt"
        pwfile.write_text(password + "\n", encoding="utf-8")
        args += ["--password-file", str(pwfile)]
    proc = subprocess.run(args, capture_output=True, timeout=50)
    result = json.loads(proc.stdout.decode("utf-8"))
    assert result["ok"] == expected, result
    assert proc.returncode == (0 if expected else 1), result
    return result


def case(name, function):
    start = time.perf_counter()
    try:
        detail = function()
        REPORT.append({"name": name, "ok": True, "wall_ms": round((time.perf_counter()-start)*1000, 2), "result": detail})
        print("PASS", name, flush=True)
    except Exception as error:
        REPORT.append({"name": name, "ok": False, "error": str(error)})
        print("FAIL", name, str(error), flush=True)


payload = {"hello.txt": b"LightUnpack\n", "folder/empty.txt": b"", "folder/\u4e2d\u6587.txt": "\u6d4b\u8bd5\u6210\u529f".encode()}
plain_data = zipbytes(payload)
plain = fixture("normal.zip", plain_data)
renamed = fixture("renamed.mp4", plain_data)
fake_doc = fixture("fake.docx", plain_data)


def assert_contents(result):
    folder = Path(result["output"])
    for name, data in payload.items():
        assert (folder / name).read_bytes() == data, (name, result)
    return result


case("plain ZIP + unicode + zero-length file", lambda: assert_contents(run("extract", [plain])))
case("ZIP renamed to MP4", lambda: assert_contents(run("extract", [renamed])))
case("ZIP disguised as DOCX", lambda: assert_contents(run("extract", [fake_doc])))
case("existing output preserved and renamed", lambda: assert_contents(run("extract", [plain])))
def verify_without_output():
    before=list(OUTPUT.rglob("*"))
    result=run("inspect",[plain],extra=["--verify"])
    assert result["status"]=="verified" and not result["output"] and list(OUTPUT.rglob("*"))==before,result
    return result
case("full SDK integrity test creates no extraction files",verify_without_output)

prefix = b"\x00\x00\x00\x18ftypisom\x00\x00\x02\x00isomiso2" + b"X" * (10 * 1024 * 1024 - 24)
embedded = fixture("embedded.mp4", prefix + plain_data)
case("archive beyond 8MiB SFX window", lambda: assert_contents(run("extract", [embedded])))
case("shallow mode locates ZIP from tail without a carrier scan", lambda: run("inspect", [embedded], extra=["--shallow"]))
boundary = fixture("boundary.mp4", b"A" * (4 * 1024 * 1024 - 2) + plain_data)
case("signature across scan block boundary", lambda: assert_contents(run("extract", [boundary])))
for mask_size in (64, 4096):
    mask = b"\x00\x00\x00\x18ftypisom" + b"M" * (mask_size - 12)
    hidden = fixture(f"apate-{mask_size}.mp4", mask + plain_data[mask_size:] + plain_data[:mask_size][::-1] + struct.pack("<I", mask_size))
    case(f"apate recovery L={mask_size}", lambda hidden=hidden: assert_contents(run("extract", [hidden])))
    def restored(hidden=hidden):
        result = run("restore", [hidden])
        assert Path(result["output"]).read_bytes() == plain_data
        assert hidden.exists()
        return result
    case(f"apate lossless save L={mask_size}", restored)

office = fixture("normal.docx", zipbytes({"[Content_Types].xml": b"<Types/>", "word/document.xml": b"<document/>", "_rels/.rels": b""}))
case("real document identified", lambda: run("inspect", [office]))
case("real document not unpacked or recycled", lambda: run("extract", [office], expected=False))
case("explicit document container extraction", lambda: run("extract", [office], extra=["--container"]))

traversal = fixture("bad.zip", zipbytes({"../outside.txt": b"must not escape"}))
case("path traversal rejected", lambda: run("extract", [traversal], expected=False))
duplicate = RUN / "duplicate.zip"
with zipfile.ZipFile(duplicate, "w") as z:
    z.writestr("same.txt", "first")
    z.writestr("SAME.txt", "second")
case("case-insensitive duplicate paths rejected", lambda: run("extract", [duplicate], expected=False))

nested = fixture("outer.zip", zipbytes({"inner.zip": plain_data, "real.docx": office.read_bytes(), "blank.txt": b""}))
def nested_check():
    result = run("extract", [nested])
    out = Path(result["output"])
    assert (out / "inner/hello.txt").read_bytes() == payload["hello.txt"]
    assert (out / "real.docx").read_bytes() == office.read_bytes()
    assert not (out / "real").exists()
    assert (out / "blank.txt").exists()
    return result
case("nested extraction retains normal documents", nested_check)
nested_hidden = fixture("outer-hidden.zip", zipbytes({"hidden.mp4": b"video-prefix" + plain_data}))
case("nested carrier detected", lambda: run("extract", [nested_hidden]))

pieces = [fixture(f"ordered-{i}.docx", plain_data[i*100:(i+1)*100] if i<2 else plain_data[i*100:]) for i in range(3)]
case("manually ordered renamed raw volumes", lambda: assert_contents(run("extract", pieces)))
independent=[fixture("independent-a.zip",zipbytes({"a.txt":b"A"})),fixture("independent-b.zip",zipbytes({"b.txt":b"B"}))]
def used_input_range():
    result=run("extract",independent)
    assert (Path(result["output"])/"b.txt").read_bytes()==b"B",result
    assert [Path(p) for p in result["sources"]]==[independent[1]],result
    return result
case("tail-located archive never marks skipped concatenated files for cleanup",used_input_range)
standard_parts = [fixture(f"split.7z.{i+1:03}", p.read_bytes()) for i, p in enumerate(pieces)]
case("auto resolve .003 to first volume", lambda: assert_contents(run("extract", [standard_parts[2]])))
missing = fixture("absent.7z.003", pieces[2].read_bytes())
case("missing first volume reported", lambda: run("extract", [missing], expected=False))
case("ordinary media is not an archive", lambda: run("inspect", [fixture("ordinary.mp4", prefix)], expected=False))

sample = fixture("sample.txt", b"encrypted sample\n" * 4096)
special = '\u5bc6\u7801 & "quotes" ^ 123'
for fmt, extra in (("7z", ["-mhe=on"]), ("zip", ["-mem=AES256"])):
    special = '\u5bc6\u7801 & "quotes" ^ 123' if fmt == "7z" else 'spaces & "quotes" ^ 123'
    archive = RUN / ("encrypted." + fmt)
    run("create"+fmt,[archive,sample],password=special)
    case("encrypted " + fmt + " wrong password", lambda archive=archive: run("extract", [archive], expected=False, password="wrong", book=RUN/"wrong.dpapi"))
    def decrypt(archive=archive):
        result = run("extract", [archive], password=special, book=RUN/"remember.dpapi")
        assert (Path(result["output"])/"sample.txt").read_bytes() == sample.read_bytes()
        return result
    case("encrypted " + fmt + " special-character password", decrypt)
    case("saved " + fmt + " password reused", lambda archive=archive: run("extract", [archive], book=RUN/"remember.dpapi"))

truncated = fixture("incomplete-first.mp4", (RUN/"encrypted.7z").read_bytes()[:40])
def incomplete_header():
    result=run("inspect",[truncated],expected=False)
    expected=(RUN/"encrypted.7z").stat().st_size-40
    assert result["type"]=="7z" and str(expected) in result["note"] and result["attempts"]==0,result
    return result
case("valid 7z first piece reports exact missing bytes without password attempts",incomplete_header)
reversed_archive=fixture("reversed-partial.mp4",(RUN/"encrypted.7z").read_bytes()[32:-4][::-1])
def reverse_extract():
    result=run("extract",[reversed_archive],book=RUN/"remember.dpapi")
    assert result["recovered"] and (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    return result
case("reversed encrypted 7z with truncated signature and CRC trailer",reverse_extract)
reversed_missing=fixture("reversed-missing-body.mp4",(RUN/"encrypted.7z").read_bytes()[48:-4][::-1])
case("recovery never fabricates missing compressed body bytes",lambda:run("extract",[reversed_missing],expected=False,book=RUN/"remember.dpapi"))
numbered_data=(RUN/"encrypted.7z").read_bytes()
numbered_parts=[fixture("scene01.mp4",numbered_data[:40]),fixture("scene02.mp4",numbered_data[40:])]
extra_numbered=fixture("scene03.mp4",b"unrelated continuation must remain outside the closed archive")
def numbered_extract(part):
    result=run("extract",[part],book=RUN/"remember.dpapi")
    assert (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    assert [Path(p) for p in result["sources"]]==numbered_parts,result
    return result
case("content validated numeric MP4 volumes from first piece",lambda:numbered_extract(numbered_parts[0]))
case("content validated numeric MP4 volumes from second piece",lambda:numbered_extract(numbered_parts[1]))
fake_parts=[fixture("decoy01.mp4",numbered_data[:40]),fixture("decoy02.mp4",b"X"*len(numbered_data[40:]))]
def invalid_join():
    result=run("inspect",[fake_parts[0]],expected=False)
    assert result["attempts"]==0 and result["type"]=="7z",result
    return result
case("same size numbered decoy never accepted without header CRC",invalid_join)
unrelated_numbered=fixture("scene99.mp4",b"unrelated file outside the verified volume group")
case("numeric filename outside closed group is not silently replaced with first piece",lambda:run("inspect",[unrelated_numbered],expected=False))
cross_folder=RUN/"cross-suffix";cross_folder.mkdir()
cross_parts=[cross_folder/"original.7z.001",cross_folder/"original.7z.002",cross_folder/"renamed.docx"]
for path,data in zip(cross_parts,[numbered_data[:80],numbered_data[80:160],numbered_data[160:]]):path.write_bytes(data)
def cross_suffix(selected):
    result=run("extract",[selected],book=RUN/"remember.dpapi")
    assert (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    assert [Path(p) for p in result["sources"]]==cross_parts,result
    return result
case("first volume discovers unique renamed .003 by size and header CRC",lambda:cross_suffix(cross_parts[0]))
case("renamed DOCX continuation discovers first and second volumes",lambda:cross_suffix(cross_parts[2]))
cross_duplicate=cross_folder/"another-renamed.pdf";cross_duplicate.write_bytes(cross_parts[2].read_bytes())
def ambiguous_tail():
    result=run("inspect",[cross_parts[0]],expected=False)
    assert result["status"]=="ambiguous_volume",result
    return result
case("multiple matching renamed tails require explicit selection",ambiguous_tail)
case("explicit renamed tail disambiguates identical alternatives",lambda:cross_suffix(cross_parts[2]))
numbered_cross=RUN/"numbered-cross";numbered_cross.mkdir()
different_extensions=[numbered_cross/"set.7z.001",numbered_cross/"set.7z.002.pdf",numbered_cross/"set.7z.003.docx"]
for path,data in zip(different_extensions,[numbered_data[:80],numbered_data[80:160],numbered_data[160:]]):path.write_bytes(data)
def numbered_cross_check():
    result=run("extract",[different_extensions[2]],book=RUN/"remember.dpapi")
    assert [Path(p) for p in result["sources"]]==different_extensions,result
    assert (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    return result
case("numbered volumes may each use a different disguise extension",numbered_cross_check)
middle_folder=RUN/"renamed-middle";middle_folder.mkdir()
middle_parts=[middle_folder/"middle.7z.001",middle_folder/"middle.7z.002",middle_folder/"opaque.docx",middle_folder/"middle.7z.004"]
for path,data in zip(middle_parts,[numbered_data[:64],numbered_data[64:128],numbered_data[128:192],numbered_data[192:]]):path.write_bytes(data)
def renamed_middle():
    result=run("extract",[middle_parts[2]],book=RUN/"remember.dpapi")
    assert [Path(p) for p in result["sources"]]==middle_parts,result
    assert (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    return result
case("unique renamed middle .003 is inserted before known .004",renamed_middle)
ambiguous_first=RUN/"ambiguous-first";ambiguous_first.mkdir()
for name in ("a.7z.001","b.7z.001"):(ambiguous_first/name).write_bytes(numbered_data[:80])
ambiguous_first_tail=ambiguous_first/"unknown.docx";ambiguous_first_tail.write_bytes(numbered_data[80:])
def ambiguous_first_check():
    result=run("inspect",[ambiguous_first_tail],expected=False)
    assert result["status"]=="ambiguous_volume",result
    return result
case("renamed tail matching multiple first volumes is ambiguous",ambiguous_first_check)
complete_first=RUN/"complete-first";complete_first.mkdir()
(complete_first/"complete.001").write_bytes(numbered_data)
unrelated_piece=complete_first/"complete.002";unrelated_piece.write_bytes(b"unrelated continuation")
def wrong_selected_piece():
    result=run("extract",[unrelated_piece],expected=False,book=RUN/"remember.dpapi")
    assert not result["output"] and not result["sources"],result
    return result
case("selected unrelated .002 never processes a complete .001 instead",wrong_selected_piece)
standalone_numeric=fixture("standalone.003",plain_data)
case("complete ZIP content overrides misleading numeric extension",lambda:assert_contents(run("extract",[standalone_numeric])))
ambiguous_middle=middle_folder/"alternative.pdf";ambiguous_middle.write_bytes(b"X"*len(middle_parts[2].read_bytes()))
def middle_ambiguity():
    result=run("inspect",[middle_parts[0]],expected=False)
    assert result["status"]=="ambiguous_volume",result
    return result
case("multiple same-size middle candidates are never chosen arbitrarily",middle_ambiguity)

body_only=RUN/"body-only.7z"
subprocess.run([str(SEVEN),"a","-t7z","-mhe=off","-pbody-right",str(body_only),str(sample)],check=True,capture_output=True)
candidate_text=fixture("public-test-candidates.txt",b"body-wrong-one\nbody-wrong-two\nbody-right\n")
body_book=RUN/"body-candidates.dpapi"
run("import-passwords",[candidate_text],book=body_book)
def body_candidates():
    result=run("extract",[body_only],book=body_book)
    assert (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    assert result["attempts"]==3,result
    return result
case("body password errors never bypass later candidate validation",body_candidates)
partial_body=fixture("known-format.7z.002",body_only.read_bytes()[64:])
def continuation_metadata():
    result=run("inspect",[partial_body],expected=False)
    assert result["type"]=="7z" and result["status"]=="missing_volume" and "64" in result["note"],result
    return result
case("7z continuation tail identifies original format and exact missing prefix",continuation_metadata)
headless=fixture("headless-encrypted.docx",(RUN/"encrypted.7z").read_bytes()[32:])
def headless_decrypt():
    result=run("extract",[headless],book=RUN/"remember.dpapi")
    assert result["recovered"] and (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    return result
case("missing signature block is rebuilt using intact encoded tail",headless_decrypt)
masked_bytes=b"X"*32+(RUN/"encrypted.7z").read_bytes()[32:]
masked_header=fixture("covered-header.docx",masked_bytes)
def covered_decrypt(file=masked_header):
    result=run("extract",[file],book=RUN/"remember.dpapi")
    assert result["recovered"] and (Path(result["output"])/sample.name).read_bytes()==sample.read_bytes(),result
    return result
case("overwritten 32-byte archive header rebuilt from intact tail",covered_decrypt)
masked_carrier=fixture("covered-carrier.mp4",b"media prefix"*31+masked_bytes)
case("tail metadata locates header-covered archive inside carrier",lambda:covered_decrypt(masked_carrier))
covered_outer=fixture("covered-nested.zip",zipbytes({"covered.docx":masked_bytes}))
def covered_nested():
    result=run("extract",[covered_outer],book=RUN/"remember.dpapi")
    assert result["archives"]==2 and result["recovered"],result
    assert (Path(result["output"])/"covered"/sample.name).read_bytes()==sample.read_bytes(),result
    assert masked_header.read_bytes()==masked_bytes
    return result
case("automatic nesting shares header recovery without changing source",covered_nested)
empty_sample=fixture("public-empty.txt",b"")
# 7z writes ordinary zero-byte ZIP entries without encryption. Build a valid AE-2
# empty stored entry to exercise password verification even when output is empty.
import hashlib
import hmac
salt=bytes(range(16))
keys=hashlib.pbkdf2_hmac("sha1",b"body-right",salt,1000,66)
encrypted_empty=salt+keys[64:]+hmac.new(keys[32:64],b"","sha1").digest()[:10]
entry_name=b"public-empty.txt"
aes_extra=struct.pack("<HHH2sBH",0x9901,7,2,b"AE",3,0)
local=struct.pack("<I5H3I2H",0x04034b50,51,1,99,0,0,0,len(encrypted_empty),0,len(entry_name),len(aes_extra))+entry_name+aes_extra+encrypted_empty
central=struct.pack("<I6H3I5H2I",0x02014b50,51,51,1,99,0,0,0,len(encrypted_empty),0,len(entry_name),len(aes_extra),0,0,0,0,0)+entry_name+aes_extra
end=struct.pack("<I4H2IH",0x06054b50,0,0,1,1,len(central),len(local),0)
empty_zip=fixture("empty-encrypted.zip",local+central+end)
empty_book=RUN/"empty-candidates.dpapi"
run("import-passwords",[candidate_text],book=empty_book)
def empty_encrypted():
    result=run("extract",[empty_zip],book=empty_book)
    assert (Path(result["output"])/empty_sample.name).read_bytes()==b"",result
    assert result["encrypted"] and result["attempts"]>=1,result
    return result
case("encrypted empty-only archive can use a later saved candidate",empty_encrypted)

def aes_stored_zip(data,password,encoding="utf-8"):
    from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
    keys=hashlib.pbkdf2_hmac("sha1",password.encode(encoding),salt,1000,66)
    encoder=Cipher(algorithms.AES(keys[:32]),modes.ECB()).encryptor()
    stream=encoder.update(b"".join(struct.pack("<Q",n)+b"\0"*8 for n in range(1,(len(data)+15)//16+1)))+encoder.finalize()
    encrypted=bytes(a^b for a,b in zip(data,stream))
    body=salt+keys[64:]+encrypted+hmac.new(keys[32:64],encrypted,"sha1").digest()[:10]
    name=b"utf8.txt"
    local=struct.pack("<I5H3I2H",0x04034b50,51,1,99,0,0,0,len(body),len(data),len(name),len(aes_extra))+name+aes_extra+body
    central=struct.pack("<I6H3I5H2I",0x02014b50,51,51,1,99,0,0,0,len(body),len(data),len(name),len(aes_extra),0,0,0,0,0)+name+aes_extra
    return local+central+struct.pack("<I4H2IH",0x06054b50,0,0,1,1,len(central),len(local),0)
utf8_password="中文密码123"
utf8_archive=fixture("utf8-password.jpg",b"\xff\xd8\xffJPEG carrier"+aes_stored_zip(b"UTF8 password payload",utf8_password)+b"trailing media junk"*1000)
def utf8_decrypt(saved=False):
    result=run("extract",[utf8_archive],password=None if saved else utf8_password,book=RUN/"utf8-remember.dpapi")
    assert (Path(result["output"])/"utf8.txt").read_bytes()==b"UTF8 password payload",result
    return result
case("UTF8 Chinese ZIP password without UTF8 flag and with media trailer",utf8_decrypt)
case("ZIP remembers original Unicode password after native encoding adaptation",lambda:utf8_decrypt(True))
legacy_unicode=fixture("legacy-unicode.zip",aes_stored_zip(b"legacy codepage payload",utf8_password,"mbcs"))
case("system-codepage Chinese ZIP password remains compatible",lambda:run("extract",[legacy_unicode],password=utf8_password,book=RUN/"legacy-unicode.dpapi"))
def descriptor_then_encrypted():
    import zlib
    wire=aes_stored_zip(b"descriptor UTF8 payload",utf8_password)
    _,_,_,_,_,cd_size,cd_offset,_=struct.unpack("<I4H2IH",wire[-22:])
    name=b"first.txt";data=b"unencrypted descriptor item";crc=zlib.crc32(data)
    local=struct.pack("<I5H3I2H",0x04034b50,20,8,0,0,0,0,0,0,len(name),0)+name+data+struct.pack("<4I",0x08074b50,crc,len(data),len(data))
    first_cd=struct.pack("<I6H3I5H2I",0x02014b50,20,20,8,0,0,0,crc,len(data),len(data),len(name),0,0,0,0,0,0)+name
    second_cd=bytearray(wire[cd_offset:cd_offset+cd_size]);struct.pack_into("<I",second_cd,42,len(local))
    central=first_cd+second_cd
    archive=fixture("descriptor-first.mp4",local+wire[:cd_offset]+central+struct.pack("<I4H2IH",0x06054b50,0,0,2,2,len(central),len(local)+cd_offset,0))
    candidates=fixture("descriptor-candidates.txt",("\n".join("wrong-"+str(i) for i in range(32))+"\n"+utf8_password+"\n").encode("utf-8"))
    book=RUN/"descriptor.dpapi";run("import-passwords",[candidates],book=book)
    result=run("extract",[archive],book=book)
    assert (Path(result["output"])/"utf8.txt").read_bytes()==b"descriptor UTF8 payload" and result["attempts"]==1,result
    return result
case("central directory locates encrypted item after unencrypted descriptor",descriptor_then_encrypted)

def zip64_fixture():
    plain=plain_data;end=len(plain)-22
    _,_,_,entries,_,size,offset,_=struct.unpack("<I4H2IH",plain[end:])
    record=struct.pack("<IQ2H2I4Q",0x06064b50,44,45,45,0,0,entries,entries,size,offset)
    locator=struct.pack("<IIQI",0x07064b50,0,end,1)
    eocd=struct.pack("<I4H2IH",0x06054b50,0,0,0xffff,0xffff,0xffffffff,0xffffffff,0)
    return fixture("zip64-carrier.jpg",b"JPEG prefix"*100+plain[:end]+record+locator+eocd+b"media tail"*100)
case("ZIP64 tail directory locates prefix and ignores appended media",lambda:assert_contents(run("extract",[zip64_fixture()])))
scan_folder=RUN/"content-scan";scan_folder.mkdir();(scan_folder/"nested").mkdir()
(scan_folder/"changed.jpg").write_bytes(plain_data)
(scan_folder/"normal.docx").write_bytes(office.read_bytes())
(scan_folder/"ordinary.mp4").write_bytes(prefix)
(scan_folder/"signature-constants.dll").write_bytes(b"MZ binary data PK\x03\x04 invalid header 7z\xbc\xaf\x27\x1c invalid CRC")
(scan_folder/"nested"/"second.pdf").write_bytes(plain_data)
def scan_content(flat):
    result=run("scan",[scan_folder],extra=["--flat"] if flat else [])
    assert result["archives"]==(1 if flat else 2) and result["scanned_files"]==(4 if flat else 5),result
    return result
case("folder scan identifies content, skips documents and signature literals",lambda:scan_content(True))
case("recursive scan shares the same content detection",lambda:scan_content(False))
def scan_disguised_volume_batches():
    batch=RUN/"volume-batch";batch.mkdir();expected=set()
    for name in ("a","b"):
        folder=batch/name;folder.mkdir()
        parts=[folder/"same.7z.001",folder/"same.7z.002",folder/"renamed.docx"]
        for path,data in zip(parts,[numbered_data[:80],numbered_data[80:160],numbered_data[160:]]):path.write_bytes(data)
        expected.update(map(str,parts))
        (folder/"ordinary01.bin").write_bytes(b"ordinary numbered file")
        (folder/"ordinary.dat").write_bytes(b"ordinary data")
    import ctypes
    kernel=ctypes.WinDLL("kernel32",use_last_error=True)
    kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
    kernel.CreateFileW.restype=ctypes.c_void_p
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    locked=kernel.CreateFileW(str(batch/"a"/"ordinary01.bin"),0x80000000,0,None,3,0x80,None)
    assert locked!=ctypes.c_void_p(-1).value,ctypes.get_last_error()
    try:result=run("scan",[batch],book=RUN/"remember.dpapi")
    finally:kernel.CloseHandle(locked)
    assert result["archives"]==2 and result["scanned_files"]==10,result
    assert {path for entry in result["entries"] for path in entry["files"]}==expected,result
    assert all(entry["ok"] for entry in result["entries"]),result
    return result
case("batch scan isolates directories and skips locked unrelated candidates",scan_disguised_volume_batches)
literal_outer=fixture("literal-library.zip",zipbytes({"signature-constants.dll":(scan_folder/"signature-constants.dll").read_bytes()}))
def nested_literals():
    result=run("extract",[literal_outer])
    assert result["archives"]==1 and not result["failed_file"],result
    assert (Path(result["output"])/"signature-constants.dll").read_bytes()==(scan_folder/"signature-constants.dll").read_bytes(),result
    return result
case("nested scan never treats binary signature constants as an archive",nested_literals)

for fmt in ("7z", "zip"):
    def create(fmt=fmt):
        target = RUN / ("created."+fmt)
        result = run("create"+fmt, [target, sample])
        check = run("extract", [Path(result["output"])])
        assert (Path(check["output"])/"sample.txt").read_bytes() == sample.read_bytes()
        return check
    case("create and read back " + fmt, create)

FIXTURES = ROOT / "tests/fixtures"
if FIXTURES.exists():
    import zlib
    def rar_check(files, password=None):
        result = run("extract", files, password=password)
        out = Path(result["output"])
        cloud = out / "clouds.jpg"
        if cloud.exists():
            assert len(cloud.read_bytes()) == 478883
            assert zlib.crc32(cloud.read_bytes()) == 0x515D4B66
        else:
            italy = next(out.rglob("italy.svg"))
            assert len(italy.read_bytes()) == 267
            assert zlib.crc32(italy.read_bytes()) == 0x2214F4E9
        return result
    case("RAR4 single archive", lambda: rar_check([FIXTURES/"single_file/clouds.jpg.rar4.rar"]))
    for name in ("encrypted/encrypted.rar4.rar", "encrypted/encrypted.rar5.rar", "header_encrypted/header_encrypted.rar5.rar"):
        case(name, lambda name=name: rar_check([FIXTURES/name], "helloworld"))
    case("RAR5 native volumes from part3", lambda: rar_check([FIXTURES/"split/clouds.jpg.part3.rar"]))
    case("RAR4 native volumes from r01", lambda: rar_check([FIXTURES/"split/clouds.jpg.r01"]))
    renamed_rar = [fixture(f"renamed-rar-{i}.docx", (FIXTURES/f"split/clouds.jpg.part{i}.rar").read_bytes()) for i in (1,2,3)]
    case("renamed RAR native volume mapping", lambda: rar_check(renamed_rar))
    mixed_parts=[fixture("auto.part"+str(i)+ext,(FIXTURES/f"split/clouds.jpg.part{i}.rar").read_bytes()) for i,ext in ((1,".jpg"),(2,".txt"),(3,".docx"))]
    case("auto group renamed RAR parts from first volume",lambda:rar_check([mixed_parts[0]]))
    case("auto group renamed RAR parts from last volume",lambda:rar_check([mixed_parts[2]]))
    duplicate_part=fixture("auto.part2.pdf",mixed_parts[1].read_bytes())
    case("ambiguous renamed RAR part number requires explicit selection",lambda:run("extract",[mixed_parts[0]],expected=False))
    gap_parts=[fixture(f"gap.part{i}.jpg",(FIXTURES/f"split/clouds.jpg.part{i}.rar").read_bytes()) for i in (1,3)]
    case("missing middle renamed RAR volume reported",lambda:run("extract",[gap_parts[0]],expected=False))
    renamed_old = [fixture(f"renamed-old-rar-{i}.docx", (FIXTURES/("split/clouds.jpg."+ext)).read_bytes()) for i,ext in enumerate(("rar","r00","r01"))]
    case("renamed RAR4 old-style native volumes", lambda: rar_check(renamed_old))
    numbered=[fixture(f"data{i}.rar",(FIXTURES/f"split/clouds.jpg.part{i}.rar").read_bytes()) for i in (1,2,3)]
    decoys=[fixture("data1."+ext,b"unrelated file, must never be recycled") for ext in ("r00","r01")]
    def actual_participants():
        result=rar_check([numbered[0]])
        assert [Path(p).resolve() for p in result["sources"]]==[p.resolve() for p in numbered],result
        assert all(p.read_bytes()==b"unrelated file, must never be recycled" for p in decoys)
        return result
    case("native RAR records real volume paths instead of guessed decoys",actual_participants)
    for label,volumes in (("RAR5",renamed_rar),("RAR4",renamed_old)):
        def restore_volumes(volumes=volumes):
            result=run("restore",volumes)
            folder=Path(result["output"])
            first=folder/("archive.rar" if (folder/"archive.rar").exists() else "archive.part1.rar")
            return rar_check([first])
        case("restore complete native volume set "+label,restore_volumes)

for fmt in ("gzip", "bzip2", "xz", "zstd", "lz4", "lz5", "lizard"):
    target = RUN / ("codec." + fmt)
    creation = subprocess.run([str(ROOT/"tools/7zip-zs-26.05/7z.exe"), "a", "-t"+fmt, str(target), str(sample)], capture_output=True)
    if creation.returncode:
        REPORT.append({"name": "codec " + fmt, "ok": False, "error": "fixture creation failed"})
    else:
        def codec_check(target=target):
            result=run("extract",[target])
            outputs=list(Path(result["output"]).rglob("*"))
            assert any(p.is_file() and p.read_bytes()==sample.read_bytes() for p in outputs)
            return result
        case("codec " + fmt,codec_check)

def cancellations():
    result=subprocess.run([str(ROOT/"build/bin/unpack_checks.exe"),str(plain),str(embedded)],capture_output=True,timeout=20)
    assert result.returncode==0,result.stdout.decode("utf8")
    return result.stdout.decode("utf8").strip()
case("cancellation during scan and before completion",cancellations)

legacy_zip=RUN/"zipcrypto.zip"
subprocess.run([str(ROOT/"tools/7zip-zs-26.05/7z.exe"),"a","-tzip","-mem=ZipCrypto","-plegacy-password",str(legacy_zip),str(sample)],check=True,capture_output=True)
case("ZipCrypto wrong candidate rejected",lambda:run("extract",[legacy_zip],password="wrong",expected=False,book=RUN/"legacy-wrong.dpapi"))
case("ZipCrypto header precheck with full validation",lambda:run("extract",[legacy_zip],password="legacy-password",book=RUN/"legacy-right.dpapi"))
bom=fixture("bom-password.txt",b"\xef\xbb\xbflegacy-password\r\n")
case("UTF-8 BOM password input",lambda:run("extract",[legacy_zip],extra=["--password-file",str(bom)],book=RUN/"bom.dpapi"))

import tarfile
tar_path=RUN/"normal.tar"
with tarfile.open(tar_path,"w") as tar:
    item=tarfile.TarInfo("hello.txt");item.size=len(payload["hello.txt"]);tar.addfile(item,io.BytesIO(payload["hello.txt"]))
def tar_check():
    result=run("extract",[tar_path]);assert (Path(result["output"])/"hello.txt").read_bytes()==payload["hello.txt"];return result
case("TAR content recognition and extraction",tar_check)

assert plain.exists() and nested.exists() and office.exists()
assert not (OUTPUT / "outside.txt").exists()
report = {"run": str(RUN), "passed": sum(r["ok"] for r in REPORT), "total": len(REPORT), "cases": REPORT}
report_path = RUN / "report.json"
report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
(ROOT / "docs/evidence/integration-latest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"{report['passed']}/{report['total']} passed; {report_path}", flush=True)
sys.exit(0 if report["passed"] == report["total"] else 1)
