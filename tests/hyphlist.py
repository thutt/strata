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

"""The personal hyphenation list in the print: the file hyphenation of the
settings folder (one word on a line, a hyphen where the word may be broken,
a word with none is never broken) becomes \\hyphenation in the LaTeX text, and
pdftotext of the printed document shows that

  - a word of the list breaks only where the list says (a word that TeX does
    not know how to break is broken at the marked places at the end of a line
    and is whole elsewhere),
  - a word with no hyphen (also written =word) is never broken where TeX
    would break it,
  - a word with an accented letter works with the fonts of the Printer,
  - a word with a character that the fonts do not have, a line that is no
    entry and a missing file leave the print alone.

    STRPRINT=... python3 -I -B tests/hyphlist.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
for tool in ('pdflatex', 'pdftotext'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='hyph')
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
EOL = b'\x1d'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


HDR = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF + b'P1HB0' +
       CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF + b'FHA1' + CRLF +
       b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF + b'YCL0' + CRLF)


def cp437(s):
    return s.encode('cp437')


def doc(word, fill=b'ab '):
    """40 paragraphs: k short words, the word, more text, so that over the
    paragraphs the word meets the end of a line in many places."""
    out = HDR + b'*B0' + CRLF + b'S' + CRLF
    for k in range(40):
        t = fill * k + word + b' and some words to end it.'
        out += (b'' if k == 0 else b'B' + CRLF) + trec(t)
    return out + b'*E' + CRLF


def prt(name, data, hyphenation=None):
    """Print data with the file hyphenation (text, UTF-8) in the settings
    folder, or without one; return the LaTeX text and the lines of the PDF."""
    d = os.path.join(tmp, name)
    os.makedirs(d)
    home = os.path.join(d, 'home')
    os.makedirs(home, mode=0o700)
    env = dict(os.environ, STRATA_HOME=home)
    if hyphenation is not None:
        p = os.path.join(home, 'hyphenation')
        with open(p, 'wb') as f:
            f.write(hyphenation)
        os.chmod(p, 0o600)
    p = os.path.join(d, 'doc.doc')
    open(p, 'wb').write(data)
    tex = os.path.join(d, 'x.tex')
    pdf = os.path.join(d, 'doc.pdf')
    subprocess.run([strprint, p, '--export-latex', tex], env=env,
        capture_output=True)
    subprocess.run([strprint, p, '-o', pdf, '--no-banner'], env=env,
        capture_output=True, timeout=300)
    text = ''
    if os.path.exists(pdf):
        text = subprocess.run(['pdftotext', '-layout', pdf, '-'],
            capture_output=True, text=True, errors='replace').stdout
    lines = [' '.join(l.split()) for l in text.split('\n')]
    return (open(tex, encoding='utf-8', errors='replace').read()
        if os.path.exists(tex) else ''), lines


def hyphenation_line(tex):
    m = re.search(r'\\hyphenation\{[^}]*\}', tex)
    return m.group(0) if m else ''


# ---- a word that only the list can break --------------------------------
W = b'Zqvxkjwbxzplqrstvwnzghkbmncxzlkjhgfd'
LIST = b'zqvxkjwbxzpl-qrstvwnzghkb-mncxzlkjhgfd\n'
END = re.compile(r'(Zqvxkjwbxzpl|qrstvwnzghkb|zqvxkjwbxzpl)-$')
tex0, lines0 = prt('none', doc(W))
check('no file: no \\hyphenation in the LaTeX text', 'hyphenation{' not in
    tex0 and tex0 != '')
own = [l for l in lines0 if l.endswith('-')]
check('no file: TeX breaks the word with its own patterns, at other places '
    '(%d times)' % len(own), len(own) >= 1 and
    not any(END.search(l) for l in lines0), lines0[:12])
tex1, lines1 = prt('list', doc(W), LIST)
check('the list is written to the preamble, before the document',
    hyphenation_line(tex1) == '\\hyphenation{zqvxkjwbxzpl-qrstvwnzghkb-'
    'mncxzlkjhgfd}' and tex1.index('\\hyphenation') <
    tex1.index('\\begin{document}'), tex1[:2000])
broke = [i for i, l in enumerate(lines1) if END.search(l)]
check('a word of the list is broken at its places at the end of a line '
    '(%d times)' % len(broke), len(broke) >= 3, lines1[:15])
check('the rest of a broken word starts the next line',
    all(re.match(r'(qrstvwnzghkb|mncxzlkjhgfd)', lines1[i + 1]) for i in
    broke), [lines1[i:i + 2] for i in broke])
check('and it is whole where it does not meet the end of a line (%d lines)'
    % sum('Zqvxkjwbxzplqrstvwnzghkbmncxzlkjhgfd' in l for l in lines1),
    sum('Zqvxkjwbxzplqrstvwnzghkbmncxzlkjhgfd' in l for l in lines1) >= 10,
    lines1[:12])
check('it is broken only at the marked places',
    not any(re.search(r'[a-z]-$', l) and not END.search(l) for l in lines1),
    [l for l in lines1 if re.search(r'[a-z]-$', l)])

# ---- never break ---------------------------------------------------------
N = b'internationalization'


def int_broken(l):
    """Does the line end with the first part of internationalization and a
    hyphen?"""
    t = l.split()[-1] if l.split() else ''
    return len(t) > 2 and t.endswith('-') and \
        'internationalization'.startswith(t[:-1].lower()) and \
        t[:-1].lower().startswith('inter')

FILL = b'Wxyzabcdefghij '
tex2, lines2 = prt('ctl', doc(N, FILL))
ctl = [l for l in lines2 if int_broken(l)]
check('control: without a list TeX breaks internationalization at the end of '
    'a line (%d times)' % len(ctl), len(ctl) >= 1, lines2[:12])
for form in (b'=internationalization\n', b'internationalization\n'):
    tex3, lines3 = prt('never%d' % len(form), doc(N, FILL), form)
    check('%s: the word is written without hyphens' % form.decode().strip(),
        hyphenation_line(tex3) == '\\hyphenation{internationalization}',
        hyphenation_line(tex3))
    check('%s: it is never broken (%d whole)' % (form.decode().strip(),
        sum('internationalization' in l for l in lines3)),
        not any(int_broken(l) for l in lines3) and
        sum('internationalization' in l for l in lines3) == 40, lines3[:12])

# ---- accents ---------------------------------------------------------------
A = cp437('zqvxkjwb\u00e9xzplqrstvwnzghkb')
tex4, lines4 = prt('accent', doc(A), 'zqvxkjwb\u00e9-xzplqrstvwnzghkb\n'
    .encode('utf-8'))
check('an accented word is written as it is (UTF-8, for the fonts of the '
    'Printer)', hyphenation_line(tex4) == '\\hyphenation{zqvxkjwb\u00e9-'
    'xzplqrstvwnzghkb}', hyphenation_line(tex4))
accb = [i for i, l in enumerate(lines4) if l.endswith('zqvxkjwb\u00e9-')]
check('an accented word breaks at its place (%d times) and the rest follows'
    % len(accb), len(accb) >= 1 and all(lines4[i + 1].startswith(
    'xzplqrstvwnzghkb') for i in accb), lines4[:14])
check('and it is whole elsewhere', sum(
    'zqvxkjwb\u00e9xzplqrstvwnzghkb' in l for l in lines4) >= 10, lines4[:14])

# ---- what is left out ---------------------------------------------------
tex5, lines5 = prt('bad', doc(W),
    'bad word\n\u2211-sum\nx1-y\n-lead\n\n  zqvxkjwbxzpl-qrstvwnzghkb-'
    'mncxzlkjhgfd  \n'.encode('utf-8'))
check('a line that is no entry and a word with a character outside the '
    'fonts are left out; the others stay',
    hyphenation_line(tex5) == '\\hyphenation{zqvxkjwbxzpl-qrstvwnzghkb-'
    'mncxzlkjhgfd}' and tex5 != '', hyphenation_line(tex5))
tex6, lines6 = prt('empty', doc(W), b'\n\nbad word\n')
check('a list with no usable word writes no command and the print is the '
    'one that there is with no file', 'hyphenation{' not in tex6 and
    lines6 == lines0 and len(lines6) > 40)
tex7, lines7 = prt('crlf', doc(W), LIST.replace(b'\n', b'\r\n'))
check('a list with CR LF lines works', hyphenation_line(tex7) ==
    '\\hyphenation{zqvxkjwbxzpl-qrstvwnzghkb-mncxzlkjhgfd}', tex7[:300])

print('hyphlist: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
