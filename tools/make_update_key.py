"""Creates Coreplay's update signing key pair (once).

The private key goes OUTSIDE the source tree: %USERPROFILE%\\.coreplay\\coreplay-update-signing-key.pem
Keep a backup: without it no installed Coreplay will accept a new
release. The public key is written to src/UpdateKey.h, which the app is built with.

ECDSA P-256 + SHA-256: Windows verifies it with its own CNG (bcrypt.dll), no extra library.
Run: python -I tools/make_update_key.py      (refuses to overwrite an existing key)
"""
import os
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

KEY = os.path.join(os.path.expanduser("~"), ".coreplay", "coreplay-update-signing-key.pem")
HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "UpdateKey.h")


def write_header(pub):
    nums = pub.public_numbers()
    x, y = nums.x.to_bytes(32, "big"), nums.y.to_bytes(32, "big")
    fmt = lambda b: ", ".join(f"0x{v:02x}" for v in b)  # noqa: E731
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n// Coreplay update signing PUBLIC key (ECDSA P-256), made by tools/make_update_key.py.\n"
                "// The private key is outside the source tree. A release is installed only when its\n"
                "// latest.json verifies against this key.\n")
        f.write(f"static const unsigned char kUpdateKeyX[32] = {{{fmt(x)}}};\n")
        f.write(f"static const unsigned char kUpdateKeyY[32] = {{{fmt(y)}}};\n")
    print("wrote", os.path.normpath(HEADER))


def main():
    if os.path.exists(KEY):
        key = serialization.load_pem_private_key(open(KEY, "rb").read(), password=None)
        print("key exists, not replaced:", KEY)
    else:
        os.makedirs(os.path.dirname(KEY), exist_ok=True)
        key = ec.generate_private_key(ec.SECP256R1())
        with open(KEY, "wb") as f:
            f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                      serialization.NoEncryption()))
        print("created", KEY, "- back it up: it is the only copy")
    write_header(key.public_key())


if __name__ == "__main__":
    sys.exit(main())
