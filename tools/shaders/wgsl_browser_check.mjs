// Checks the WGSL that hlsl_to_wgsl.py produced against Chrome's own WebGPU
// compiler - the one that actually matters, since a converter accepting a
// shader says nothing about whether a browser will.
//
// Serves the spike output over localhost (WebGPU needs a secure context),
// drives headless Chrome over the DevTools protocol, and for every WGSL file
// runs createShaderModule + getCompilationInfo. Compute shaders additionally
// get a real pipeline built with layout "auto", which also validates the
// bindings against the device's limits.
//
//   node tools/shaders/wgsl_browser_check.mjs [spike_dir]
//
// Needs node 22+ (built-in WebSocket) - emsdk's node 24 works.
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { join, dirname, resolve, relative } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
// Serves the repo, because the report lives with the intermediates
// (build_wasm/shader_spike) while the .wgsl it points at lives in the engine's
// assets. Paths in the report are absolute; they are served repo-relative.
const repoRoot = resolve(join(here, "..", ".."));
const spikeDir = resolve(process.argv[2] ?? join(repoRoot, "build_wasm", "shader_spike"));
const report = JSON.parse(readFileSync(join(spikeDir, "report.json"), "utf8"));
const chromePath = "C:/Program Files/Google/Chrome/Application/chrome.exe";

