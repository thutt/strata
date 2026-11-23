#!/usr/bin/env python3
"""Makes the probe document of boxes and pictures for the original from
notes.doc (made by manual/mkmanual.py from notes.src): a block with a picture
marker follows the text block of every level.  The generator writes a record
the original refuses (ZNP3, caption numbers) when it has figures, so the
picture blocks are put in here."""
import re
import sys

d = open(sys.argv[1], 'rb').read()
m = b'B\r\nT\x1325(1CD2.GMF\x00\r\n22500\r\n\x13)\x1d\x00\r\n'
out = re.sub(rb'(TText of level [a-z]+, [^\r]*\r\n)', lambda x: x.group(1) + m,
             d)
open(sys.argv[2], 'wb').write(out)
