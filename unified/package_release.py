"""Package a committed OWL-Tables source tree and build the Windows ZIP from it."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile


ROOT = Path(__file__).resolve().parents[1]
GUIDES = ("docs/EDITOR.md", "docs/MACRO_IMPORT.md", "docs/BUILD_UNIFIED.md")
SOURCE_PATHS = (
    ".gitignore", "README.md", "LICENSE", *GUIDES,
    "assets/Template.xlsx", "assets/Table2FBD.ico",
    "cpp/*.cpp", "cpp/*.hpp", "cpp/vendor/*LICENSE*",
    "boolean/*.cpp", "boolean/*.hpp", "boolean/build.ps1", "boolean/app.rc",
    "boolean/app.manifest", "boolean/engine/policy.json", "selector/*.cpp", "selector/*.hpp",
    "unified", "examples", "docs/solver/examples/lookup-and-reuse.json",
    "docs/solver/examples/word-mask.json", "docs/solver/examples/row-masks.json",
)
BINARY_FILES = (
    "OWL-Tables.exe", "README.md", "LICENSE", "THIRD_PARTY_NOTICES.md",
    *GUIDES, "Template.xlsx", "licenses/json-LICENSE.txt", "licenses/pugixml-LICENSE.txt",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bds", help="Path to the C++Builder installation")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    version_header = subprocess.check_output(
        ["git", "show", f"{commit}:boolean/version.hpp"], cwd=ROOT, text=True
    )
    version = re.search(r'^#define BOOLEAN_VERSION "(\d+\.\d+\.\d+)"$', version_header, re.M)[1]
    product = f"OWL-Tables-{version}"
    dest = ROOT / "release" / product
    dest.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest.parent) as temp_name:
        temp = Path(temp_name)
        source = temp / f"{product}-source.zip"
        subprocess.run(
            ["git", "archive", "--format=zip", f"--prefix={source.stem}/",
             "-o", source, commit, "--", *SOURCE_PATHS], cwd=ROOT, check=True,
        )
        with ZipFile(source) as archive:
            archive.extractall(temp)
        tree = temp / source.stem
        command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                   "-File", tree / "boolean/build.ps1", "-Unified"]
        if args.bds:
            command.extend(["-Bds", args.bds])
        subprocess.run(command, cwd=tree, check=True)
        dist = tree / "unified/dist/OWL-Tables"
        binary = temp / f"{product}-win32.zip"
        with ZipFile(binary, "w", ZIP_DEFLATED) as archive:
            for name in BINARY_FILES:
                archive.write(dist / name, f"{binary.stem}/{name}")
            for path in sorted((tree / "examples").rglob("*")):
                if path.is_file():
                    name = path.relative_to(tree).as_posix()
                    archive.write(dist / name, f"{binary.stem}/{name}")
        with ZipFile(binary) as archive:
            damaged = archive.testzip()
            if damaged:
                raise ValueError(f"Damaged ZIP entry: {damaged}")
        sums = temp / "SHA256SUMS.txt"
        sums.write_bytes("".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
            for path in (source, binary)
        ).encode("ascii"))
        dest.mkdir(exist_ok=True)
        for artifact in (source, binary, sums):
            # A file moved out of a private temp directory can keep its private Windows ACL.
            staged = dest / f".{temp.name}-{artifact.name}"
            try:
                shutil.copyfile(artifact, staged)
                staged.replace(dest / artifact.name)
            finally:
                staged.unlink(missing_ok=True)
    print(f"{commit}\n{dest}")


if __name__ == "__main__":
    main()
