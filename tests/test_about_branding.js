'use strict';
const assert = require('assert').strict;
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const source = fs.readFileSync(path.join(__dirname, '../src/HttpServer.cpp'), 'utf8');
const about = source.slice(source.indexOf('function openAboutModal()'), source.indexOf('function openLoginModal()'));
const escape = source.slice(source.indexOf('function escapeHtmlValue('), source.indexOf('function normalizedOutputType('));
assert(about.startsWith('function openAboutModal()'));
const context = vm.createContext({state:{program_version:'1<&'},window:{__TVS_VERSION:'2'},language:'es', t:key=>key, openModal:html=>{context.html=html}});
vm.runInContext(escape + about + '\nthis.openAboutModal=openAboutModal;', context);
for (const language of ['es','en','ru']) {
  context.language = language;
  context.openAboutModal();
  assert.match(context.html, /TVListerSat/);
  assert.doesNotMatch(context.html, /TVStreammerSAT5/i);
  assert.equal((context.html.match(/class="about-row"/g) || []).length, 2);
  assert.match(context.html, /1&lt;&amp;/);
  assert.equal(context.html.includes(' EN'), language === 'en');
  assert.match(context.html, /onclick="closeModal\(\)"/);
  assert.doesNotMatch(context.html, /monkipnet|Лукомский|Беларусь|mailto:|about-donate|about-qr|wallet/i);
}
context.state = {}; context.language = 'es'; context.openAboutModal();
assert.match(context.html, />2<\/span>/);
console.log('PASS: About branding, dynamic version, escaping and languages');

const branding = source.slice(source.indexOf('function displayServerName('), source.indexOf('function updateLiveTiles()'));
vm.runInContext(branding + '\nthis.displayServerName=displayServerName;', context);
assert.equal(context.displayServerName('TVStreammerSAT5'), 'TVListerSat');
assert.equal(context.displayServerName('tvstreamersat'), 'TVListerSat');
assert.equal(context.displayServerName(''), 'TVListerSat');
assert.equal(context.displayServerName('Mi servidor'), 'Mi servidor');
assert.match(source, /headerServerName\.textContent = displayServerName\(state\.server_name\)/);
assert.match(source, /<title>TVListerSat<\/title>/);
assert.match(source, /data-initial-name="\$\{escapeHtmlValue\(displayServerName\(state\.server_name\)\)\}"/);
assert.match(source, /input\.value !== input\.dataset\.initialName/);
assert.match(fs.readFileSync(path.join(__dirname, '../src/OscamMiniManager.cpp'), 'utf8'), /href="\/">← TVListerSat<\/a>/);
