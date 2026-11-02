"""Create WiX source and a Debian binary package from current source-publish directories."""
from __future__ import annotations
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import tarfile
import time
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
VERSION = ET.parse(ROOT / "Directory.Build.props").findtext(".//Version")
EPOCH = int(os.environ.get("SOURCE_DATE_EPOCH", str(int(time.time()))))
WIX = "http://wixtoolset.org/schemas/v4/wxs"
NAMESPACE = uuid.UUID("34f37e7d-a149-49e7-9aec-fef01c53a769")


def identity(prefix, value):
    return prefix + hashlib.sha256(value.encode()).hexdigest()[:24]


def wix_source(payload, output):
    ET.register_namespace("", WIX)
    def child(parent, tag, **attrs):
        return ET.SubElement(parent, "{"+WIX+"}"+tag, attrs)
    root = ET.Element("{"+WIX+"}Wix")
    package = child(root, "Package", Name="OpenAI Usage", Manufacturer="Umberto Giacobbi",
                    Version=VERSION, Language="1033", UpgradeCode=str(NAMESPACE), Scope="perUser",
                    InstallerVersion="500", Compressed="yes")
    child(package, "MajorUpgrade", DowngradeErrorMessage="A newer version of OpenAI Usage is installed.")
    child(package, "MediaTemplate", EmbedCab="yes", CompressionLevel="high")
    # Windows Installer's legacy WindowsBuild value can report 9600 on modern Windows.
    # AppSearch reads the actual build from the native registry view before LaunchConditions.
    os_build = child(package, "Property", Id="OPENAI_OS_BUILD", Secure="yes")
    child(os_build, "RegistrySearch", Id="ReadActualWindowsBuild", Root="HKLM",
          Key="SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", Name="CurrentBuildNumber",
          Type="raw", Bitness="always64")
    child(package, "Launch", Condition="Installed OR (VersionNT64 AND OPENAI_OS_BUILD >= 19041)",
          Message="OpenAI Usage requires 64-bit Windows 10 version 2004 or newer.")
    child(package, "Property", Id="ARPCOMMENTS", Value="Native OpenAI allowance and token-history widget. Uses your existing Codex sign-in.")
    child(package, "Property", Id="ARPNOMODIFY", Value="1")
    child(package, "Icon", Id="AppIcon", SourceFile=str(payload / "OpenAIUsage.Windows.exe"))
    child(package, "Property", Id="ARPPRODUCTICON", Value="AppIcon")
    local = child(package, "StandardDirectory", Id="LocalAppDataFolder")
    programs = child(local, "Directory", Id="UserPrograms", Name="Programs")
    install = child(programs, "Directory", Id="INSTALLFOLDER", Name="OpenAI Usage")
    dirs = {".": install}
    files = sorted(p for p in payload.rglob("*") if p.is_file() and p.suffix.lower() != ".pdb")
    component_ids = []
    exe_file = None
    for file in files:
        relative = file.relative_to(payload).as_posix()
        parent = file.relative_to(payload).parent
        cumulative = Path()
        for part in parent.parts:
            ancestor = cumulative.as_posix()
            cumulative /= part
            key = cumulative.as_posix()
            if key not in dirs:
                dirs[key] = child(dirs[ancestor], "Directory", Id=identity("D", key), Name=part)
        comp_id = identity("C", relative)
        comp = child(dirs[parent.as_posix()], "Component", Id=comp_id,
                     Guid=str(uuid.uuid5(NAMESPACE, relative)), Bitness="always64")
        file_id = identity("F", relative)
        child(comp, "File", Id=file_id, Source=str(file), Name=file.name, KeyPath="no")
        child(comp, "RegistryValue", Root="HKCU", Key="Software\\UmbertoGiacobbi\\OpenAIUsage\\Components",
              Name=comp_id, Type="integer", Value="1", KeyPath="yes")
        component_ids.append(comp_id)
        if relative == "OpenAIUsage.Windows.exe":
            exe_file = file_id
    if not exe_file:
        raise ValueError("Current Windows executable is missing")
    for key, directory in dirs.items():
        comp_id = identity("R", key)
        comp = child(directory, "Component", Id=comp_id, Guid=str(uuid.uuid5(NAMESPACE, "directory:"+key)), Bitness="always64")
        child(comp, "RemoveFolder", Id=identity("Remove", key), On="uninstall")
        child(comp, "RegistryValue", Root="HKCU", Key="Software\\UmbertoGiacobbi\\OpenAIUsage\\Directories",
              Name=comp_id, Type="integer", Value="1", KeyPath="yes")
        component_ids.append(comp_id)
    menu = child(package, "StandardDirectory", Id="ProgramMenuFolder")
    comp = child(menu, "Component", Id="MenuShortcut", Guid=str(uuid.uuid5(NAMESPACE, "menu-shortcut")), Bitness="always64")
    shortcut = child(comp, "Shortcut", Id="StartMenuShortcut", Name="OpenAI Usage", Target="[#"+exe_file+"]",
          WorkingDirectory="INSTALLFOLDER", Icon="AppIcon", Advertise="no")
    child(shortcut,"ShortcutProperty",Key="System.AppUserModel.ID",Value="biz.umbertogiacobbi.OpenAIUsage")
    child(comp, "RegistryValue", Root="HKCU", Key="Software\\UmbertoGiacobbi\\OpenAIUsage",
          Name="StartMenuShortcut", Type="integer", Value="1", KeyPath="yes")
    component_ids.append("MenuShortcut")
    feature = child(package, "Feature", Id="Application", Title="OpenAI Usage", Level="1")
    for name in component_ids:
        child(feature, "ComponentRef", Id=name)
    ET.indent(root)
    ET.ElementTree(root).write(output, encoding="utf-8", xml_declaration=True)
    return len(files)


