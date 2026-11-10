'use strict';

// 203.76 regression: the security controls must stay wired into the request
// pipeline. The behavioural logic itself is covered by the C++ test
// (tests/test_http_origin_20376.cpp); this test fails if a refactor silently
// removes or bypasses the integration points.

const assert = require('assert');
const { readFile } = require('./lib/version');

const http = readFile('src/HttpServer.cpp');
const httpHeader = readFile('src/HttpServer.h');
const configCpp = readFile('src/ConfigManager.cpp');
const configHeader = readFile('src/ConfigManager.h');
const oscam = readFile('src/OscamMiniManager.cpp');
const oscamHeader = readFile('src/OscamMiniManager.h');
const mainCpp = readFile('src/main.cpp');

// --- anti-CSRF ---------------------------------------------------------------
assert.ok(http.includes('#include "HttpOriginPolicy.h"'),
  'HttpServer must use the shared same-origin policy');
assert.match(http,
  /req\.method\(\) != http::verb::get && req\.method\(\) != http::verb::head &&\s*requestComesFromAnotherOrigin\(req\)/,
  'state-changing requests must be same-origin checked');
assert.ok(http.includes('tvs::http_security::evaluate('),
  'the Origin/Referer verdict must come from the tested policy');
assert.ok(http.includes('http::status::forbidden'),
  'cross-site requests must be answered with 403');

// --- request body limit -----------------------------------------------------
assert.match(http, /parser\.body_limit\(\s*static_cast<std::uint64_t>\(clampRequestBodyMb/,
  'the parser body limit must be bounded and configurable');
assert.ok(http.includes('http::status::payload_too_large'),
  'an oversized body must answer 413');
assert.ok(!http.includes('512ULL * 1024ULL * 1024ULL'),
  'the hard-coded 512 MiB body limit must not come back');

// --- authentication rate limiting -------------------------------------------
assert.ok(http.includes('isLoginRateLimited(') && http.includes('recordAuthFailure(') &&
  http.includes('clearAuthFailures('),
  'authentication failures must be rate limited');
assert.ok(http.includes('http::status::too_many_requests'),
  'a locked-out client must receive 429');
assert.match(httpHeader, /std::unordered_map<std::string, AuthFailureState> authFailures;/,
  'the failure state must be tracked per IP');

// --- response hardening -----------------------------------------------------
assert.ok(http.includes('applySecurityHeaders(res)') &&
  http.includes('X-Content-Type-Options') &&
  http.includes('X-Frame-Options'),
  'responses must carry the security headers');

// --- initial credentials ----------------------------------------------------
assert.match(configCpp, /RAND_bytes\(/, 'initial password must come from a CSPRNG');
assert.ok(configCpp.includes('generateInitialPassword()'),
  'ConfigManager must generate the first password');
assert.ok(!/defaultConfig\.password\s*=\s*"admin"/.test(configCpp),
  'the first-run password must never be the shipped default');
assert.ok(configHeader.includes('bindAddress') && configHeader.includes('requireStreamAuth') &&
  configHeader.includes('maxRequestBodyMb'),
  'deployment hardening keys must be part of AppConfig');

// --- no shell execution -----------------------------------------------------
// Comments are stripped first: the rationale comment above run() mentions the
// popen() implementation it replaced.
const stripCppComments = (source) =>
  source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
const oscamCode = stripCppComments(oscam);
assert.ok(!/\bpopen\s*\(/.test(oscamCode), 'OscamMiniManager must not use popen()');
assert.ok(!/\bpclose\s*\(/.test(oscamCode), 'OscamMiniManager must not use pclose()');
assert.ok(oscamCode.includes('posix_spawnp('), 'OscamMiniManager must spawn with argv');
assert.match(oscamHeader, /run\(const std::vector<std::string>& argv/,
  'the command runner must take an argv vector');
assert.match(oscam, /run\(\{"systemctl", "restart", kService\}, &rc\)/,
  'service control must be argv based');

// --- logs -------------------------------------------------------------------
assert.ok(!/login=" << configManager\.config\.login/.test(mainCpp),
  'the login must not be written to the service log');

// --- version plumbing -------------------------------------------------------
assert.ok(http.includes('window.__TVS_VERSION="__TVS_PROGRAM_VERSION__"'),
  'the page must receive the version from AppVersion.h');
assert.ok(http.includes('rendered.replace(position, placeholder.size(), kProgramVersion)'),
  'the placeholder must be resolved with kProgramVersion');

console.log('PASS: 203.76 security controls are wired into the request pipeline');
