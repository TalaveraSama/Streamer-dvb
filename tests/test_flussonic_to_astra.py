import json
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/flussonic_to_astra.py'

class AstraRelayTest(unittest.TestCase):
    def run_tool(self, src, target, *args):
        return subprocess.run(['python3', str(SCRIPT), str(src), str(target), '--local-hls-base',
                               'http://192.0.2.7:2086', '--interface', 'ens37=ens36', *map(str,args)],
                              text=True, capture_output=True)

    def test_full_copy_conflicts_and_existing_amc(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'flus.conf'; target=d/'astra.json'; out=d/'review.json'
            src.write_text('''edit_auth ignored SOURCE_SECRET;
stream a0di { title "AMC"; input http://upstream/private; push udp://ens37@239.1.2.58:1058; }
stream a0ek { title "A & E"; input http://one/; input http://two/; push udp://ens37@239.1.1.85:1085; }
stream beta { disabled; title "Dos"; push udp://ens37@239.1.1.85:1085; push rtmp://server/SECRET; }
stream empty { title "Sin salida"; meta program_id 1; }
''')
            existing = {'softcam':[{'id':'a001','pass':'EXISTING_SECRET'}], 'users':{'admin':{'cipher':'EXISTING_HASH'}},
                'settings':{'http_play_hls':True}, 'gid':466789,
                'make_stream':[{'id':'a06d','name':'AMC','enable':True,'type':'spts',
                   'input':['http://192.0.2.7:2086/a0di/index.m3u8'], 'output':['udp://ens36@239.1.2.58:1058']}]}
            target.write_text(json.dumps(existing)); src_before=src.read_bytes(); before=target.read_bytes()
            res=self.run_tool(src,target,'--details'); self.assertEqual(res.returncode,0,res.stderr)
            self.assertIn('entrada_ya_en_astra: 1',res.stdout); self.assertIn('preparados: 3',res.stdout)
            self.assertIn('sin_salida_udp: 1',res.stdout); self.assertIn('conflictos_udp: 1',res.stdout)
            self.assertNotIn('SOURCE_SECRET',res.stdout+res.stderr); self.assertNotIn('EXISTING_SECRET',res.stdout+res.stderr)
            res=self.run_tool(src,target,'--output',out); self.assertNotEqual(res.returncode,0); self.assertFalse(out.exists())
            res=self.run_tool(src,target,'--allow-duplicate-udp','--output',out); self.assertEqual(res.returncode,0,res.stderr)
            result=json.loads(out.read_text()); self.assertEqual(result['make_stream'][0],existing['make_stream'][0])
            self.assertEqual(result['softcam'],existing['softcam']); self.assertEqual(result['users'],existing['users'])
            self.assertEqual(result['settings'],existing['settings']); self.assertEqual(result['gid'],466792)
            a,b,c=result['make_stream'][1:]; self.assertEqual([x['id'] for x in (a,b,c)], ['a06e','a06f','a06g'])
            self.assertEqual(a['input'],['http://192.0.2.7:2086/a0ek/index.m3u8'])
            self.assertEqual(a['output'],['udp://ens36@239.1.1.85:1085']); self.assertEqual(b['output'],a['output'])
            self.assertEqual(c['output'],[]); self.assertTrue(all(x['enable'] is False for x in (a,b,c)))
            self.assertNotIn('SOURCE_SECRET',out.read_text()); self.assertNotIn('rtmp://',out.read_text())
            self.assertEqual(stat.S_IMODE(out.stat().st_mode),0o600)
            self.assertEqual(src.read_bytes(),src_before); self.assertEqual(target.read_bytes(),before)
            self.assertNotEqual(self.run_tool(src,target,'--allow-duplicate-udp','--output',out).returncode,0)
            self.assertNotEqual(self.run_tool(src,target,'--allow-duplicate-udp','--output',target).returncode,0)

    def test_collision_existing_and_unsafe_interface_stop_copy(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'s'; target=d/'t'; out=d/'o'
            src.write_text('stream new { title "Test"; push udp://ens37@239.1.1.5:1234; }')
            target.write_text(json.dumps({'gid':466789,'make_stream':[{'id':'a06d','name':'old','input':[],
                'output':['udp://ens36@239.1.1.5:1234#cbr=5000']}], 'other':{'id':'a06e'}}))
            r=self.run_tool(src,target,'--details'); self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn('conflictos_udp: 1',r.stdout); self.assertIn('existente a06d',r.stdout)
            r=self.run_tool(src,target,'--output',out,'--allow-duplicate-udp'); self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual(json.loads(out.read_text())['make_stream'][-1]['id'],'a06f')
            src.write_text('stream new { push udp://ens99@239.1.1.5:1234; }')
            r=self.run_tool(src,target,'--output',d/'other','--allow-duplicate-udp')
            self.assertNotEqual(r.returncode,0); self.assertIn('interface_needs_mapping: 1',r.stdout)
            self.assertFalse((d/'other').exists())

    def test_bad_base_existing_name_and_invalid_target(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'s'; target=d/'t'; out=d/'o'
            src.write_text('stream new { title "same"; push udp://239.1.1.1:1; }')
            target.write_text(json.dumps({'gid':466789,'make_stream':[{'name':'same','input':['http://old/a'], 'output':[]}]}))
            r=self.run_tool(src,target,'--output',out); self.assertNotEqual(r.returncode,0)
            self.assertIn('nombre_ya_en_astra_revisar: 1',r.stdout); self.assertFalse(out.exists())
            target.write_text(json.dumps({'gid':466789,'make_stream':[{'name':'old','output':['udp://invalid:5000']}]}))
            self.assertNotEqual(self.run_tool(src,target,'--output',out).returncode,0)
            target.write_text(json.dumps({'gid':'466789','make_stream':[]}))
            self.assertNotEqual(self.run_tool(src,target,'--output',out).returncode,0)
            target.write_text(json.dumps({'gid':466789,'make_stream':[]}))
            r=subprocess.run(['python3',str(SCRIPT),str(src),str(target),'--local-hls-base',
                               'http://user:SECRET@192.0.2.7:2086','--interface','ens37=ens36'],capture_output=True,text=True)
            self.assertNotEqual(r.returncode,0); self.assertNotIn('SECRET', r.stderr)
    def test_existing_markdown_url_and_default_port(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td); src=d/'s'; target=d/'t'
            src.write_text('stream a0di { title "AMC"; push udp://ens37@239.1.1.5:1234; }\nstream other { push udp://239.1.1.5:1234; }')
            url='http://192.0.2.7:2086/a0di/index.m3u8'
            target.write_text(json.dumps({'gid':466789,'make_stream':[{'id':'a06d','name':'AMC',
                'input':[f'[{url}]({url})'], 'output':['rtp://ens36@239.1.1.5']}]}))
            r=self.run_tool(src,target,'--details'); self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn('entrada_ya_en_astra: 1',r.stdout); self.assertIn('conflictos_udp: 1',r.stdout)

if __name__=='__main__': unittest.main()
