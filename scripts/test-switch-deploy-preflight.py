"""Exercise the real deployment CLI against an in-memory FTP transport."""
import ftplib
import hashlib
import json
from pathlib import Path
import runpy
import sys
import tempfile

ROOT=Path(__file__).resolve().parent.parent
REQUIRED=['mkvsdcu_shaders_dk.nfsp','mkvsdcu_shadercache_dksh.bin','mkvsdcu_shader_limits.bin']


def main():
    original_ftp,original_argv=ftplib.FTP,sys.argv
    class FakeFTP:
        files={};writes=[]
        def connect(self,*a,**k):pass
        def login(self):pass
        def set_pasv(self,value):pass
        def cwd(self,path):pass
        def nlst(self):return list(self.files)
        def voidcmd(self,command):return '200'
        def size(self,name):return len(self.files[name])
        def quit(self):pass
        def close(self):pass
        def storbinary(self,command,stream,blocksize,callback):
            name=command.split(' ',1)[1];self.files[name]=stream.read();self.writes.append(name)
        def retrbinary(self,command,callback,blocksize):callback(self.files[command.split(' ',1)[1]])
        def rename(self,old,new):self.files[new]=self.files.pop(old)
    try:
        ftplib.FTP=FakeFTP
        with tempfile.TemporaryDirectory(prefix='mkvsdcu-deploy-test-',dir='E:/tools/tmp') as temp:
            folder=Path(temp);nro=b'fixture-only-not-an-nro'
            (folder/'mkvsdcu.nro').write_bytes(nro)
            (folder/'switch-build-verification.json').write_text(json.dumps(dict(graphics_api='deko3d',sha256=hashlib.sha256(nro).hexdigest())))
            sizes={name:dict(bytes=8) for name in REQUIRED}
            (folder/'shader-verification.json').write_text(json.dumps(dict(files=sizes)))
            sys.argv=['deploy-switch.py','--host','fixture','--dist',str(folder),'--files','mkvsdcu.nro','--receipt',str(folder/'receipt.json')]
            for invalid in ('missing','truncated'):
                FakeFTP.files={'mkvsdcu.nro':b'old'}
                if invalid=='truncated':FakeFTP.files.update({name:b'x' for name in REQUIRED})
                FakeFTP.writes=[]
                try:runpy.run_path(str(ROOT/'scripts/deploy-switch.py'),run_name='__main__')
                except SystemExit as e:assert 'Incomplete native installation' in str(e)
                else:raise AssertionError('Incomplete installation was accepted')
                assert FakeFTP.writes==[] and FakeFTP.files['mkvsdcu.nro']==b'old'
            FakeFTP.files={'mkvsdcu.nro':b'old',**{name:b'12345678' for name in REQUIRED}}
            FakeFTP.writes=[]
            runpy.run_path(str(ROOT/'scripts/deploy-switch.py'),run_name='__main__')
            receipt=json.loads((folder/'receipt.json').read_text())
            assert FakeFTP.files['mkvsdcu.nro']==nro
            assert FakeFTP.files[receipt['backups']['mkvsdcu.nro']]==b'old'
        print('PASS: missing/truncated assets rejected before upload; intact install uploads, verifies and retains rollback')
    finally:
        ftplib.FTP=original_ftp;sys.argv=original_argv


if __name__=='__main__':main()
