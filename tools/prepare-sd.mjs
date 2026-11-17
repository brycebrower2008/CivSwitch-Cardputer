// Build loader-ready, <=500 KB JS payloads from a checked-out upstream source.
// Run: (cd upstream && npm ci) && node tools/prepare-sd.mjs
import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import {fileURLToPath, pathToFileURL} from "node:url";
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const upstream = path.join(root, "upstream");
const output = path.join(root, "sd-package", "civswitch");
const {buildPayload} = await import(pathToFileURL(path.join(upstream, "tools", "bundle.mjs")).href);
fs.mkdirSync(output, {recursive: true});
const files = [
  {name: "fs.js", entry: "src/core.js", notes: "FS promotion; requires CivSwitch remote JS loader"},
  {name: "chain.js", entry: "src/chain.mjs", notes: "FS promotion + kaiHEN; requires kaiHEN files already on Switch SD"},
  {name: "chain-sd.js", entry: "src/chain-sd.mjs", notes: "FS promotion + kaiHEN JS loaded from Switch SD /kaihen/payload.js"},
  {name: "kaihen.js", entry: "src/kaihen.js", notes: "kaiHEN boot; requires successful FS promotion in the same console boot"}
];
const sourceCommit = (await import("node:child_process")).execFileSync("git", ["-C", upstream, "rev-parse", "HEAD"], {encoding:"utf8"}).trim();
const manifest = {
  source: "https://github.com/s8n/CivSwitchFS-kaiHEN",
  revision: sourceCommit,
  maxLoaderBytes: 500000,
  generatedAtUTC: new Date().toISOString(),
  warning: "Research exploit, risk of save corruption. Running requires CivSwitch loader, compatible firmware and game.",
  files: []
};
for (const f of files) {
  console.log("Building", f.name, "from", f.entry);
  const result = await buildPayload({entry: path.join(upstream, f.entry)});
  if (result.bytes < 1 || result.bytes >= 500000)
    throw new Error(f.name + " exceeds the remote loader byte limit: " + result.bytes);
  const dest = path.join(output, f.name);
  fs.writeFileSync(dest, result.payload, "utf8");
  const hash = crypto.createHash("sha256").update(result.payload, "utf8").digest("hex");
  manifest.files.push({...f, bytes: result.bytes, sha256: hash});
  console.log(f.name, result.bytes, "bytes", hash);
}
fs.writeFileSync(path.join(output, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
fs.writeFileSync(path.join(root, "sd-package", "README.txt"), [
  "CivSwitch Cardputer-Adv SD payloads",
  "Copy the civswitch directory to the ROOT of the Cardputer microSD.",
  "The Switch 2 uses a DIFFERENT microSD card. It must have its own kaiHEN files.",
  "Device: press P from Home, choose a JS script and confirm explicitly with Y.",
  "Actual payloads are unverified on physical hardware; firmware 23.0.1 and Civ7 1.0.0 are the reported upstream test case.",
  "A real CivSwitch Remote JS Loader must already be waiting at port 9026 on the Switch.",
  "Do not run this on unsupported versions. Back up saves. Use at your own risk.",
  "These files originate from "+manifest.source+" revision "+sourceCommit+".",
  "License/source: see upstream-source.zip and upstream LICENSE / THIRD_PARTY_NOTICES.",
  ""
].join("\n"));
