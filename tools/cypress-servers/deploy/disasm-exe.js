// Disassemble arbitrary RVA ranges directly from CollegeFB27.exe using the MSVC
// toolchain (ml64 + dumpbin). Lets us explore any ProtoSSL function without
// rebuilding/re-running the DLL. No external packages.
//
//   node disasm-exe.js <rva>:<len> [<rva>:<len> ...]
//   node disasm-exe.js --exe "C:\path\CollegeFB27.exe" 16E1C00:600 16E4400:400
//
// RVAs and lengths are hex. Output: disassembly-exe.txt next to this script,
// with addresses shown as real RVAs.

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");

const DEFAULT_EXE = "C:\\Program Files\\EA Games\\EA SPORTS College Football 27\\CollegeFB27.exe";

function findMsvcBin() {
  const vswhere = path.join(process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)",
    "Microsoft Visual Studio", "Installer", "vswhere.exe");
  let installs = [];
  try {
    installs = execFileSync(vswhere, ["-products", "*", "-property", "installationPath"], { encoding: "utf8" })
      .split(/\r?\n/).filter(Boolean);
  } catch {}
  for (const g of ["C:\\Program Files\\Microsoft Visual Studio\\2022",
                   "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022"])
    for (const ed of ["Community", "Professional", "Enterprise", "BuildTools"]) {
      const p = path.join(g, ed);
      if (fs.existsSync(p)) installs.push(p);
    }
  for (const inst of installs) {
    const root = path.join(inst, "VC", "Tools", "MSVC");
    if (!fs.existsSync(root)) continue;
    for (const v of fs.readdirSync(root).sort().reverse()) {
      const bin = path.join(root, v, "bin", "Hostx64", "x64");
      if (fs.existsSync(path.join(bin, "ml64.exe"))) return bin;
    }
  }
  return null;
}

const MSVC_BIN = findMsvcBin();
if (!MSVC_BIN) { console.error("ml64.exe not found (install VS C++ workload)."); process.exit(1); }
const ML64 = path.join(MSVC_BIN, "ml64.exe");
const DUMPBIN = path.join(MSVC_BIN, "dumpbin.exe");
const ENV = { ...process.env, PATH: MSVC_BIN + path.delimiter + (process.env.PATH || "") };

function parseSections(buf) {
  const peOff = buf.readUInt32LE(0x3C);
  const numSec = buf.readUInt16LE(peOff + 6);
  const optSize = buf.readUInt16LE(peOff + 20);
  let s = peOff + 24 + optSize;
  const secs = [];
  for (let i = 0; i < numSec; i++, s += 40) {
    secs.push({
      va: buf.readUInt32LE(s + 12),
      vsize: buf.readUInt32LE(s + 8),
      raw: buf.readUInt32LE(s + 20),
      rsize: buf.readUInt32LE(s + 16),
    });
  }
  return secs;
}

function rvaToOffset(secs, rva) {
  for (const s of secs) {
    const size = Math.max(s.vsize, s.rsize);
    if (rva >= s.va && rva < s.va + size) return s.raw + (rva - s.va);
  }
  return -1;
}

function masmBytes(buf) {
  const lines = [];
  for (let i = 0; i < buf.length; i += 16) {
    const chunk = [];
    for (let j = i; j < Math.min(i + 16, buf.length); j++)
      chunk.push("0" + buf[j].toString(16).padStart(2, "0") + "h");
    lines.push("  BYTE " + chunk.join(","));
  }
  return lines.join("\n");
}

function disasm(rva, buf, tmp) {
  const asm = path.join(tmp, "b.asm"), obj = path.join(tmp, "b.obj");
  fs.writeFileSync(asm, ".code\n" + masmBytes(buf) + "\nEND\n");
  execFileSync(ML64, ["/nologo", "/c", "/Fo", obj, asm], { stdio: ["ignore", "ignore", "inherit"], env: ENV });
  const raw = execFileSync(DUMPBIN, ["/nologo", "/disasm:nobytes", obj], { encoding: "utf8", env: ENV });
  const out = [`\n==== rva=0x${rva.toString(16).toUpperCase()} len=0x${buf.length.toString(16)} ====`];
  for (const line of raw.split(/\r?\n/)) {
    const m = line.match(/^\s*([0-9A-Fa-f]{8,16}):\s+(.*)$/);
    if (!m) continue;
    const abs = (rva + parseInt(m[1], 16)) >>> 0;
    const insn = m[2].trim();
    const mark = /^(j\w+|call|ret)/i.test(insn) ? "   <-- branch/call/ret" : "";
    out.push(`0x${abs.toString(16).toUpperCase().padStart(8, "0")}: ${insn}${mark}`);
  }
  return out.join("\n");
}

function main() {
  const args = process.argv.slice(2);
  let exe = DEFAULT_EXE;
  const ranges = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--exe") { exe = args[++i]; continue; }
    const [r, l] = args[i].split(":");
    ranges.push([parseInt(r, 16), parseInt(l || "200", 16)]);
  }
  if (!ranges.length) { console.error("Usage: node disasm-exe.js <rva>:<len> ..."); process.exit(1); }

  const buf = fs.readFileSync(exe);
  const secs = parseSections(buf);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "cfbexe-"));
  const out = [`exe: ${exe}`];
  for (const [rva, len] of ranges) {
    const off = rvaToOffset(secs, rva);
    if (off < 0) { out.push(`\n==== rva=0x${rva.toString(16)} (not in any section) ====`); continue; }
    out.push(disasm(rva, buf.subarray(off, off + len), tmp));
  }
  const outPath = path.join(__dirname, "disassembly-exe.txt");
  fs.writeFileSync(outPath, out.join("\n") + "\n");
  console.log(`wrote ${outPath}`);
}

main();