def tar_bytes(entries):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, data, mode in entries:
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.mtime = len(data), mode, EPOCH
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            archive.addfile(info, io.BytesIO(data))
    return stream.getvalue()


def deb_package(payload, output):
    files = {}
    for file in payload.rglob("*"):
        if not file.is_file() or file.suffix == ".pdb":
            continue
        relative = file.relative_to(payload).as_posix()
        mode = 0o755 if relative in ("backend/OpenAIUsage.Backend", "Linux/run.sh") else 0o644
        files["usr/lib/openai-usage/"+relative] = (file.read_bytes(), mode)
    launcher = b'#!/bin/sh\nexec /usr/bin/python3 /usr/lib/openai-usage/Linux/openai_usage.py "$@"\n'
    files["usr/bin/openai-usage"] = (launcher, 0o755)
    desktop = (ROOT / "Linux/openai-usage.desktop").read_text(encoding="utf-8").replace("Exec=openai-usage", "Exec=/usr/bin/openai-usage")
    files["usr/share/applications/biz.umbertogiacobbi.OpenAIUsage.desktop"] = (desktop.encode(), 0o644)
    files["usr/share/icons/hicolor/512x512/apps/biz.umbertogiacobbi.OpenAIUsage.png"] = ((ROOT/"Branding/app-icon-512.png").read_bytes(), 0o644)
    files["usr/share/doc/openai-usage/README.md"] = ((ROOT / "README.md").read_bytes(), 0o644)
    files["usr/share/doc/openai-usage/copyright"] = (b"Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\nUpstream-Name: OpenAI Usage\n\nFiles: *\nCopyright: 2026 Umberto Giacobbi\nLicense: Proprietary\n Private local build. Third-party components retain their own licenses;\n see /usr/lib/openai-usage/licenses.\n", 0o644)
    installed_size = sum(len(data) for data, _ in files.values())//1024+1
    control = f"""Package: openai-usage
Version: {VERSION}-1
Architecture: amd64
Maintainer: OpenAI Usage local builds <noreply@example.invalid>
Section: utils
Priority: optional
Installed-Size: {installed_size}
Depends: python3 (>= 3.10), python3-gi, python3-gi-cairo, python3-cairo, gir1.2-gtk-4.0 (>= 4.14), gir1.2-adw-1 (>= 1.5), libgtk-4-1 (>= 4.14), libadwaita-1-0 (>= 1.5), libc6 (>= 2.35), libgcc-s1, libstdc++6, libgssapi-krb5-2, libssl3t64 | libssl3, libicu78 | libicu77 | libicu76 | libicu74 | libicu72, zlib1g
Description: Native OpenAI allowance and token-history widget
 GTK desktop widget with split-flap reset countdown and real Codex account data.
 Includes the source-built .NET backend and its private runtime.
 Requires an independently installed Codex CLI with ChatGPT sign-in.
""".encode()
    md5sums = "".join(hashlib.md5(data).hexdigest()+"  "+name+"\n" for name, (data, _) in sorted(files.items())).encode()
    control_archive = gzip.compress(tar_bytes([("control", control, 0o644), ("md5sums", md5sums, 0o644)]), mtime=EPOCH)
    data_path = output.with_suffix(".data.tar.xz")
    with tarfile.open(data_path, "w:xz", format=tarfile.USTAR_FORMAT) as archive:
        directories = set()
        for name in files:
            directories.update(p.as_posix() for p in Path(name).parents if p.as_posix() != ".")
        for name in sorted(directories):
            info = tarfile.TarInfo(name)
            info.type, info.mode, info.mtime = tarfile.DIRTYPE, 0o755, EPOCH
            info.uname = info.gname = "root"
            archive.addfile(info)
        for name, (data, mode) in sorted(files.items()):
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.mtime = len(data), mode, EPOCH
            info.uname = info.gname = "root"
            archive.addfile(info, io.BytesIO(data))
    with output.open("wb") as out:
        out.write(b"!<arch>\n")
        for name, content in (("debian-binary", b"2.0\n"), ("control.tar.gz", control_archive), ("data.tar.xz", data_path.read_bytes())):
            header = f"{name+'/':<16}{EPOCH:<12}{0:<6}{0:<6}{'100644':<8}{len(content):<10}`\n".encode("ascii")
            assert len(header) == 60
            out.write(header); out.write(content)
            if len(content)%2:
                out.write(b"\n")
    data_path.unlink()
    return len(files)


