const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const template = fs.readFileSync(path.join(__dirname, "../installer/index.html"), "utf8");
const registeredBundle = 'customElements.define("ewt-install-dialog", class {});';

function page({serial = true, secure = true, bundle = registeredBundle,
               payload = '{"version":"test","parts":[{"data":"AQIDBA==","offset":65536}]}',
               skipInstaller = false} = {}) {
  const elements = {
    "#loading": {hidden: false, textContent: "正在加载安装器…"},
    "#installerControl": {hidden: true},
    "#firmware": {textContent: payload},
    "#startup-diagnostics": {hidden: true},
    "#startup-error": {textContent: ""},
  };
  const listeners = {}, timers = new Map(), definitions = new Map();
  const context = vm.createContext({
    document: {querySelector: selector => elements[selector]},
    navigator: {userAgent: "Installer regression browser", ...(serial ? {serial: {requestPort() {}}} : {})},
    location: {protocol: "file:"}, isSecureContext: secure,
    customElements: {define: (name, type) => definitions.set(name, type), get: name => definitions.get(name)},
    Uint8Array, Blob, URL, atob, console: {error() {}},
    setTimeout(callback) {const id = timers.size + 1; timers.set(id, callback); return id;},
    clearTimeout(id) {timers.delete(id);},
    addEventListener(name, callback) {listeners[name] = callback;},
  });
  context.window = context;
  const source = template.replace("__INSTALLER_BUNDLE__", bundle);
  const scripts = Array.from(source.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi))
    .filter(match => !match[1].includes("application/json"));
  for (const [index, script] of scripts.entries()) {
    if (skipInstaller && index === 2) continue;
    try {
      vm.runInContext(script[2], context);
    } catch (error) {
      if (listeners.error) listeners.error({error, message: error.message});
    }
  }
  return {elements, timers, listeners, context};
}

function failed(result) {
  assert.equal(result.elements["#installerControl"].hidden, true);
  assert.equal(result.elements["#loading"].hidden, false);
  assert.doesNotMatch(result.elements["#loading"].textContent, /正在加载/);
  assert.equal(result.elements["#startup-diagnostics"].hidden, false);
  assert.match(result.elements["#startup-error"].textContent, /Installer regression browser/);
}

async function main() {
  const unpackaged = page({bundle: "__INSTALLER_BUNDLE__", payload: "__FIRMWARE_PAYLOAD__"});
  failed(unpackaged);
  assert.match(unpackaged.elements["#loading"].textContent, /源码模板/);
  assert.doesNotMatch(unpackaged.elements["#startup-error"].textContent, /ReferenceError/);

  // A dependency exception used to bypass the only try/catch and leave loading forever.
  const dependency = page({bundle: 'throw new Error("codec component initialization failed");'});
  failed(dependency);
  assert.match(dependency.elements["#startup-error"].textContent, /codec component initialization failed/);

  // A syntax error in the bundled script must be reported by an earlier script.
  const syntax = page({bundle: "const = invalid;"});
  failed(syntax);
  assert.match(syntax.elements["#startup-error"].textContent, /SyntaxError|Unexpected token/);

  for (const options of [{serial: false}, {secure: false}]) {
    const unsupported = page({...options, bundle: 'throw new Error("must not execute");'});
    failed(unsupported);
    assert.doesNotMatch(unsupported.elements["#startup-error"].textContent, /must not execute/);
  }

  const corrupt = page({payload: "invalid JSON"});
  failed(corrupt);
  assert.match(corrupt.elements["#startup-error"].textContent, /JSON|Unexpected token/);

  const stalled = page({skipInstaller: true});
  for (const callback of stalled.timers.values()) callback();
  failed(stalled);
  assert.match(stalled.elements["#loading"].textContent, /超时/);

  const ready = page();
  assert.equal(ready.elements["#loading"].hidden, true);
  assert.equal(ready.elements["#installerControl"].hidden, false);
  assert.equal(ready.timers.size, 0);
  const manifest = await (await fetch(ready.context.installerManifest)).json();
  assert.equal(manifest.version, "test");
  assert.equal(manifest.builds[0].chipFamily, "ESP32-S3");
  assert.equal(manifest.builds[0].parts[0].offset, 65536);
  assert.deepEqual(Array.from(new Uint8Array(await (await fetch(manifest.builds[0].parts[0].path)).arrayBuffer())), [1, 2, 3, 4]);
  ready.listeners.error({message: "later installation error"});
  assert.equal(ready.elements["#loading"].hidden, true);
  console.log("Installer startup: 8 scenarios passed");
}

main().catch(error => {console.error(error); process.exitCode = 1;});
