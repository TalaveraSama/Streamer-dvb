/* 203.73 regression: DVB private preview bypasses the generic HTTP remux;
 * configured/public HTTP and production DVB output behavior remain unchanged. */
'use strict';
const fs = require('fs');
const assert = require('assert');
const { readFile, readProgramVersion } = require('./lib/version');
const cpp = readFile('src/StreamManager.cpp');
const start = cpp.indexOf('bool StreamManager::buildOutputBranch(');
const end = cpp.indexOf('bool StreamManager::buildPassthroughPipeline(', start);
assert(start >= 0 && end > start);
const branch = cpp.slice(start, end);
assert(branch.includes('const bool privateDvbPreview = type == "http"'));
assert(branch.includes('outputConfig.outputHost == "127.0.0.1" && outputConfig.outputPort == 0'));
assert(branch.includes('state->runtimeConfig.inputServiceId > 0'));
assert(branch.includes('state->sharedDvbServiceRelayUri'));
assert(branch.includes('gst_element_link_many(sourceTail, queue, sink, nullptr)'));
assert(branch.indexOf('if (privateDvbPreview)') < branch.indexOf('const bool strictTsNetworkOutput'));
assert(cpp.includes('HTTP PREVIEW DELIVERY 203.73: stream='));
assert(cpp.includes('event=no-upstream-media-after-10s bytes=0'));
// 203.76: no hard-coded version anywhere in the shipping sources.
readProgramVersion();
const httpSource = readFile('src/HttpServer.cpp');
assert.ok(!/program_version"\s*:\s*"\d{3}\.\d{2}"/.test(httpSource),
  'program_version literals must come from AppVersion.h');
console.log('PASS: selected DVB private preview direct TS; per-session delivery telemetry');
