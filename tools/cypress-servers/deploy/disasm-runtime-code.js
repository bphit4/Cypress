// Disassemble the `runtime-code` byte dumps in cfb27-bridge.log using the MSVC
// toolchain (ml64 + dumpbin), which is already installed. No external packages.
//
// RUN FROM the "x64 Native Tools Command Prompt for VS" (so ml64/dumpbin are on
// PATH):
//     node disasm-runtime-code.js [path-to-cfb27-bridge.log]
//
// If no log path is given it auto-picks the newest runs\cli_*\cfb27-bridge.log
// under %APPDATA%\Cypress\CFB27\Private. Output: disassembly.txt next to this
// script. Addresses are shown as real RVAs so they match the probe's logged
// protossl-exec / stack addresses.

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");

// Locate ml64.exe + dumpbin.exe from the VS install so this works from any
// shell (not only the "x64 Native Tools" prompt).
function findMsvcBin() {
  const vswhere = path.join(
    process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)",
    "Microsoft Visual Studio", "Installer", "vswhere.exe");
  let installs = [];
  try {
    const out = execFileSync(vswhere, ["-products", "*", "-property", "installationPath"], { encoding: "utf8" });
    installs = out.split(/\r?\n/).filter(Boolean);
  } catch { /* fall through to guesses */ }
  for (const g of ["C:\\Program Files\\Microsoft Visual Studio\\2022",
                   "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022"]) {
    for (const ed of ["Community", "Professional", "Enterprise", "BuildTools"]) {
      const p = path.join(g, ed);
      if (fs.existsSync(p)) installs.push(p);
    }
  }
  for (const inst of installs) {
    const msvcRoot = path.join(inst, "VC", "Tools", "MSVC");
    if (!fs.existsSync(msvcRoot)) continue;
    const vers = fs.readdirSync(msvcRoot).sort().reverse();
    for (const v of vers) {
      const bin = path.join(msvcRoot, v, "bin", "Hostx64", "x64");
      if (fs.existsSync(path.join(bin, "ml64.exe"))) return bin;
    }
  }
  return null;
}

const MSVC_BIN = findMsvcBin();
if (!MSVC_BIN) {
  console.error("Could not find ml64.exe. Install the VS C++ workload, or run from the x64 Native Tools prompt.");
  process.exit(1);
}
const ML64 = path.join(MSVC_BIN, "ml64.exe");
const DUMPBIN = path.join(MSVC_BIN, "dumpbin.exe");
const CHILD_ENV = { ...process.env, PATH: MSVC_BIN + path.delimiter + (process.env.PATH || "") };

function newestLog() {
  const root = path.join(process.env.APPDATA || "", "Cypress", "CFB27", "Private", "runs");
  let best = null;
  let bestMtime = 0;
  for (const dir of fs.existsSync(root) ? fs.readdirSync(root) : []) {
    const p = path.join(root, dir, "cfb27-bridge.log");
    if (fs.existsSync(p)) {
      const m = fs.statSync(p).mtimeMs;
      if (m > bestMtime) { bestMtime = m; best = p; }
    }
  }
  if (!best) { console.error("No cfb27-bridge.log found."); process.exit(1); }
  return best;
}

function masmBytes(buf) {
  const lines = [];
  for (let i = 0; i < buf.length; i += 16) {
    const chunk = [];
    for (let j = i; j < Math.min(i + 16, buf.length); j++) {
      chunk.push("0" + buf[j].toString(16).padStart(2, "0") + "h");
    }
    lines.push("  BYTE " + chunk.join(","));
  }
  return lines.join("\n");
}

function disasmOne(name, rva, buf, tmp) {
  const asm = path.join(tmp, "blob.asm");
  const obj = path.join(tmp, "blob.obj");
  fs.writeFileSync(asm, ".code\n" + masmBytes(buf) + "\nEND\n");
  execFileSync(ML64, ["/nologo", "/c", "/Fo", obj, asm], { stdio: ["ignore", "ignore", "inherit"], env: CHILD_ENV });
  const raw = execFileSync(DUMPBIN, ["/nologo", "/disasm:nobytes", obj], { encoding: "utf8", env: CHILD_ENV });

  const out = [`\n==== ${name}  rva=0x${rva.toString(16).toUpperCase()}  len=0x${buf.length.toString(16)} ====`];
  for (const line of raw.split(/\r?\n/)) {
    // dumpbin lines look like:  0000000000000005: mov rsi, rcx
    const m = line.match(/^\s*([0-9A-Fa-f]{8,16}):\s+(.*)$/);
    if (!m) continue;
    const off = parseInt(m[1], 16);
    const abs = (rva + off) >>> 0;
    const insn = m[2].trim();
    const mark = /^(j\w+|call|ret)/i.test(insn) ? "   <-- branch/call/ret" : "";
    out.push(`0x${abs.toString(16).toUpperCase().padStart(8, "0")}: ${insn}${mark}`);
  }
  return out.join("\n");
}

function main() {
  const logPath = process.argv[2] || newestLog();
  const text = fs.readFileSync(logPath, "utf8");
  const re = /runtime-code (\S+) rva=0x([0-9A-Fa-f]+) length=0x[0-9A-Fa-f]+ executable=\w+ read=true bytes=([0-9A-Fa-f]+)/g;

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "cfbdis-"));
  const results = [`source: ${logPath}`];
  const seen = new Set();
  let m;
  while ((m = re.exec(text)) !== null) {
    if (seen.has(m[1])) continue;
    seen.add(m[1]);
    try {
      results.push(disasmOne(m[1], parseInt(m[2], 16), Buffer.from(m[3], "hex"), tmp));
    } catch (e) {
      results.push(`\n==== ${m[1]} (FAILED: ${e.message}) ====`);
    }
  }

  const outPath = path.join(__dirname, "disassembly.txt");
  fs.writeFileSync(outPath, results.join("\n") + "\n");
  console.log(`wrote ${outPath} (${seen.size} functions)`);
}

main();
