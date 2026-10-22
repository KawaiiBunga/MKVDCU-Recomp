"""Validate full-byte texture fingerprints and inspect real ARM CRC codegen."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent.parent
NATIVE=ROOT/'targets/mkvsdcu-nx/thirdparty/nfsmw-deko3d'
TOOLS=Path('E:/tools')
ENV=dict(os.environ,TEMP=str(TOOLS/'tmp'),TMP=str(TOOLS/'tmp'))
TEST=r'''
#include "mkvsdcu_texture_hash.h"
#include <nmmintrin.h>
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace nfsmw::nativo::texture_hash;
// x86 CRC32 is Castagnoli. IEEE uses the independent scalar oracle.
struct X86 : Scalar {
 static uint32_t C64(uint32_t s,uint64_t v){return uint32_t(_mm_crc32_u64(s,v));}
 static uint32_t C32(uint32_t s,uint32_t v){return _mm_crc32_u32(s,v);}
 static uint32_t C8(uint32_t s,uint8_t v){return _mm_crc32_u8(s,v);}
};
int main(){
 const auto* text=reinterpret_cast<const uint8_t*>("123456789");
 uint32_t c=~0u,i=~0u;
 for(unsigned n=0;n<9;++n){c=Scalar::C8(c,text[n]);i=Scalar::I8(i,text[n]);}
 CHECK(~c==0xe3069283u && ~i==0xcbf43926u);
 uint64_t random=0x123456789abcdef0ull;
 auto next=[&]{random^=random<<13;random^=random>>7;random^=random<<17;return random;};
 std::array<uint8_t,8192> bytes{};
 for(auto& b:bytes)b=uint8_t(next());
 for(unsigned n=0;n<5000;++n){
   const unsigned offset=unsigned(next()%32),size=unsigned(next()%4097);
   const uint64_t seed=next(),word=next();const uint32_t start=uint32_t(next());
   CHECK(X86::C64(start,word)==Scalar::C64(start,word));
   CHECK(X86::C32(start,uint32_t(word))==Scalar::C32(start,uint32_t(word)));
   CHECK(Fingerprint<X86>(bytes.data()+offset,size,seed)==Fingerprint<Scalar>(bytes.data()+offset,size,seed));
 }
 // Read right up to a protected page: no tail load may cross the input range.
 auto* guard=static_cast<uint8_t*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 CHECK(guard);DWORD old;CHECK(VirtualProtect(guard+4096,4096,PAGE_NOACCESS,&old));
 for(unsigned n=0;n<4096;++n)guard[n]=uint8_t(n);
 for(unsigned size=0;size<=257;++size){
   const auto* data=guard+4096-size;
   CHECK(Fingerprint<X86>(data,size,random)==Fingerprint<Scalar>(data,size,random));
 }
 CHECK(VirtualFree(guard,0,MEM_RELEASE));
 for(unsigned size: {1u,4u,8u,16u,17u,31u,64u,127u,257u}){
   const uint64_t before=Fingerprint<X86>(bytes.data(),size);
   for(unsigned n=0;n<size;++n){bytes[n]^=0x80;CHECK(Fingerprint<X86>(bytes.data(),size)!=before);bytes[n]^=0x80;}
   CHECK(Fingerprint<X86>(bytes.data(),size,123)!=before);
 }
 const auto base=Fingerprint<X86>(bytes.data(),4096);
 const auto chain=Fingerprint<X86>(bytes.data()+4096,2048,base);
 bytes[5000]^=1;CHECK(Fingerprint<X86>(bytes.data()+4096,2048,base)!=chain);
 bytes[5000]^=1;CHECK(Fingerprint<X86>(bytes.data()+4096,2048,base)==chain);
 CHECK(SelfTest());
 std::puts("PASS: known IEEE/Castagnoli vectors; 5000 seeded/aligned differential cases; protected-page tails; byte mutations and chained mips");
}
'''
WRAPPER='''#include "mkvsdcu_texture_hash.h"
static_assert(nfsmw::nativo::texture_hash::kAvailable);
extern "C" uint64_t MKvDCUTextureFingerprint(const uint8_t* p,size_t n,uint64_t s){
return nfsmw::nativo::texture_hash::Fingerprint<nfsmw::nativo::texture_hash::Hardware>(p,n,s);}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='mkvsdcu-texture-hash-',dir=TOOLS/'tmp') as folder:
        p=Path(folder);(p/'test.cpp').write_text(TEST);(p/'arm.cpp').write_text(WRAPPER)
        subprocess.run([str(TOOLS/'llvm-mingw/llvm-mingw-20261006-ucrt-x86_64/bin/clang++.exe'),
            '-std=c++23','-O2','-msse4.2','-static','-I'+str(NATIVE),str(p/'test.cpp'),'-o',str(p/'test.exe')],check=True,env=ENV)
        result=subprocess.run([str(p/'test.exe')],check=True,env=ENV,capture_output=True,text=True,timeout=60)
        print(result.stdout.strip())
        subprocess.run([str(TOOLS/'devkitPro/devkitA64/bin/aarch64-none-elf-g++.exe'),
            '-std=c++23','-O3','-march=armv8-a+crc+crypto','-fno-exceptions','-fno-rtti',
            '-I'+str(NATIVE),'-c',str(p/'arm.cpp'),'-o',str(p/'arm.o')],check=True,env=ENV)
        assembly=subprocess.run([str(TOOLS/'devkitPro/devkitA64/bin/aarch64-none-elf-objdump.exe'),
            '-d',str(p/'arm.o')],check=True,capture_output=True,text=True).stdout
        assert all(op in assembly for op in ('crc32cx','crc32x','crc32cw','crc32w','crc32cb','crc32b'))
        assert '\tbl\t' not in assembly,'Unexpected hot-path function call'
        print('PASS: actual AArch64 object uses both hardware CRC polynomials and has no hot-path calls')
        report={'host':result.stdout.strip(),'aarch64_crc_instructions':True,'hot_path_calls':False,
            'header_sha256':hashlib.sha256((NATIVE/'mkvsdcu_texture_hash.h').read_bytes()).hexdigest()}
        (ROOT/'research/mkvsdcu-deko3d/hardware-visible-scene-20261022/texture-hash-regression.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
