#!/usr/bin/env python3
"""
Disassemble the `runtime-code` byte dumps that the CFB27 bridge DLL writes to
cfb27-bridge.log, so we can locate the exact certificate accept/reject branch.

Usage:
    python disasm-runtime-code.py <path-to-cfb27-bridge.log>

If no path is given, it auto-picks the newest runs\\cli_*\\cfb27-bridge.log under
%APPDATA%\\Cypress\\CFB27\\Private. Output is written next to the log as
disassembly.txt. Requires the `capstone` package (auto-installed if missing).
"""
import glob
import os
import re
import subprocess
import sys

try:
    import capstone
except ImportError:
    subprocess.run([sys.executable, "-m", "pip", "install", "capstone", "--quiet"], check=False)
    import capstone


def newest_log() -> str:
    root = os.path.join(os.environ.get("APPDATA", ""), "Cypress", "CFB27", "Private", "runs")
    candidates = glob.glob(os.path.join(root, "*", "cfb27-bridge.log"))
    if not candidates:
        sys.exit("No cfb27-bridge.log found under %APPDATA%\\Cypress\\CFB27\\Private\\runs")
    return max(candidates, key=os.path.getmtime)


def main() -> None:
    log_path = sys.argv[1] if len(sys.argv) > 1 else newest_log()
    text = open(log_path, encoding="utf-8", errors="ignore").read()

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.skipdata = True

    # Disassemble each dump at its RVA so branch/call targets print as RVAs that
    # match the probe's protossl-exec addresses directly.
    entries = {}
    for m in re.finditer(
        r"runtime-code (\S+) rva=0x([0-9A-Fa-f]+) length=0x[0-9A-Fa-f]+ "
        r"executable=\w+ read=true bytes=([0-9A-Fa-f]+)",
        text,
    ):
        entries[m.group(1)] = (int(m.group(2), 16), bytes.fromhex(m.group(3)))

    out_path = os.path.join(os.path.dirname(log_path), "disassembly.txt")
    with open(out_path, "w", encoding="utf-8") as out:
        out.write(f"source: {log_path}\nfunctions: {len(entries)}\n")
        for name, (rva, code) in entries.items():
            out.write(f"\n==== {name}  rva=0x{rva:X}  len=0x{len(code):X} ====\n")
            for insn in md.disasm(code, rva):
                mark = ""
                if insn.mnemonic.startswith(("j", "call")):
                    mark = "   <-- branch/call"
                out.write(f"0x{insn.address:08X}: {insn.mnemonic:<7} {insn.op_str}{mark}\n")

    print(f"wrote {out_path} ({len(entries)} functions)")


if __name__ == "__main__":
    main()
