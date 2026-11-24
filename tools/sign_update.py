"""Signs a built Coreplay release for the update feed.

Usage: python -I tools/sign_update.py <version> ["release notes" | @notes-file] [--file <path> --out <dir>]
Reads  dist/Coreplay-<version>-Setup.exe (Windows), or --file (a Mac .dmg, the Linux AppImage)
Writes dist/update-<version>/ (or --out): the file, latest.json (version, file, size, SHA-256,
notes) and latest.json.sig (ECDSA P-256 / SHA-256 over the exact bytes of latest.json,
raw r||s, base64). The app installs a release only when both check out.
"""
import base64
import hashlib
import json
import os
import shutil
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
KEY = os.path.join(os.path.expanduser("~"), ".coreplay", "coreplay-update-signing-key.pem")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    args = sys.argv[1:]
    opt = {}
    for flag in ("--file", "--out"):
        if flag in args:
            i = args.index(flag)
            opt[flag] = args[i + 1]
            del args[i:i + 2]
    version = args[0]
    notes = args[1] if len(args) > 1 else ""
    if notes.startswith("@"):
        notes = open(notes[1:], encoding="utf-8").read().strip()
    setup = opt.get("--file") or os.path.join(ROOT, "dist", f"Coreplay-{version}-Setup.exe")
    name = os.path.basename(setup)
    if not os.path.isfile(setup):
        sys.exit("missing " + setup)
    if version not in name:
        sys.exit(f"{name} is not version {version}")
    key = serialization.load_pem_private_key(open(KEY, "rb").read(), password=None)
    out = opt.get("--out") or os.path.join(ROOT, "dist", f"update-{version}")
    os.makedirs(out, exist_ok=True)
    shutil.copy2(setup, os.path.join(out, name))
    data = open(setup, "rb").read()
    manifest = json.dumps({"version": version, "file": name, "size": len(data),
                           "sha256": hashlib.sha256(data).hexdigest(), "notes": notes},
                          indent=2, ensure_ascii=False).encode("utf-8")
    der = key.sign(manifest, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    raw = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    open(os.path.join(out, "latest.json"), "wb").write(manifest)
    open(os.path.join(out, "latest.json.sig"), "wb").write(base64.b64encode(raw) + b"\n")
    print("signed", os.path.normpath(out))


if __name__ == "__main__":
    main()
