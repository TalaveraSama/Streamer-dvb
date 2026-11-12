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
  assert.match(context.html, /TVStreammerSAT5/);
  assert.match(context.html, /1&lt;&amp;/);
  assert.equal(context.html.includes(' EN'), language === 'en');
  assert.match(context.html, /onclick="closeModal\(\)"/);
  assert.doesNotMatch(context.html, /monkipnet|Лукомский|Беларусь|mailto:|about-donate|about-qr|wallet/i);
}
context.state = {}; context.language = 'es'; context.openAboutModal();
assert.match(context.html, />2<\/span>/);
console.log('PASS: About branding, dynamic version, escaping and languages');
