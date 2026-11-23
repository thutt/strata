#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The sizes of the envelopes and the zones that the address of each style
must lie in.  tests/envsamples.py uses them for the sample envelopes; they
are the numbers of the sources that tests/templates.py names for the
templates (docs/ENVELOPES.txt), written down again here so that a sample is
checked against the source and not against the generator.

zone_faults() takes the box of the sender block (or of the first block) and
the box of the recipient block, in millimetres from the top left corner of
the envelope, and the size of the envelope.  It returns the list of the
rules that the address breaks: an empty list means that it lies in its zone.
"""

import sys
sys.dont_write_bytecode = True

# name: (width, height in millimetres, the size as the description says it,
# the set of rules of the zone).  The rules: 'rm' Royal Mail, 'usps' the
# reading area of the United States Postal Service, 'din' the address field
# of DIN 5008 in the window of DIN 680, 'fr' the address zone of La Poste,
# 'au' the address zone of Australia Post, 'jp4' the left window of JIS S
# 5502 of the Yougata 4 and 'jp2' the two characters from the left edge of
# Japan Post for an envelope without a window.
SOURCES = {
    'envelope-us-ca-10': (241.3, 104.8, '4 1/8 x 9 1/2 in', 'usps'),
    'envelope-us-ca-9': (225.4, 98.4, '3 7/8 x 8 7/8 in', 'usps'),
    'envelope-us-ca-monarch': (190.5, 98.4, '3 7/8 x 7 1/2 in', 'usps'),
    'envelope-uk-c5': (229.0, 162.0, '162 x 229 mm', 'rm'),
    'envelope-uk-dl': (220.0, 110.0, '110 x 220 mm', 'rm'),
    'envelope-uk-c6': (162.0, 114.0, '114 x 162 mm', 'rm'),
    'envelope-de-din5008-dl': (220.0, 110.0, '110 x 220 mm', 'din'),
    'envelope-fr-dl': (220.0, 110.0, '110 x 220 mm', 'fr'),
    'envelope-eu-dl': (220.0, 110.0, '110 x 220 mm', 'rm'),
    'envelope-eu-c5': (229.0, 162.0, '162 x 229 mm', 'rm'),
    'envelope-eu-c65': (229.0, 114.0, '114 x 229 mm', 'rm'),
    'envelope-latam-dl': (220.0, 110.0, '110 x 220 mm', 'rm'),
    'envelope-au-dl': (220.0, 110.0, '110 x 220 mm', 'au'),
    'envelope-au-c5': (229.0, 162.0, '162 x 229 mm', 'au'),
    'envelope-jp-yougata-4': (235.0, 105.0, '235 x 105 mm', 'jp4'),
    'envelope-jp-yougata-2': (162.0, 114.0, '162 x 114 mm', 'jp2'),
}

# DIN 680, the window of a DL window envelope: 90 by 45 mm, 20 mm from the
# left edge and 15 mm from the bottom edge.  A4 folded in three is 210 by
# 105 mm in the 220 by 110 mm envelope; the address field of DIN 5008 form B
# is 85 mm wide and begins 20 mm from the left edge of the letter, so it is
# 25 mm from the left edge of the envelope when the letter lies in the
# middle.
DIN_ENV = (220.0, 110.0)
DIN_WINDOW = (20.0, DIN_ENV[1] - 15.0 - 45.0, 20.0 + 90.0, DIN_ENV[1] - 15.0)
DIN_FOLD = (210.0, 105.0)
DIN_FIELD_LEFT = 20.0 + (DIN_ENV[0] - DIN_FOLD[0]) / 2.0
MARGIN = 3.0


def zone_faults(rules, sb, rb, wmm, hmm):
    """The rules that the address breaks.  sb is the box of the first block
    of ink, rb the box of the last one: (x0, y0, x1, y1) in millimetres."""
    out = []
    if rules == 'din':
        wx0, wy0, wx1, wy1 = DIN_WINDOW
        # The sender is in the corner (the owner's decision of 2026-11-20);
        # the window shows the recipient only.
        if not (rb[0] >= DIN_FIELD_LEFT - 1.0 and
                rb[2] <= DIN_FIELD_LEFT + 85.5):
            out.append('the address field of DIN 5008, 85 mm wide from '
                       '%.0f mm' % DIN_FIELD_LEFT)
        if not (rb[0] >= wx0 + MARGIN and rb[1] >= wy0 + MARGIN and
                rb[2] <= wx1 - MARGIN and rb[3] <= wy1 - MARGIN):
            out.append('3 mm inside the window of DIN 680')
        if not sb[3] < wy0:
            out.append('the sender in the corner, outside the window')
        if not rb[1] >= wy0 + 5.0 + 17.7 - 3.0 - 2.0:
            out.append('the recipient in the address zone, 17.7 mm below '
                       'the top of the field')
    elif rules == 'fr':
        if not (rb[0] >= wmm / 2.0 and wmm - rb[2] >= 20.0 and
                hmm - rb[3] >= 20.0 and hmm - rb[1] <= 120.0):
            out.append('the address zone of La Poste: right half, 20 mm '
                       'from the right and bottom edge, 120 mm or less '
                       'above the bottom edge')
        if not (sb[3] <= 28.0 and sb[0] < wmm / 2.0 and
                (rb[0] - sb[2] >= 20.0 or rb[1] - sb[3] >= 20.0)):
            out.append('the sender at the top left, no lower than 28 mm, '
                       '20 mm or more from the address')
    elif rules == 'au':
        if not (hmm - rb[3] >= 15.0 and hmm - rb[1] <= 100.0 and
                rb[0] >= 10.0 and wmm - rb[2] >= 10.0 and
                (rb[1] >= 40.0 or rb[2] <= wmm - 90.0)):
            out.append('the address zone of Australia Post: 15 mm from '
                       'the bottom, 100 mm or less up, 10 mm from the '
                       'sides, out of the postage zone')
        if not ((sb[1] >= 40.0 or sb[2] <= wmm - 90.0) and
                (rb[0] - sb[2] >= 12.0 or rb[1] - sb[3] >= 12.0)):
            out.append('the return address out of the postage zone and '
                       '12 mm from the address')
    elif rules == 'jp4':
        if not (rb[0] >= 22.0 + MARGIN and rb[1] >= 12.0 + MARGIN and
                rb[2] <= 22.0 + 90.0 - MARGIN and
                rb[3] <= 12.0 + 45.0 - MARGIN):
            out.append('3 mm inside the left window of JIS S 5502')
    elif rules == 'jp2':
        if not (rb[0] >= 8.0 and rb[1] >= 3.0 and rb[3] <= hmm * 0.6):
            out.append('8 mm or more from the left edge, in the upper half')
    elif rules == 'usps':
        if not (rb[0] >= 12.7 and wmm - rb[2] >= 12.7 and
                hmm - rb[3] >= 15.9 and hmm - rb[1] <= 69.9):
            out.append('the reading area of the Postal Service: 12.7 mm '
                       'from the sides, 15.9 mm from the bottom, not above '
                       '69.9 mm from it')
    elif rules == 'rm':
        if not (rb[0] >= 15.0 and rb[1] >= 15.0 and
                wmm - rb[2] >= 15.0 and hmm - rb[3] >= 18.0 and
                rb[3] - 3.3 > 50.0 and
                (rb[1] >= 40.0 or rb[2] <= wmm - 75.0)):
            out.append('the address block of Royal Mail')
        if not (sb[3] <= 28.0 and (rb[0] - sb[2] >= 12.0 or
                                   rb[1] - sb[3] >= 12.0)):
            out.append('the return address of Royal Mail')
    return out