def verify_deb(path):
    members = {}
    with path.open("rb") as source:
        assert source.read(8) == b"!<arch>\n"
        while header := source.read(60):
            assert len(header) == 60 and header[-2:] == b"`\n"
            name = header[:16].decode().strip().rstrip("/")
            size = int(header[48:58])
            members[name] = source.read(size)
            assert len(members[name]) == size
            if size%2:
                assert source.read(1) == b"\n"
    assert list(members) == ["debian-binary", "control.tar.gz", "data.tar.xz"]
    assert members["debian-binary"] == b"2.0\n"
    with tarfile.open(fileobj=io.BytesIO(members["control.tar.gz"]), mode="r:gz") as control_tar:
        control = control_tar.extractfile("control").read().decode()
        sums = control_tar.extractfile("md5sums").read().decode().splitlines()
    assert "Architecture: amd64" in control and "Package: openai-usage" in control
    with tarfile.open(fileobj=io.BytesIO(members["data.tar.xz"]), mode="r:xz") as data_tar:
        for line in sums:
            digest, name = line.split("  ", 1)
            assert not name.startswith("/") and ".." not in Path(name).parts
            item = data_tar.getmember(name)
            assert item.uid == item.gid == 0
            assert hashlib.md5(data_tar.extractfile(item).read()).hexdigest() == digest
        apphost = data_tar.getmember("usr/lib/openai-usage/backend/OpenAIUsage.Backend")
        assert apphost.mode == 0o755
        elf = data_tar.extractfile(apphost).read(20)
        assert elf[:6] == b"\x7fELF\x02\x01" and elf[18:20] == b"\x3e\x00"
        assert data_tar.getmember("usr/bin/openai-usage").mode == 0o755
    return {"archive": "valid ar/deb 2.0", "filesVerified": len(sums), "backend": "ELF64 x86-64", "runtimeExecution": "not performed"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["wix", "deb", "verify-deb"])
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    if args.command == "verify-deb":
        print(json.dumps(verify_deb(args.payload)))
    elif args.command == "wix":
        print(f"Windows installer source: {wix_source(args.payload.resolve(), args.output)} files")
    else:
        print(f"Ubuntu package: {deb_package(args.payload.resolve(), args.output)} files")
        print(json.dumps(verify_deb(args.output)))
