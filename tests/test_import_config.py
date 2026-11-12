import json
import stat
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/import_config.py'

class UnifiedImportTest(unittest.TestCase):
    def run_cli(self, source, target, *args):
        return subprocess.run(['python3', str(SCRIPT), str(source), str(target), *map(str,args)],
                              text=True, capture_output=True)

    def test_astra_txt_preview_and_optional_partial(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); config = root/'config.txt'; target = root/'existing.json'; out = root/'review.json'
            data = {'softcam':[{'pass':'DO_NOT_LEAK'}], 'dvb_tune':[{'id':'t','adapter':0,'device':0,
                    'frequency':'11010','symbolrate':'27900','type':'S2','polarization':'V'}],
                    'make_stream':[{'id':'one','name':'First','input':['http://192.0.2.10/one.ts']},
                    {'id':'two','name':'Two','input':['dvb://t#pnr=111&cam=x']},
                    {'id':'bad','name':'Bad','input':['exec://no-execute']}]}
            config.write_text(json.dumps(data),encoding='utf8')
            target.write_text(json.dumps({'password':'enc:v1:example','streams':[]}),encoding='utf8')
            old = target.read_bytes(); prev = self.run_cli(config,target,'--details')
            self.assertEqual(prev.returncode,0,prev.stderr)
            self.assertIn('Formato detectado: Astra/Cesbo',prev.stdout)
            self.assertIn('dvb_needs_adapter_and_lnb_confirmation: 1',prev.stdout)
            self.assertIn('Pendiente: two — dvb_needs_adapter_and_lnb_confirmation',prev.stdout)
            self.assertIn('Pendiente: bad — rejected_unsafe_or_unsupported',prev.stdout)
            self.assertIn('rejected_unsafe_or_unsupported: 1',prev.stdout)
            self.assertNotIn('DO_NOT_LEAK',prev.stdout + prev.stderr)
            self.assertNotEqual(self.run_cli(config,target,'--output',out).returncode,0)
            self.assertFalse(out.exists())
            result = self.run_cli(config,target,'--allow-partial','--output',out)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('partial',result.stdout)
            review = json.loads(out.read_text())
            self.assertEqual([s['name'] for s in review['streams']], ['First'])
            self.assertEqual(stat.S_IMODE(out.stat().st_mode),0o600)
            self.assertNotIn('DO_NOT_LEAK',out.read_text())
            self.assertEqual(target.read_bytes(),old)

    def test_flussonic_txt_preview_and_udp(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); config=root/'config.txt'; target=root/'existing.json'; out=root/'review.json'
            config.write_text('''# global
edit_auth admin SECRET;
stream uno { input m4f://192.0.2.20:2086/upstream; title "A & E"; push udp://ens37@239.1.1.85:1085; }
stream dos { input publish://; title Other; }
''', encoding='utf-8-sig')
            target.write_text('{"streams":[]}',encoding='utf8'); old=target.read_bytes()
            preview=self.run_cli(config,target,'--details','--interface','ens37=ens37')
            self.assertEqual(preview.returncode,0,preview.stderr)
            self.assertIn('Formato detectado: Flussonic',preview.stdout)
            self.assertIn('unsupported_input: 1',preview.stdout)
            self.assertIn('Pendiente: dos — unsupported_input',preview.stdout)
            self.assertNotIn('SECRET',preview.stdout + preview.stderr)
            self.assertNotEqual(self.run_cli(config,target,'--interface','ens37=ens37','--output',out).returncode,0)
            self.assertFalse(out.exists())
            result=self.run_cli(config,target,'--interface','ens37=ens37','--allow-partial','--output',out)
            self.assertEqual(result.returncode,0,result.stderr)
            review=json.loads(out.read_text()); self.assertEqual(len(review['streams']),1)
            stream=review['streams'][0]
            self.assertEqual(stream['name'],'A & E')
            self.assertEqual(stream['input_uri'],'http://192.0.2.20:2086/upstream/index.m3u8')
            self.assertEqual((stream['output_host'],stream['output_port']),('239.1.1.85',1085))
            self.assertFalse(stream['auto_start']); self.assertNotIn('SECRET',out.read_text())
            self.assertEqual(target.read_bytes(),old)
            self.assertNotEqual(self.run_cli(config,target,'--interface','ens37=ens37','--allow-partial','--output',out).returncode,0)

    def test_installed_entry_point_and_collision_guard(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td); tools = root / 'tools'; tools.mkdir()
            for name in ('import_config.py', 'import_astra_channels.py', 'import_flussonic_channels.py'):
                shutil.copy2(ROOT / 'scripts' / name, tools / name)
            cfg = root/'config.txt'; target = root/'target.json'; out = root/'out.json'
            cfg.write_text('stream a { input http://example.invalid/a.ts; push udp://239.1.1.1:1000; }\n'
                           'stream b { input http://example.invalid/b.ts; push udp://239.1.1.1:1000; }\n'
                           'stream c { input publish://; }')
            target.write_text('{"streams":[]}')
            base = ['python3', str(tools/'import_config.py'), str(cfg), str(target)]
            rejected = subprocess.run(base + ['--allow-partial','--output',str(out)],capture_output=True,text=True)
            self.assertNotEqual(rejected.returncode,0)
            self.assertIn('udp_endpoint_conflicts',rejected.stderr)
            self.assertFalse(out.exists())
            allowed = subprocess.run(base + ['--allow-partial','--allow-duplicate-udp','--output',str(out)],capture_output=True,text=True)
            self.assertEqual(allowed.returncode,0,allowed.stderr)
            self.assertEqual(len(json.loads(out.read_text())['streams']),2)

    def test_malformed_udp_is_reported_and_blocks_partial_by_default(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); cfg=root/'config.txt'; dst=root/'dst.json'; out=root/'out.json'
            cfg.write_text('stream good { input http://example.invalid/a.ts; push udp://239.1.1.1:1000; }\n'
                           'stream bad { input http://example.invalid/b.ts; push udp://[broken; }')
            dst.write_text('{"streams":[]}')
            result=self.run_cli(cfg,dst,'--details','--output',out)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Pendiente: bad — invalid_udp',result.stdout)
            self.assertFalse(out.exists())

    def test_unrecognized_or_included_config_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); cfg=root/'config.txt'; dst=root/'dst.json'; dst.write_text('{"streams":[]}')
            for text in ('{}', '{bad}', 'hello;', 'include /tmp/other.conf; stream a { input http://example.invalid/a; }'):
                cfg.write_text(text)
                self.assertNotEqual(self.run_cli(cfg,dst).returncode,0)

if __name__ == '__main__': unittest.main()