// Static server: a blank page at / plus the spike directory's files.
const server = createServer((req, res) => {
	const path = decodeURIComponent(new URL(req.url, "http://x").pathname);
	if (path === "/") {
		res.writeHead(200, { "content-type": "text/html" });
		return res.end("<!doctype html><title>wgsl check</title>");
	}
	const file = join(repoRoot, path);
	if (!file.startsWith(repoRoot) || !existsSync(file)) {
		res.writeHead(404);
		return res.end();
	}
	res.writeHead(200, { "content-type": "text/plain" });
	res.end(readFileSync(file));
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const httpPort = server.address().port;

const cdpPort = 9334;
const chrome = spawn(chromePath, [
	"--headless=new", "--no-sandbox", "--enable-unsafe-webgpu",
	`--remote-debugging-port=${cdpPort}`, `--user-data-dir=${process.env.TEMP}\\wgsl_check_profile`, "about:blank",
], { stdio: "ignore" });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let target;
for (let i = 0; i < 50 && !target; i++) {
	await sleep(200);
	try {
		target = (await (await fetch(`http://127.0.0.1:${cdpPort}/json/list`)).json()).find((t) => t.type === "page");
	} catch {}
}
if (!target) {
	console.error("could not reach headless chrome");
	process.exit(2);
}

const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener("open", r, { once: true }));
let nextId = 1;
const pending = new Map();
ws.addEventListener("message", (ev) => {
	const msg = JSON.parse(ev.data);
	if (msg.id && pending.has(msg.id)) {
		pending.get(msg.id)(msg);
		pending.delete(msg.id);
	}
});
const send = (method, params = {}) => new Promise((resolve) => {
	const id = nextId++;
	pending.set(id, resolve);
	ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression) => {
	const r = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
	if (r.result.exceptionDetails) {
		throw new Error(r.result.exceptionDetails.exception?.description ?? r.result.exceptionDetails.text);
	}
	return r.result.result.value;
};

await send("Page.navigate", { url: `http://127.0.0.1:${httpPort}/` });
await sleep(1000);

const adapterInfo = await evaluate(`(async () => {
	if (!navigator.gpu) return { error: "navigator.gpu missing" };
	const adapter = await navigator.gpu.requestAdapter();
	if (!adapter) return { error: "no WebGPU adapter" };
	window.blkDevice = await adapter.requestDevice();
	const i = adapter.info ?? {};
	return { vendor: i.vendor, architecture: i.architecture, description: i.description };
})()`);
if (adapterInfo.error) {
	console.error(`WebGPU unavailable in headless Chrome: ${adapterInfo.error}`);
	process.exit(2);
}
console.log(`WebGPU adapter: ${adapterInfo.vendor} ${adapterInfo.architecture} ${adapterInfo.description ?? ""}`.trim());

const rows = report.filter((r) => r.wgsl);
for (const row of rows) {
	const rel = "/" + relative(repoRoot, row.wgsl).replaceAll("\\", "/");
	const result = await evaluate(`(async () => {
		const device = window.blkDevice;
		const code = await (await fetch(${JSON.stringify(rel)})).text();
		device.pushErrorScope("validation");
		const module = device.createShaderModule({ code });
		const info = await module.getCompilationInfo();
		const messages = info.messages.filter((m) => m.type === "error").map((m) => m.lineNum + ":" + m.linePos + " " + m.message);
		let pipeline = "n/a";
		if (${JSON.stringify(row.profile)}.startsWith("cs_") && messages.length === 0) {
			try {
				const entry = ${JSON.stringify(row.entry)};
				device.createComputePipeline({ layout: "auto", compute: { module, entryPoint: entry } });
				pipeline = "ok";
			} catch (e) { pipeline = String(e); }
		}
		const scopeError = await device.popErrorScope();
		return { errors: messages, pipeline, scopeError: scopeError ? scopeError.message : null };
	})()`);
	row.browser = result.errors.length === 0 && !result.scopeError ? "ok" : (result.errors[0] ?? result.scopeError);
	row.browser_pipeline = result.pipeline;
}

// ---- Render pipelines --------------------------------------------------------
//
// Module compilation doesn't check bind group numbers against the device's
// limits, or that a separately converted vertex and pixel stage agree on what
// passes between them. Building the real pipeline checks both, so each vertex
// shader is paired with its file's pixel shader(s). Vertex buffers and render
// targets are derived from the WGSL itself.

const VERTEX_FORMATS = {
	"f32": ["float32", 4], "vec2<f32>": ["float32x2", 8], "vec3<f32>": ["float32x3", 12], "vec4<f32>": ["float32x4", 16],
	"u32": ["uint32", 4], "vec2<u32>": ["uint32x2", 8], "vec3<u32>": ["uint32x3", 12], "vec4<u32>": ["uint32x4", 16],
	"i32": ["sint32", 4], "vec2<i32>": ["sint32x2", 8], "vec3<i32>": ["sint32x3", 12], "vec4<i32>": ["sint32x4", 16],
};
// WebGPU requires a fragment output to supply at least as many components as
// its target format has - unlike D3D12, which leaves unwritten channels
// undefined - so the target follows the output's component count.
function targetFormat(type) {
	const kind = type.includes("u32") ? "uint" : type.includes("i32") ? "sint" : "float";
	const count = type.startsWith("vec") ? Number(type[3]) : 1;
	const channels = { 1: "r32", 2: "rg32", 3: null, 4: kind === "float" ? "rgba16" : "rgba32" }[count];
	return channels ? channels + kind : `no 3-channel format for ${type}`;
}

function entrySignature(wgsl, stage, name) {
	// Lazy up to the ")" before "->" or "{" - parameters contain "@location(0)".
	const m = wgsl.match(new RegExp(`@${stage}\\s+fn\\s+${name}\\s*\\(([\\s\\S]*?)\\)\\s*(?:->\\s*([^{]+))?\\{`));
	return m ? { params: m[1], returns: (m[2] ?? "").trim() } : null;
}

function locations(text) {
	return [...text.matchAll(/@location\((\d+)\)\s*(?:@interpolate\([^)]*\)\s*)?\w+\s*:\s*([\w<>]+)/g)].map((l) => ({ loc: Number(l[1]), type: l[2] }));
}

function outputLocations(wgsl, returns) {
	if (!returns) return [];
	const direct = returns.match(/@location\((\d+)\)\s*([\w<>]+)/);
	if (direct) return [{ loc: Number(direct[1]), type: direct[2] }];
	const struct = wgsl.match(new RegExp(`struct\\s+${returns}\\s*\\{([^}]*)\\}`));
	return struct ? locations(struct[1]) : [];
}

const pipelineResults = [];
for (const variant of [...new Set(report.map((r) => r.variant))]) {
	for (const vs of report.filter((r) => r.variant === variant && r.entry === "vertex_shader" && r.browser === "ok")) {
		for (const ps of report.filter((r) => r.variant === variant && r.shader === vs.shader && r.profile.startsWith("ps_"))) {
			const label = `${vs.shader}: vertex_shader + ${ps.entry}`;
			if (ps.browser !== "ok") {
				pipelineResults.push({ variant, label, result: "skipped (pixel stage did not compile)" });
				continue;
			}
			const vsWgsl = readFileSync(vs.wgsl, "utf8");
			const psWgsl = readFileSync(ps.wgsl, "utf8");
			const vsSig = entrySignature(vsWgsl, "vertex", vs.entry);
			const psSig = entrySignature(psWgsl, "fragment", ps.entry);
			if (!vsSig || !psSig) {
				pipelineResults.push({ variant, label, result: "could not find entry point signature" });
				continue;
			}

			const buffers = locations(vsSig.params).map(({ loc, type }) => {
				const [format, size] = VERTEX_FORMATS[type] ?? ["float32x4", 16];
				return { arrayStride: size, attributes: [{ shaderLocation: loc, offset: 0, format }] };
			});
			const outputs = outputLocations(psWgsl, psSig.returns);
			const targets = [];
			for (const { loc, type } of outputs) targets[loc] = { format: targetFormat(type) };
			for (let i = 0; i < targets.length; i++) targets[i] ??= null;

			const desc = {
				layout: "auto",
				vertex: { entryPoint: vs.entry, buffers },
				fragment: { entryPoint: ps.entry, targets },
				depthStencil: { format: "depth32float", depthWriteEnabled: true, depthCompare: "less" },
				primitive: { topology: "triangle-list" },
			};
			const vsRel = "/" + relative(repoRoot, vs.wgsl).replaceAll("\\", "/");
			const psRel = "/" + relative(repoRoot, ps.wgsl).replaceAll("\\", "/");
			const result = await evaluate(`(async () => {
				const device = window.blkDevice;
				const desc = ${JSON.stringify(desc)};
				desc.vertex.module = device.createShaderModule({ code: await (await fetch(${JSON.stringify(vsRel)})).text() });
				desc.fragment.module = device.createShaderModule({ code: await (await fetch(${JSON.stringify(psRel)})).text() });
				try {
					await device.createRenderPipelineAsync(desc);
					return "ok";
				} catch (e) {
					return e.message.split("\\n")[0];
				}
			})()`);
			pipelineResults.push({ variant, label, result, groups: [...new Set([...vsWgsl.matchAll(/@group\((\d+)\)/g), ...psWgsl.matchAll(/@group\((\d+)\)/g)].map((g) => Number(g[1])))].sort() });
		}
	}
}

writeFileSync(join(spikeDir, "report.json"), JSON.stringify(report, null, 1));
writeFileSync(join(spikeDir, "pipelines.json"), JSON.stringify(pipelineResults, null, 1));

for (const variant of [...new Set(report.map((r) => r.variant))]) {
	const vr = report.filter((r) => r.variant === variant);
	console.log(`\n[${variant}]`);
	for (const r of vr) {
		const b = r.browser ?? "-";
		const p = r.browser_pipeline && r.browser_pipeline !== "n/a" ? `  (compute pipeline: ${r.browser_pipeline})` : "";
		console.log(`${(r.shader + "." + r.entry).padEnd(44)} ${b === "ok" ? "ok" : b === "-" ? "-" : "FAIL: " + b}${p}`);
	}
	const ran = vr.filter((r) => r.browser).length;
	console.log(`  Chrome accepts ${vr.filter((r) => r.browser === "ok").length}/${ran} of the WGSL it was given (${vr.length} entry points)`);

	const pr = pipelineResults.filter((p) => p.variant === variant);
	if (pr.length) {
		console.log(`  render pipelines:`);
		for (const p of pr) console.log(`    ${p.label.padEnd(52)} ${p.result === "ok" ? "ok" : "FAIL: " + p.result}${p.groups ? "  groups " + JSON.stringify(p.groups) : ""}`);
		console.log(`  Chrome builds ${pr.filter((p) => p.result === "ok").length}/${pr.length} render pipelines`);
	}
}

ws.close();
chrome.kill();
server.close();
process.exit(0);
