#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the sample envelopes that Strata ships, into the folder
# that is its argument (the committed copies are in samples/envelopes/):
#
#     python3 -I -B tests/mksamples.py samples/envelopes
#
# There is one sample for each envelope template of tests/mktemplates.py.  A
# sample is the document that New makes from its template, with the address
# of a made-up person or company, written as it is written in the country of
# the envelope.  The paper, the places of the blocks and the named formats
# are those of the template, so a sample prints where its template does
# (tests/envsamples.py checks it again).  Every name, street and postal code
# here is made up: the names are those of the examples of the country and
# the companies are named Example, Sample or Muster.  The output has no date
# and no other thing that changes from one run to the next.

import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mktemplates

PREFIX = "sample-"

# template name: (the size as the description says it, what the sample
# shows, the lines of the sender, the lines of the recipient).  The text is
# ASCII, and no line is longer than the longest line of the template.
SAMPLES = {
    "envelope-us-ca-10": (
        "Number 10 envelope (4 1/8 x 9 1/2 in)",
        "a company in the United States, with an attention line and a "
        "ZIP+4 code",
        ["Example Widgets, Inc.", "100 Market Street, Suite 4",
         "Springfield, ST 12345"],
        ["Ms. Jane Q. Sample", "Director of Purchasing",
         "Sample Industries, Inc.", "200 Oak Avenue, Floor 3",
         "SHELBYVILLE ST 54321-0001"]),
    "envelope-us-ca-9": (
        "Number 9 envelope (3 7/8 x 8 7/8 in)",
        "a company in Canada, with the postal code of Canada",
        ["Example Widgets Ltd.", "100 Market Street",
         "Springfield ON K1A 0B1"],
        ["Ms. Jane Sample", "Sample Corporation", "200 Maple Avenue",
         "TORONTO ON M5V 2T6"]),
    "envelope-us-ca-monarch": (
        "Monarch envelope (3 7/8 x 7 1/2 in)",
        "a person in the United States, a personal letter",
        ["John and Mary Example", "42 Elm Street",
         "Springfield, ST 12345"],
        ["Mr. Robert Sample", "17 Pine Lane", "Apartment 5B",
         "SHELBYVILLE ST 54321"]),
    "envelope-uk-c5": (
        "C5 envelope (162 x 229 mm)",
        "a company in the United Kingdom, postcode on the last line",
        ["Example Widgets Ltd", "10 High Street", "London"],
        ["Ms Jane Sample", "Sample and Sons Ltd", "20 Church Road",
         "BRISTOL", "BS1 1AA"]),
    "envelope-uk-dl": (
        "DL envelope (110 x 220 mm)",
        "a person in the United Kingdom, postcode on the last line",
        ["J Example", "10 High Street", "London"],
        ["Mr John Sample", "20 Church Road", "BRISTOL", "BS1 1AA"]),
    "envelope-uk-c6": (
        "C6 envelope (114 x 162 mm)",
        "a doctor in the United Kingdom, for a card or a short letter",
        ["Strata Surgery", "3 Station Road", "London"],
        ["Dr Jane Sample", "5 The Green", "OXFORD", "OX1 1AA"]),
    "envelope-de-din5008-dl": (
        "DL window envelope (110 x 220 mm)",
        "a person in Germany: return line in the window, then form of "
        "address, name, street and postal code before the town",
        ["Muster GmbH - Allee 1 - 12345 Musterstadt"],
        ["Herrn", "Max Mustermann", "Musterstrasse 12",
         "50667 Beispielstadt"]),
    "envelope-fr-dl": (
        "DL envelope (110 x 220 mm)",
        "a person in France, in capitals, the postal code before the town",
        ["Exemple SARL", "1 rue Principale", "75001 PARIS"],
        ["M. JEAN EXEMPLE", "ENTREPRISE EXEMPLE SA", "12 AVENUE DU TEST",
         "33000 BORDEAUX"]),
    "envelope-eu-dl": (
        "DL envelope (110 x 220 mm)",
        "a person in the Netherlands, postal code of four digits and two "
        "letters",
        ["Voorbeeld B.V.", "Voorbeeldstraat 1", "1000 AA AMSTERDAM"],
        ["De heer J. Voorbeeld", "Steekproefweg 12", "3500 BB UTRECHT"]),
    "envelope-eu-c5": (
        "C5 envelope (162 x 229 mm)",
        "a person in Italy, the province after the town",
        ["Esempio S.r.l.", "Via Roma 1", "00100 ROMA RM"],
        ["Gentile Signora", "Maria Esempio", "Via Garibaldi 10",
         "20100 MILANO MI"]),
    "envelope-eu-c65": (
        "C6/5 envelope (114 x 229 mm)",
        "a person in Switzerland, the postal code before the town",
        ["Muster AG", "Musterweg 1", "8000 Zuerich"],
        ["Herr", "Hans Muster", "Beispielstrasse 5", "3000 Bern"]),
    "envelope-latam-dl": (
        "DL envelope (110 x 220 mm)",
        "a person in Mexico, the colony after the street",
        ["Empresa Ejemplo S.A.", "Calle Principal 1", "Ciudad Ejemplo"],
        ["Sra. Ana Ejemplo", "Av. Central 123, Col. Centro",
         "06000 Ciudad de Mexico"]),
    "envelope-au-dl": (
        "DL envelope (110 x 220 mm)",
        "a person in Australia, town, state and postcode on one line",
        ["Example Pty Ltd", "10 High Street", "SYDNEY NSW 2000"],
        ["Ms Jane Sample", "20 Church Road", "MELBOURNE VIC 3000"]),
    "envelope-au-c5": (
        "C5 envelope (162 x 229 mm)",
        "a company in Australia, with a post office box",
        ["Example Pty Ltd", "10 High Street", "SYDNEY NSW 2000"],
        ["Mr John Sample", "Sample Pty Ltd", "PO Box 123",
         "BRISBANE QLD 4000"]),
    "envelope-jp-yougata-4": (
        "Yougata 4 envelope (235 x 105 mm)",
        "a company in Japan, written across: postal code first, the "
        "sender on the back",
        [],
        ["100-0001", "Tokyo-to Chiyoda-ku Chiyoda 1-1", "Sample Building 5F",
         "Example Co., Ltd.", "Hanako Suzuki sama"]),
    "envelope-jp-yougata-2": (
        "Yougata 2 envelope (162 x 114 mm)",
        "a person in Japan, written across: postal code first, the "
        "sender on the back",
        [],
        ["530-0001", "Osaka-fu Osaka-shi Kita-ku 2-3-4", "Example Heights 201",
         "Ichiro Tanaka sama"]),
}


def sample_name(template):
    """The name of the file of the sample of a template, without .doc."""
    return PREFIX + template


def sample_description(template):
    """The description of a sample (the record MD, at most 250 characters):
    the paper and what the address shows."""
    size, shows = SAMPLES[template][:2]

    return "Sample: %s, addressed to %s." % (size, shows)


def sample(template):
    """The document of the sample of a template."""
    sender, rcpt = SAMPLES[template][2:]

    return mktemplates.envelope(template, sample_description(template),
                                sender, rcpt)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: mksamples.py FOLDER\n")
        return 2
    os.makedirs(argv[1], exist_ok=True)
    for template in mktemplates.ENVELOPES:
        with open(os.path.join(argv[1], sample_name(template) + ".doc"),
                  "wb") as f:
            f.write(sample(template).bytes())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
