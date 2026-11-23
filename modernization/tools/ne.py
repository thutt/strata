#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""Dump NE ('overlay') structure of the modules of a program under study;
optionally extract segments."""
import sys as _s; _s.dont_write_bytecode = True
import struct, sys, os
def parse(path):
    d = open(path,'rb').read()
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[ne:ne+2] == b'NE'
    (ver,rev,entry,entlen,crc,flags,autods,heap,stack,csip,sssp,nseg,nmod,
     nonresz,segtab,rsrc,restab,modtab,imptab,nonres,nmov,shift,nres,
     os_) = struct.unpack_from('<BBHHIHHHHIIHHHHHHHHIHHHB', d, ne+2)
    shift = shift or 9
    segs=[]
    for i in range(nseg):
        o,l,f,m = struct.unpack_from('<HHHH', d, ne+segtab+i*8)
        segs.append(dict(n=i+1,off=o<<shift,len=l or 0x10000,flags=f,
            minalloc=m or 0x10000))
    # names
    def names(pos, absolute=False):
        out=[]; p=pos
        while True:
            n=d[p]
            if n==0: break
            nm=d[p+1:p+1+n].decode('latin1'); idx=struct.unpack_from('<H',d,p+1
                +n)[0]
            out.append((idx,nm)); p+=n+3
        return out
    res = names(ne+restab)
    nonresn = names(nonres)
    mods=[]
    for i in range(nmod):
        o=struct.unpack_from('<H',d,ne+modtab+i*2)[0]; n=d[ne+imptab
            +o] if ne+imptab+o<len(d) else 0
        mods.append(d[ne+imptab+o+1:ne+imptab+o+1+n].decode('latin1'))
    return dict(d=d,ne=ne,flags=flags,csip=csip,sssp=sssp,nseg=nseg,segs=segs,
        res=res,nonres=nonresn,mods=mods,shift=shift,hdr=dict(entry=entry,
        heap=heap,stack=stack,segtab=segtab,restab=restab,modtab=modtab,
        imptab=imptab,nonres=nonres,nmov=nmov))
if __name__=='__main__':
    p=parse(sys.argv[1])
    print('flags %04x nseg %d csip %08x sssp %08x shift %d'%(p['flags'],
        p['nseg'],p['csip'],p['sssp'],p['shift']), p['hdr'])
    print('modules:',p['mods'])
    for s in p['segs']: print('seg',s['n'],hex(s['off']),hex(s['len']),
        hex(s['flags']))
    print('resident names:',len(p['res']),p['res'][:200])
    print('nonres:',p['nonres'][:20])
    if len(sys.argv)>2:
        os.makedirs(sys.argv[2],exist_ok=True)
        for s in p['segs']:
            open(os.path.join(sys.argv[2],'seg%02d.bin'%s['n']),
                'wb').write(p['d'][s['off']:s['off']+s['len']])
