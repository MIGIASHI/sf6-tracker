"""Exercise settings persistence, retention and selected-key API against Python or native tracker."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
NATIVE = sys.argv[1:]
del sys.argv[1:]


class ServerSettingsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        if NATIVE:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1',0)); port = sock.getsockname()[1]
            self.proc = subprocess.Popen([NATIVE[0],self.tmp.name,NATIVE[1],str(port)], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            self.addCleanup(self.stop_native)
            self.assertEqual(self.proc.stdout.readline().strip(), 'READY')
        else:
            from sf6tracker.config import Config
            from sf6tracker.store import Store
            from sf6tracker.server import serve
            app = SimpleNamespace(cfg=Config(self.tmp.name),store=Store(self.tmp.name),roster=lambda:[],decorate=lambda m:m)
            self.server = serve(app,0)
            threading.Thread(target=self.server.serve_forever,daemon=True).start()
            self.addCleanup(self.stop_python)
            port = self.server.server_address[1]
        self.base = f'http://127.0.0.1:{port}'

    def stop_native(self):
        _, error = self.proc.communicate('stop\n',timeout=10)
        self.assertEqual(self.proc.returncode,0,error)

    def stop_python(self):
        self.server.shutdown(); self.server.server_close()

    def request(self,path,data=None):
        body = None if data is None else json.dumps(data).encode()
        with urlopen(Request(self.base+path,data=body,headers={'Content-Type':'application/json'}),timeout=10) as response:
            return json.load(response)

    def test_input_settings_replace_and_persist(self):
        self.request('/api/input-settings',{'type':'pad','positions':{'LS':[10,20]},'map':[2,3]})
        self.request('/api/input-settings',{'type':'keyboard','positions':{},'map':[81,256]})
        saved = self.request('/api/input-settings')
        self.assertEqual(saved['positions'],{})
        self.assertEqual(saved['map'],[81,256])
        cfg = json.loads(Path(self.tmp.name,'config.json').read_text(encoding='utf-8'))
        self.assertEqual(cfg['input_display'],saved)

    def test_retention_setting_prunes_immediately(self):
        self.request('/api/matches',{'ts':time.time()-10800,'result':'win'})
        self.request('/api/matches',{'ts':time.time()-30,'result':'lose'})
        self.request('/api/config',{'history':{'retention_hours':2,'reset_on_character_change':True}})
        matches = self.request('/api/matches')
        self.assertEqual(len(matches),1)
        self.assertEqual(matches[0]['result'],'lose')
        self.assertEqual(self.request('/api/config')['history']['retention_hours'],2)

    def test_invalid_retention_is_rejected(self):
        for value in (-1,'one',90000,True):
            with self.assertRaises(HTTPError) as error: self.request('/api/config',{'history':{'retention_hours':value}})
            self.assertEqual(error.exception.code,400)
        with self.assertRaises(HTTPError): self.request('/api/config',{'history':[]})

    def test_selected_keys_and_wheel_only(self):
        result = self.request('/api/keys?codes=65,256,257')
        self.assertTrue(set(result['pressed']) <= {65,256,257})
        for codes in ('0','255','999','word',','.join(['65']*33)):
            with self.assertRaises(HTTPError) as error: self.request('/api/keys?codes='+codes)
            self.assertEqual(error.exception.code,400)

    def test_invalid_input_settings_is_rejected(self):
        with self.assertRaises(HTTPError) as error: self.request('/api/input-settings',[])
        self.assertEqual(error.exception.code,400)


if __name__ == '__main__': unittest.main()
