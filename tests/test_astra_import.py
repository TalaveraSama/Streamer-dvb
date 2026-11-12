import importlib.util
import json
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/import_astra_channels.py'


class ImportTest(unittest.TestCase):
    def test_preview_and_private_output(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            source = {
                'softcam': [{'user': 'DO_NOT_COPY_USER', 'pass': 'DO_NOT_COPY_SECRET'}],
                'users': {'DO_NOT_COPY_USER': {'cipher':'DO_NOT_COPY_HASH'}},
                'dvb_tune': [
                    {'id':'t1', 'adapter':0, 'device':0, 'type':'S2', 'frequency':'10970',
                     'symbolrate':'27900', 'polarization':'V', 'modulation':'PSK8', 'fec':'2/3'},
                    {'id':'t2', 'adapter':1, 'device':0, 'type':'S2', 'frequency':'11054',
                     'symbolrate':'27900', 'polarization':'H', 'diseqc':'1'}],
                'make_stream': [
                    {'id':'a', 'name':'Canal', 'enable':True, 'input':['dvb://t1#pnr=112&cam=secret']},
                    {'id':'b', 'name':'HTTP', 'enable':False, 'input':['http://192.168.1.5/0.ts'],
                     'output':['srt://example.invalid:8000#streamid=secret']},
                    {'id':'c', 'name':'Switch', 'input':['dvb://t2#pnr=138&cam=secret']},
                    {'id':'d', 'name':'Danger', 'input':['exec://touch ' + str(d / 'executed')]},
                    {'id':'e', 'name':'Token', 'input':['http://example.invalid/0.ts?token=DO_NOT_COPY_SECRET']},
                    {'id':'f', 'name':'Dup', 'input':['dvb://t1#pnr=112&cam=secret']},
                    {'id':'g', 'name':'Bad html', 'input':['http://example.invalid/" onfocus="bad']},
                ]}
            target = {'password':'enc:v1:existing', 'other':{'keep':True},
                      'streams':[{'id':'existing','input_uri':'http://example.invalid/existing.ts',
                                  'output_host':'127.0.0.1','output_port':20000}]}
            src = d/'astra.json'; dst = d/'current.json'; out = d/'review.json'
            src.write_text(json.dumps(source)); dst.write_text(json.dumps(target))
            before_src = src.read_bytes(); before_dst = dst.read_bytes()
            base = ['python3', str(SCRIPT), str(src), str(dst)]
            preview = subprocess.run(base, capture_output=True, text=True, check=True)
            self.assertIn('dvb_needs_adapter_and_lnb_confirmation: 3', preview.stdout)
            self.assertIn('http_importable: 1', preview.stdout)
            self.assertFalse(out.exists())
            args = ['--confirm-adapters','--lof1','9750','--lof2','10600','--slof','11700']
            incomplete = subprocess.run(base + args + ['--output',str(out)], capture_output=True, text=True)
            self.assertNotEqual(incomplete.returncode, 0)
            self.assertIn('dvb_needs_diseqc_mapping=1', incomplete.stderr)
            self.assertFalse(out.exists())
            args += ['--diseqc','t2=0']
            result = subprocess.run(base + args + ['--output',str(out)], capture_output=True, text=True, check=True)
            self.assertIn('rejected_unsafe_or_unsupported: 3', result.stdout)
            self.assertIn('duplicate: 1', result.stdout)
            self.assertIn('importable: 3', result.stdout)
            self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o600)
            document = json.loads(out.read_text())
            self.assertEqual(document['password'], target['password'])
            self.assertEqual(document['other'], target['other'])
            self.assertEqual(document['streams'][0], target['streams'][0])
            sat, http, switched = document['streams'][1:]
            self.assertEqual(sat['input_service_id'],112)
            self.assertIn('frequency_khz=10970000', sat['input_uri'])
            self.assertIn('modulation=8psk', sat['input_uri'])
            self.assertIn('fec=2%2F3', sat['input_uri'])
            self.assertEqual(http['input_uri'],'http://192.168.1.5/0.ts')
            self.assertEqual(sat['output_port'],20001)
            self.assertEqual(http['output_port'],20002)
            self.assertIn('diseqc_source=0', switched['input_uri'])
            for stream in (sat,http,switched):
                self.assertFalse(stream['auto_start'])
                self.assertEqual(stream['conditional_access_client'],'')
                self.assertEqual(stream['output_host'],'127.0.0.1')
            for secret in ('DO_NOT_COPY_SECRET','DO_NOT_COPY_USER','DO_NOT_COPY_HASH','exec://','srt://'):
                self.assertNotIn(secret, out.read_text())
            self.assertFalse((d/'executed').exists())
            self.assertEqual(src.read_bytes(),before_src)
            self.assertEqual(dst.read_bytes(),before_dst)
            # Repeat cannot overwrite a draft or the live configuration.
            self.assertNotEqual(subprocess.run(base + args + ['--output',str(out)],capture_output=True).returncode,0)
            self.assertNotEqual(subprocess.run(base + args + ['--output',str(dst)],capture_output=True).returncode,0)
            self.assertEqual(dst.read_bytes(),before_dst)
            result = subprocess.run(base + args,capture_output=True,text=True,check=True)
            self.assertIn('importable: 3',result.stdout)
            self.assertFalse((d/'executed').exists())

    def test_unsafe_http(self):
        spec = importlib.util.spec_from_file_location('import_astra', SCRIPT)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        self.assertTrue(module.safe_http('http://192.168.1.1:80/4.ts'))
        for uri in ('exec://bad', 'http://user:pass@example.com/', 'http://example.com/?token=secret',
                    'http://example.com/#secret', 'http://example.com/"x', 'http://[broken'):
            self.assertFalse(module.safe_http(uri), uri)


if __name__ == '__main__':
    unittest.main()
