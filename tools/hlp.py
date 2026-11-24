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

"""Decode a help file of the old format.

Layout (little endian):
  u16 maxlen(2000) | u16 ntopics(379) | u16 treebytes(664)
  tree: treebytes/4 nodes of two s16 (child0,child1); <0 => node index -v, >=0
  => literal byte.
        Bit 1 selects child0, bit 0 selects child1 (inverted), bits read MSB
        first.
  u32 topic_offset[ntopics]   (absolute file offsets; topic 0 starts right after
  table)
  topic: binary header (list of 5-byte link records) followed by Huffman-coded
  text.
"""
import sys as _s; _s.dont_write_bytecode = True
import struct, sys, re
def load(path):
    d=open(path,'rb').read()
    maxlen,nt,treebytes=struct.unpack_from('<HHH',d,0)
    n=treebytes//4
    w=struct.unpack_from('<%dh'%(n*2),d,6)
    nodes=[(w[2*i],w[2*i+1]) for i in range(n)]
    tab=6+treebytes
    offs=list(struct.unpack_from('<%dI'%nt,d,tab))
    return d,nodes,offs
# Map CP437 art to plain ASCII (project rule: ASCII text only).
import unicodedata
M={}
for ch in "\u2550\u2500\u254c\u2504": M[ch]='-'
for ch in "\u2551\u2502\u2503": M[ch]='|'
for ch in ("\u2554\u2557\u255a\u255d\u250c\u2510\u2514\u2518\u251c\u2524"
           "\u252c\u2534\u253c\u2560\u2563\u2566\u2569\u256c"): M[ch]='+'
M.update({'\u2591':'.','\u2592':':','\u25a0':'#','\u2580':'^','\u2584':'_',
    '\u2588':'#','\u00b7':'.','\u2026':'...',
 '\u221a':'v','\u2014':'--','\u2013':'-','\u2192':'->','\u2190':'<-',
 '\u2191':'^','\u2193':'v','\u00f7':'/','\u00d7':'x',
 '\u25ba':'>','\u25c4':'<','\u25bc':'v','\u25b6':'>','\u00a3':'L','\u2212':'-',
 '\u27e6':'[[','\u27e7':']]',
 '\u00bd':'1/2','\u00b1':'+/-','\u00b0':'deg','\u00a7':'Sec.',
 '\u241e':'<NBSP>','\u00a9':'(c)','\u00df':'ss'})
def conv(s):
    o=[]
    for ch in s:
        if ord(ch)<128: o.append(ch)
        elif ch in M: o.append(M[ch])
        else:
            o.append(unicodedata.normalize('NFD',ch)[0])
    return ''.join(o)

def decode_bits(d,nodes,startbyte,endbyte,bitoff=0):
    out=bytearray();node=0;pos=startbyte*8+bitoff;end=endbyte*8
    while pos<end:
        bit=(d[pos>>3]>>(7-(pos&7)))&1; pos+=1
        v=nodes[node][1-bit]
        if v<0: node=-v; continue
        node=0; out.append(v&255)
    return bytes(out)
def printable(b):
    if not b: return 0
    ok=sum(1 for c in b if 32<=c<127 or c in (10,0,13) or c>=0x80 and c<0xe0
        or c in (0x0b,))
    return ok/len(b)

def topic(d,nodes,offs,t):
    """Return (header_bytes, text_bytes) for topic t."""
    o=offs[t]; end=(offs+[len(d)])[t+1] if t+1<len(offs) else len(d)
    blob=d[o:end]
    m=re.search(rb'\x97\x00.\x18\x0a(..)',blob,re.S)
    if not m: return blob,b''
    n=struct.unpack('<H',m.group(1))[0]
    start=o+m.end()
    out=bytearray();node=0;pos=start*8
    while len(out)<n and pos<end*8:
        bit=(d[pos>>3]>>(7-(pos&7)))&1; pos+=1
        v=nodes[node][1-bit]
        if v<0: node=-v; continue
        node=0; out.append(v&255)
    return blob[:m.end()],bytes(out)

def expand(x):
    """Expand 00 <count> <char> run-length codes and convert CP437 -> str."""
    out=bytearray(); i=0
    while i<len(x):
        if x[i]==0 and i+2<len(x):
            out+=bytes([x[i+2]])*x[i+1]; i+=3
        else:
            out.append(x[i]); i+=1
    return conv(out.decode('cp437'))
def title(txt):
    for ln in txt.split('\n'):
        t=ln.strip('- \r')
        if t: return t
    return ''
if __name__=='__main__':
    import os
    d,nodes,offs=load(sys.argv[1]); outdir=sys.argv[2]
    os.makedirs(outdir,exist_ok=True)
    idx=[]; alltxt=[]
    for t in range(len(offs)):
        h,x=topic(d,nodes,offs,t)
        txt=expand(x)
        ti=title(txt)
        idx.append((t,ti))
        alltxt.append('#### TOPIC %d: %s\n%s\n'%(t,ti,txt))
        open(os.path.join(outdir,'%03d.txt'%t),'w',encoding='ascii').write(txt)
    open(os.path.join(outdir,'ALL.txt'),'w',
        encoding='ascii').write('\n'.join(alltxt))
    open(os.path.join(outdir,'INDEX.txt'),'w',
        encoding='ascii').write('\n'.join('%03d  %s'%i for i in idx))
    print(len(idx),'topics')
