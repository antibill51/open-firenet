// Checks the translations of the web page (the I18N object of open-firenet/web/index.html): every language has
// exactly the keys of English, at every level, no empty text, and the values that are not texts left as they are. Run by test/build_and_test.sh.
//   node test/i18n_test.mjs            all languages
//   node test/i18n_test.mjs it         only this one, with the list of what is missing (handy while translating)
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const html = fs.readFileSync(path.join(root, 'open-firenet', 'web', 'index.html'), 'utf8');
const start = html.indexOf('const I18N = {');
const end = html.indexOf('\n};', start);
if (start < 0 || end < 0) { console.error('I18N object not found in index.html'); process.exit(1); }
const I18N = new Function(html.slice(start, end + 3) + '\nreturn I18N;')();

function keys(obj, prefix = '') {
  let out = [];
  for (const [k, v] of Object.entries(obj)) {
    if (v && typeof v === 'object') out = out.concat(keys(v, prefix + k + '.'));
    else out.push(prefix + k);
  }
  return out;
}
function get(obj, dotted) { return dotted.split('.').reduce((o, k) => (o == null ? o : o[k]), obj); }

const reference = keys(I18N.en);
const only = process.argv[2];
let failures = 0;
for (const lang of Object.keys(I18N)) {
  if (only && lang !== only) continue;
  const mine = new Set(keys(I18N[lang]));
  const missing = reference.filter(k => !mine.has(k));
  const extra = [...mine].filter(k => !reference.includes(k));
  const empty = [...mine].filter(k => typeof get(I18N[lang], k) === 'string' && get(I18N[lang], k).trim() === '');
  // Values that are not texts (flags of the state table...) are settings, the same in every language.
  const changed = reference.filter(k => mine.has(k) && typeof get(I18N.en, k) !== 'string' && get(I18N[lang], k) !== get(I18N.en, k));
  for (const k of missing) console.log(`ECHEC ${lang}: missing "${k}" (English: ${JSON.stringify(get(I18N.en, k))})`);
  for (const k of extra) console.log(`ECHEC ${lang}: "${k}" does not exist in English (typo, or a key that was removed)`);
  for (const k of empty) console.log(`ECHEC ${lang}: "${k}" is empty`);
  for (const k of changed) console.log(`ECHEC ${lang}: "${k}" is not a text and must stay ${JSON.stringify(get(I18N.en, k))}`);
  failures += missing.length + extra.length + empty.length + changed.length;
}
if (only && !I18N[only]) { console.log(`ECHEC no language "${only}" in I18N`); failures++; }
console.log(`i18n: ${Object.keys(I18N).join(', ')} | ${reference.length} texts each, ${failures} failures`);
process.exit(failures ? 1 : 0);
