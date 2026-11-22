"""Signs a built Coreplay release for the update feed.

Usage: python -I tools/sign_update.py <version> ["release notes" | @notes-file]
Reads  dist/Coreplay-<version>-Setup.exe
Writes dist/update-<version>/: the setup file, latest.json (version, file, size, SHA-256,
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
    version = sys.argv[1]
    notes = sys.argv[2] if len(sys.argv) > 2 else ""
    if notes.startswith("@"):
        notes = open(notes[1:], encoding="utf-8").read().strip()
    name = f"Coreplay-{version}-Setup.exe"
    setup = os.path.join(ROOT, "dist", name)
    if not os.path.isfile(setup):
        sys.exit("missing " + setup)
    key = serialization.load_pem_private_key(open(KEY, "rb").read(), password=None)
    out = os.path.join(ROOT, "dist", f"update-{version}")
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
