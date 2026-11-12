import json
import importlib.util
import sys
from types import SimpleNamespace
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/import_flussonic_channels.py'

class FlussonicTest(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run(['python3', str(SCRIPT), *map(str,args)], capture_output=True, text=True)

    def test_preview_then_stage_and_security(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td); src = d/'flussonic.conf'; target = d/'target.json'; out = d/'review.json'
            src.write_text('''# Comment
http 2086;
edit_auth dummy NEVER_COPY_SECRET;
stream alpha {
  disabled false;
  input m4f://192.0.2.20:2086/different_source;
  title "A & E";
  provider "A & E";
  push udp://ens37@239.1.1.85:1085;
}
stream beta {
  disabled;
  input tshttp://192.0.2.10/beta.ts;
  title "Canal dos";
  push udp://ens37@239.1.1.85:1085;
  push rtmp://example.invalid/key_NEVER_COPY_SECRET;
}
stream gamma {
  input hlss://example.invalid/index.m3u8;
  title "Canal tres";
  prepush 3;
}
''')
            target.write_text(json.dumps({'password':'enc:v1:old','streams':[]})); before = target.read_bytes()
            result = self.run_tool(src,target)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('interface_needs_mapping: 2',result.stdout)
            self.assertNotIn('NEVER_COPY_SECRET', result.stdout)
            result = self.run_tool(src,target,'--output',out)
            self.assertNotEqual(result.returncode,0); self.assertFalse(out.exists())
            common = ['--interface','ens37=ens37']
            result = self.run_tool(src,target,*common,'--output',out)
            self.assertNotEqual(result.returncode,0); self.assertIn('udp_endpoint_conflicts=1',result.stderr); self.assertFalse(out.exists())
            result = self.run_tool(src,target,*common,'--allow-duplicate-udp','--output',out)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(stat.S_IMODE(out.stat().st_mode),0o600)
            self.assertIn('source_disabled: 1',result.stdout)
            self.assertIn('udp_endpoint_conflicts: 1',result.stdout)
            self.assertIn('non_udp_push_omitted: 1',result.stdout)
            self.assertIn('unconverted_directives: 1',result.stdout)
            self.assertNotIn('NEVER_COPY_SECRET',out.read_text())
            document = json.loads(out.read_text()); a,b,c = document['streams']
            self.assertEqual(a['name'],'A & E'); self.assertEqual(a['id'],'alpha')
            self.assertEqual(a['input_uri'],'http://192.0.2.20:2086/different_source/index.m3u8')
            self.assertEqual(a['input_mode'], 'hls')
            self.assertEqual(a['output_host'],'239.1.1.85'); self.assertEqual(a['output_port'],1085)
            self.assertEqual(a['interface_address'],'ens37')
            self.assertEqual(b['input_uri'],'http://192.0.2.10/beta.ts')
            self.assertEqual(c['input_uri'],'https://example.invalid/index.m3u8')
            self.assertEqual(c['input_mode'],'hls')
            self.assertEqual(c['output_host'],'127.0.0.1')
            self.assertTrue(all(not x['auto_start'] for x in (a,b,c)))
            self.assertEqual(target.read_bytes(),before)
            self.assertNotEqual(self.run_tool(src,target,*common,'--allow-duplicate-udp','--output',out).returncode,0)
            self.assertNotEqual(self.run_tool(src,target,*common,'--allow-duplicate-udp','--output',target).returncode,0)

    def test_unsafe_and_multi_input_do_not_execute(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'flus.conf'; target=d/'target.json'; out=d/'out.json'; marker=d/'executed'
            src.write_text(f'''stream commands {{ input "exec://touch {marker}"; }}
stream incoming {{ input publish://; }}
stream token {{ input hls://example.invalid/a.m3u8?token=SECRET; }}
stream backup {{ input http://example.invalid/a.ts; input http://example.invalid/b.ts; }}
''')
            target.write_text('{"streams":[]}')
            res=self.run_tool(src,target,'--output',out)
            self.assertNotEqual(res.returncode,0); self.assertFalse(out.exists()); self.assertFalse(marker.exists())
            self.assertIn('unsupported_input: 2',res.stdout)
            self.assertIn('input_needs_review: 1',res.stdout)
            self.assertIn('backup_order_needs_confirmation: 1',res.stdout)
            self.assertNotIn('SECRET',res.stdout + res.stderr)

    def test_meta_and_mixed_backups_require_review(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'flus.conf'; target=d/'target.json'; out=d/'out.json'
            src.write_text('''stream meta1 { input http://example.invalid/a.ts; meta program_id "1"; }
stream backup { input hls://example.invalid/a.m3u8; input tshttp://example.invalid/b.ts; }
''')
            target.write_text('{"streams":[]}')
            res=self.run_tool(src,target,'--confirm-backup-order','--output',out)
            self.assertNotEqual(res.returncode,0); self.assertFalse(out.exists())
            self.assertIn('meta_needs_review: 1',res.stdout)
            self.assertIn('incompatible_backup_modes: 1',res.stdout)
            res=self.run_tool(src,target,'--confirm-backup-order','--omit-meta','--output',out)
            self.assertNotEqual(res.returncode,0); self.assertFalse(out.exists())

    def test_m4f_default_and_optional_template(self):
        sys.path.insert(0, str(ROOT / 'scripts'))
        try:
            spec = importlib.util.spec_from_file_location('flus_import_test', SCRIPT)
            module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        finally:
            sys.path.pop(0)
        options = SimpleNamespace(m4f_template=None, allow_query=False)
        self.assertEqual(module.translate_input('m4f://192.0.2.7:2086/a0ek', options),
                         ('http://192.0.2.7:2086/a0ek/index.m3u8', 'hls'))
        self.assertEqual(module.translate_input('m4f://example.invalid/stream/', options),
                         ('http://example.invalid/stream/index.m3u8', 'hls'))
        self.assertEqual(module.translate_input('m4f://192.0.2.7:2086/source_id', options),
                         ('http://192.0.2.7:2086/source_id/index.m3u8', 'hls'))
        with self.assertRaises(ValueError):
            module.translate_input('m4f://192.0.2.7:2086/id?secret=token', options)
        with self.assertRaisesRegex(ValueError, 'unsafe_m4f'):
            module.translate_input('m4f://example.invalid:bad/id', options)
        with self.assertRaisesRegex(ValueError, 'input_needs_review'):
            module.translate_input('m4f://[broken/id', options)
        options.m4f_template = 'http://{host}:{port}/{path}/mpegts'
        self.assertEqual(module.translate_input('m4f://192.0.2.7:2086/source_id', options),
                         ('http://192.0.2.7:2086/source_id/mpegts', 'auto'))

    def test_unterminated_syntax_fails(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'flus.conf'; target=d/'target.json'; src.write_text('stream x { input http://example.invalid/a;')
            target.write_text('{"streams":[]}')
            self.assertNotEqual(self.run_tool(src,target).returncode,0)

if __name__ == '__main__': unittest.main()
