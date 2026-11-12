'use strict';
const assert = require('assert').strict;
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const manager = read('src/OscamMiniManager.cpp');
const http = read('src/HttpServer.cpp');
const page = manager.split('return R"HTML(')[1].split(')HTML";')[0];
const script = page.split('<script>')[1].split('</script>')[0];
new Function(script); // Syntax check the actual embedded page.
assert.match(page, /adminlte-3\.2\.0\.min\.css/);
assert.ok(!/[А-Яа-яЁё]/.test(page), 'mini-OSCam page should have English source text');
assert.match(script, /enabled:e\.querySelector\('\.reader-enabled'\)\.checked/);
assert.match(script, /keepalive:keepalive\.checked/);
assert.match(script, /detect:e\.dataset\.detect\|\|'cd'/);
assert.match(script, /if\(x\.ok\)await loadAll\(\)/, 'a failed save must not discard form values');
assert.match(script, /const text=await r\.text\(\)/, 'read an error response only once');
assert.match(manager, /mkstemp\(name\.data\(\)\)/, 'credentials are written with private permissions');
assert.match(http, /target == "\/preview\/adminlte-3\.2\.0\.min\.css"/);
assert.match(http, /text\/css; charset=UTF-8/);
assert.match(read('scripts/install_from_source.sh'), /adminlte-3\.2\.0\.min\.css/);
assert.match(read('src/ConfigManager.cpp'), /config\.language != "es"/);
assert.match(http, /translations\.es =/);
assert.ok(fs.statSync(path.join(root, 'web/vendor/adminlte-3.2.0.min.css')).size > 100000);
assert.ok(read('web/vendor/LICENSE.adminlte').includes('MIT License'));

// Exercise the real mini-OSCam translation function, including backend errors.
const start = script.indexOf('const spanish =');
const end = script.indexOf('const originals =', start);
const ctx = vm.createContext({locale:'es'});
vm.runInContext(script.slice(start, end) + '\nthis.translate = tr;', ctx);
assert.equal(ctx.translate('Save and restart'), 'Guardar y reiniciar');
assert.equal(ctx.translate('Card ready'), 'Tarjeta lista');
assert.equal(ctx.translate('Invalid device for reader card1'), 'Dispositivo inválido para el lector card1');
ctx.locale = 'en';
assert.equal(ctx.translate('Card ready'), 'Card ready');
console.log('PASS: mini-OSCam localization, state preservation and local AdminLTE assets');
