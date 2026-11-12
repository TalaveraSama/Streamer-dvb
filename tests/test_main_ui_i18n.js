'use strict';
const assert = require('assert').strict;
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '../src/HttpServer.cpp'), 'utf8');
const first = source.slice(source.indexOf('const uiEnToEs ='), source.indexOf('function normalizeLanguage'));
const second = source.slice(source.indexOf('const uiRuToEn ='), source.indexOf('function applyRuntimeLocalization('));
assert(first.startsWith('const uiEnToEs =') && second.startsWith('const uiRuToEn ='));
const context = vm.createContext({language:'es', Node:{TEXT_NODE:3, ELEMENT_NODE:1}});
vm.runInContext(first + '\n' + second + `
this.tr = translateUiText;
this.text = localizeUiTextNode;
this.attrs = localizeUiAttributes;
this.ruEntries = [...uiRuToEn.entries()];
this.hasSpanish = key => uiEnToEs.has(key);
`, context);
assert.equal(context.tr('Входной URL (Основной)'), 'URL de entrada (principal)');
assert.equal(context.tr('Интерфейс вывода'), 'Interfaz de salida');
assert.equal(context.tr('Зациклить файл замены'), 'Repetir archivo de reemplazo');
assert.equal(context.tr('HTTP / HLS доступ'), 'Acceso HTTP / HLS');
assert.equal(context.tr('  Автозапуск\n'), '  Inicio automático\n');
assert.equal(context.tr('Основной · http://192.168.160.7:2086/a0ek/index.m3u8'), 'Principal · http://192.168.160.7:2086/a0ek/index.m3u8');
assert.equal(context.tr('Удалить MPTS «demo»?'), '¿Eliminar MPTS «demo»?');
assert.equal(context.tr('Выбран файл: demo.ts'), 'Archivo seleccionado: demo.ts');
assert.equal(context.tr('Сканирование /dev/dvb/adapter0/frontend1 · удержание LOCK...'), 'Escaneando /dev/dvb/adapter0/frontend1 · manteniendo LOCK...');
assert.equal(context.tr('Доступно: H.264 Auto=да, AAC нет'), 'Disponible: H.264 Auto=sí, AAC no');
assert.equal(context.tr('96 кбит/с'), '96 kbps');
assert.equal(context.tr('Select a CAM/Newcamd client for encrypted DVB services. FTA streams do not use this setting.'), 'Selecciona un cliente CAM/Newcamd para servicios DVB codificados. Los flujos FTA no usan este ajuste.');
const exempt = new Set(['Vitaliy Lukomskiy', '3840×2160 (4K UHD)', '3200×1800 (3K)', '2560×1440 (2K QHD)', '1920×1080 (Full HD)', '1280×720 (HD)']);
for (const [russian, english] of context.ruEntries) {
  assert(context.hasSpanish(english) || exempt.has(english), `Missing Spanish for ${russian}: ${english}`);
  assert(!/[А-Яа-яЁё]/.test(context.tr(russian)), `Untranslated Russian: ${russian}`);
}
// The same node and attribute switch between languages without losing the source.
const node = {nodeType:3,nodeValue:'Входной URL (Основной)',parentElement:{tagName:'LABEL',closest:()=>null}};
context.text(node); assert.equal(node.nodeValue, 'URL de entrada (principal)');
context.language = 'en'; context.text(node); assert.equal(node.nodeValue, 'Input URL (Primary)');
context.language = 'ru'; context.text(node); assert.equal(node.nodeValue, 'Входной URL (Основной)');
const values = {placeholder:'Оставьте пустым, чтобы не менять'};
const element = {nodeType:1,tagName:'INPUT',hasAttribute:k=>k in values,getAttribute:k=>values[k],setAttribute:(k,v)=>{values[k]=v;}};
context.language = 'es'; context.attrs(element); assert.equal(values.placeholder, 'Dejar vacío para conservar la contraseña actual');
context.language = 'ru'; context.attrs(element); assert.equal(values.placeholder, 'Оставьте пустым, чтобы не менять');
console.log('PASS: dashboard Spanish/English/Russian labels, dynamic text and language switching');
