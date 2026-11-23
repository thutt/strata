#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the sample business letters that Strata ships, into the
# folder that is its argument (the committed copies are in samples/letters/):
#
#     python3 -I tests/mkletters.py samples/letters
#
# Every letter is written twice, once on US Letter paper (NAME-us.doc) and
# once on A4 paper (NAME-a4.doc), and uses the named formats of the letter
# style of the templates letter-us and letter-a4 (tests/mktemplates.py):
# Sender, Date, Recipient, Salutation, Body, Closing, Signature, Enclosure,
# Copy and Postscript, with the records that the templates give them.  A
# letter may add named formats of its own for the parts that the style has no
# name for (a subject line, the rows of a table); a block of a letter never
# carries a format that has no name.  The names, the companies and the
# addresses are made up, and the dates are written as text, so that the files
# are the same bytes on every run.  The recipient of an A4 letter stands where
# the window of a DL window envelope shows it (DIN 5008, docs/ENVELOPES.txt),
# so that the sender has three lines and the recipient four lines.

import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mktemplates as mt

CRLF = mt.CRLF
EOL = mt.EOL
ATTR = b"\x12"
# The names of the formats of the letter style, as the templates define them.
STYLE = ["Sender", "Date", "Recipient", "Salutation", "Body", "Closing",
         "Signature", "Enclosure", "Copy", "Postscript"]
# The names of the formats that a letter may add to the style.
EXTRA = ["Subject", "Table head", "Table row"]
# The date that every letter carries, as text.
DATE = {"us": b"March 14, 2026", "a4": b"14 March 2026"}


def attr(mask):
    """A change of the attributes of the text that follows."""
    return ATTR + bytes([mask, 0])


def bold(text):
    """The text in bold."""
    return attr(1) + text + attr(0)


def manual_rule():
    """The function table_mask of the manual builder, which writes the
    borders of every table of the manuals: the header row has a single line
    at the top, the left, the right and between its columns and a double line
    at its bottom, the other rows have single lines at the left, the right,
    between the columns and at the bottom, and none at the top."""
    path = os.path.join(HERE, "..", "manual", "mkmanual.py")
    spec = importlib.util.spec_from_file_location("mkmanual", path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["mkmanual"] = mod
    spec.loader.exec_module(mod)
    return mod.table_mask


class LetterDoc(mt.Doc):
    """A letter: the document of the templates, with the blocks of several
    columns that a table needs and the typeface records."""

    def __init__(self, desc, paper, width, height, formats):
        mt.Doc.__init__(self, desc, paper, width, height,
                        (1000, 1000, 1000, 1000), formats)
        self.faces = []
        self.items = []
        self.header = []

    def face(self, number, name):
        """The name of the font NUMBER of the typeface list."""
        self.faces.append(b"DF%d" % number + b"N" + name + b"\x00")

    def text_font(self, number):
        """The font of the text of the whole letter (the record FTF)."""
        self.header.append(b"FTF%d" % number)

    def row(self, fmt, columns):
        """A block of columns; a column is a list of records and its text."""
        self.items.append((fmt, columns))

    def bytes(self):
        ml, mr, mt_, mb = self.margins
        out = [b"*R3", b"MD" + self.desc.encode("ascii") + b"\x00",
               b"MA" + b"Strata" + b"\x00", b"MR1000",
               b"PML%d" % ml, b"PMR%d" % mr, b"PMT%d" % mt_, b"PMB%d" % mb,
               b"PP\\1\\\x00", b"P1C33"]
        out += self.hf
        out += [b"FHA1", b"FTT5"] + self.header
        out += [b"DP%d,%d,%d" % self.paper]
        out += self.faces
        for name in sorted(self.formats):
            out.append(b"FN" + name.encode() + b"\x00")
            out += self.formats[name]
        out.append(b"FN\x00")
        out += [b"*B0", b"S"]
        for i, (fmt, columns) in enumerate(self.items):
            if i:
                out.append(b"B")
            out.append(b"BG" + fmt.encode() + b"\x00")
            for props, data in columns:
                out += props
                out.append(b"T" + data + b"\x00")
        out.append(b"*E")
        return CRLF.join(out) + CRLF


def new_letter(desc, kind, recipient_name):
    """An empty letter on the paper KIND, with the named formats of the style
    and of the letters, and the head of the pages after the first: the
    recipient, the date and the number of the page."""
    width, height, date_space, code = (
        (8500, 11000, 200, 0) if kind == "us" else (8268, 11693, 460, 8))
    cells = round(3.5 / ((width - 2000) / 1000.0) * 80)
    formats = mt.letter_formats(cells, date_space)
    formats["Subject"] = [b"BSB200"]
    formats["Table head"] = [b"BSB200"]
    formats["Table row"] = [b"BSB0"]
    d = LetterDoc(desc, code, width, height, formats)
    head = mt.hfblock(recipient_name + b" - " + DATE[kind] + b" - Page " +
                      mt.marker(22))
    d.hf = [b"PHB", head, b"P3HB0", b"P3FB0"]
    return d


def put(d, fmt, *lines):
    """A block of the format FMT with LINES; a line ends with the end of
    line."""
    d.row(fmt, [([], mt.text(*lines))])


def table(d, widths, rows):
    """A table of ROWS (lists of cells; the first row is the header) with
    the borders of the tables of the manuals."""
    rule = manual_rule()
    for r, cells in enumerate(rows):
        header = r == 0
        columns = []
        for i, cell in enumerate(cells):
            data = bold(cell) if header else cell
            props = []
            if i < len(cells) - 1:
                props.append(b"CW%d" % widths[i])
            props.append(b"CB%d,1" % rule(header, r == 0, i))
            if i == 0:
                props.append(b"CL1")
            props += [b"CR1", b"CFT1"]
            columns.append((props, data + EOL))
        d.row("Table head" if header else "Table row", columns)


# The parties of the letters: the sender block has three lines and the
# recipient block four, so that the recipient of an A4 letter stays in the
# window of a DL window envelope.  The sales firm writes most of the letters;
# a customer writes to a utility in one of them.
SIGNER = [b"", b"", b"", b"Robert Hale", b"Sales Manager"]
FIRMS = {
    "us": {
        "sender": [b"Fennimore Fabrication Co.", b"418 Quarry Road",
                   b"Hallsworth, ST 40218"],
        "rcpt": [b"Ms. Dana Whitcombe", b"Purchasing Manager",
                 b"Calder Valley Mills", b"92 Mill Lane, Ashby, ST 51377"],
        "name": b"Ms. Whitcombe",
        "head": b"Ms. Dana Whitcombe",
        "close": b"Sincerely,",
        "signer": SIGNER,
    },
    "a4": {
        "sender": [b"Fennimore Fabrication Ltd", b"418 Quarry Road",
                   b"Hallsworth HW4 2QR"],
        "rcpt": [b"Ms Dana Whitcombe", b"Purchasing Manager",
                 b"Calder Valley Mills", b"92 Mill Lane, Ashby AS3 9PL"],
        "name": b"Ms Whitcombe",
        "head": b"Ms Dana Whitcombe",
        "close": b"Yours sincerely,",
        "signer": SIGNER,
    },
}
# The utility that a customer writes to.  The name has an ampersand, which
# the Printer must write as \& for LaTeX (tests/letters.py looks for it in
# the text of the PDF).
UTILITY = b"Pathetic Gas & Deceptive"
CUSTOMERS = {
    "us": {
        "sender": [b"Helena Brandt", b"27 Linden Court",
                   b"Brookfield, ST 30415"],
        "rcpt": [b"Customer Accounts Department", UTILITY,
                 b"Post Office Box 4401", b"Ridgemont, ST 60912"],
        "name": b"Sir or Madam",
        "head": b"Pathetic Gas & Deceptive",
        "close": b"Sincerely,",
        "signer": [b"", b"", b"", b"Helena Brandt", b"Account holder"],
    },
    "a4": {
        "sender": [b"Helena Brandt", b"27 Linden Court",
                   b"Brookfield BF3 5TQ"],
        "rcpt": [b"Customer Accounts Department", UTILITY,
                 b"PO Box 4401", b"Ridgemont RM2 7AX"],
        "name": b"Sir or Madam",
        "head": b"Pathetic Gas & Deceptive",
        "close": b"Yours faithfully,",
        "signer": [b"", b"", b"", b"Helena Brandt", b"Account holder"],
    },
}


def frame(kind, desc, subject, faces=(), party=None):
    """The start of a letter: sender, date, recipient, subject, salutation.
    PARTY holds the lines of the two blocks; it defaults to the sales firm."""
    f = party[kind] if party else FIRMS[kind]
    d = new_letter(desc, kind, f["head"])
    d.party = f
    for number, name in faces:
        d.face(number, name)
    put(d, "Sender", *f["sender"])
    put(d, "Date", DATE[kind])
    put(d, "Recipient", *f["rcpt"])
    if subject:
        put(d, "Subject", bold(subject))
    put(d, "Salutation", b"Dear " + f["name"] + b",")
    return d


def finish(d, kind, enclosure=(), copy=(), postscript=None):
    """The end of a letter: closing, signature, enclosures, copies, P.S."""
    f = d.party
    put(d, "Closing", f["close"])
    put(d, "Signature", *f["signer"])
    if enclosure:
        put(d, "Enclosure", *enclosure)
    if copy:
        put(d, "Copy", *copy)
    if postscript:
        put(d, "Postscript", postscript)
    return d


def enquiry(kind):
    d = frame(kind, "Sample business letter: an enquiry about the price of "
              "steel brackets. " + PAPER[kind], b"Enquiry: steel mounting "
              b"brackets")
    put(d, "Body", b"We make shelving frames for schools and offices, and "
        b"we are looking for a new supplier of steel mounting brackets.  "
        b"Your catalogue lists the bracket that we need in three sizes, "
        b"and we should like to know what you would charge for a larger "
        b"order than the catalogue prices cover.")
    put(d, "Body", b"We expect to order 2,000 brackets in each of the three "
        b"sizes, four times a year, over the next two years.  The brackets "
        b"are to be zinc plated and delivered in boxes of fifty.  Please "
        b"tell us the price of each size at that quantity, what you "
        b"need as a deposit, and how long you take to deliver after an "
        b"order.")
    put(d, "Body", b"If you can send a quotation by the end of the month, we "
        b"will decide before our spring order, which we plan to place on "
        b"April 30.  A sample of each size would help us a great deal, and "
        b"we will pay for it.")
    return finish(d, kind)


def quotation(kind):
    d = frame(kind, "Sample business letter: a quotation with a table of "
              "prices. " + PAPER[kind], b"Quotation 2026-0314")
    put(d, "Body", b"Thank you for your enquiry of last week.  We are glad "
        b"to offer you the following prices for the steel mounting "
        b"brackets, zinc plated and packed in boxes of fifty, delivered to "
        b"your works.")
    if kind == "us":
        rows = [[b"Bracket", b"Boxes", b"Price per box", b"Amount"],
                [b"Small, 60 mm", b"40", b"$38.00", b"$1,520.00"],
                [b"Medium, 90 mm", b"40", b"$52.50", b"$2,100.00"],
                [b"Large, 120 mm", b"40", b"$71.00", b"$2,840.00"],
                [bold(b"Total for one order"), b"", b"", bold(b"$6,460.00")]]
    else:
        rows = [[b"Bracket", b"Boxes", b"Price per box", b"Amount"],
                [b"Small, 60 mm", b"40", b"GBP 31.00", b"GBP 1,240.00"],
                [b"Medium, 90 mm", b"40", b"GBP 42.50", b"GBP 1,700.00"],
                [b"Large, 120 mm", b"40", b"GBP 58.00", b"GBP 2,320.00"],
                [bold(b"Total for one order"), b"", b"",
                 bold(b"GBP 5,260.00")]]
    table(d, [26, 9, 20], rows)
    put(d, "Body", b"The prices hold for 30 days from the date of this "
        b"letter and for four orders in a year.  We ask for a deposit of "
        b"ten percent with the first order, and we deliver within three "
        b"weeks of every order.")
    put(d, "Body", b"Our terms of sale are enclosed.  Please call me if you "
        b"would like to talk about the sizes or about the delivery.")
    return finish(d, kind, enclosure=[b"encl: Terms of sale"])


def reply(kind):
    d = frame(kind, "Sample business letter: a reply to a complaint. " +
              PAPER[kind], b"Your complaint about delivery 2026-0288")
    put(d, "Body", b"Thank you for your letter of March 9.  I am sorry that "
        b"the brackets that we delivered last week were not the ones that "
        b"you ordered, and that your assembly line stood still for a "
        b"morning because of it.")
    put(d, "Body", b"We have looked into it.  Two pallets were loaded in the "
        b"wrong order at our warehouse, and the labels were not checked "
        b"against the order before they left.  We have changed the way "
        b"that we load: a second person now checks every pallet against "
        b"the order.")
    put(d, "Body", b"The correct brackets left our works this morning and "
        b"will reach you tomorrow.  Please keep the other pallets until "
        b"our driver collects them on Friday.  We will send you a credit "
        b"note for the full price of the wrong brackets and for the "
        b"freight, and I would like to take ten percent off your next "
        b"order as well.")
    put(d, "Body", b"I regret the trouble that this has caused you, and I "
        b"hope that you will give us the chance to put it right.")
    return finish(d, kind, copy=[b"cc: Customer service file"])


def confirmation(kind):
    d = frame(kind, "Sample business letter: the confirmation of an order. "
              + PAPER[kind], b"Order 2026-0301: confirmation")
    put(d, "Body", b"We confirm the order that you placed by telephone on "
        b"Monday, for 40 boxes of medium steel mounting brackets, zinc "
        b"plated.  The order number is 2026-0301.")
    put(d, "Body", b"We will load the brackets on Thursday, March 19, and "
        b"our carrier will deliver them to your works on Friday between "
        b"eight and noon.  Please have someone sign for them, and check "
        b"the number of boxes against the delivery note before the driver "
        b"leaves.")
    put(d, "Body", b"The invoice comes with the delivery.  It is due 30 "
        b"days after the date of the invoice.  If anything about the "
        b"delivery needs to change, please call me before Wednesday.")
    return finish(d, kind, postscript=b"P.S. Our works are closed on "
                  b"Monday, March 23, for stocktaking.")


def enclosures(kind):
    d = frame(kind, "Sample business letter: documents sent with a letter, "
              "enclosures and copies. " + PAPER[kind],
              b"Renewal of the warehouse lease")
    put(d, "Body", b"I am writing about the renewal of the lease of our "
        b"warehouse in Quarry Road, which ends on June 30.  We should like "
        b"to renew it for another five years on the same terms.")
    put(d, "Body", b"I enclose the signed renewal form, the certificate of "
        b"our insurance for the building, and a floor plan that shows "
        b"the changes that we made last year with your permission.  Please "
        b"sign the form where it is marked and return one copy to me in the "
        b"envelope that I have enclosed for the purpose.")
    put(d, "Body", b"My lawyer has a copy of everything that I have sent "
        b"you and will call your office if she has a question.")
    return finish(d, kind, enclosure=[b"encl: Renewal form (2 copies)",
                                      b"encl: Certificate of insurance",
                                      b"encl: Floor plan"],
                  copy=[b"cc: Ms. Irene Vale, Vale and Ross, Lawyers",
                        b"cc: Mr. Paul Reeve, Accounts"])


REVIEW = [
    b"Thank you for the time that you gave to our meeting on Tuesday.  "
    b"I write to set down what we agreed about the supply agreement between "
    b"our two firms, which has now run for a year, and about the changes "
    b"that we should like to make to it for the next year.",
    b"The brackets.  Over the year you took 7,400 boxes from us in "
    b"all, which is about a fifth more than the 6,000 that we planned "
    b"for.  The medium size sold best and the small size least.  We agreed "
    b"that we shall plan for 8,000 boxes in the next year, split into four "
    b"orders of 2,000, and that we shall hold the prices of the year just "
    b"ended until the end of September.",
    b"Delivery.  Of 31 deliveries, 29 reached you on the day that we "
    b"promised.  One was a day late because of the snow in February, and "
    b"one was the wrong delivery that you wrote to us about in March.  "
    b"We changed the way that we load because of it, and you have seen the "
    b"new check at our warehouse.  We agreed that we shall tell you by "
    b"telephone on the day before a delivery that is going to be late.",
    b"Payment.  You pay our invoices in 30 days, and in the whole year "
    b"only three were paid after 45 days.  We should like to offer you a "
    b"discount of two percent on invoices that you pay within ten days, "
    b"and you said that you would think about it and let us know after "
    b"your next finance meeting.",
    b"Quality.  You returned 112 boxes in the year, mostly because of "
    b"scratches on the plating.  Our plating shop has changed the racks "
    b"that hold the brackets while they are plated, and the returns of "
    b"the last three months are down to nine boxes.  We should like you "
    b"to tell us at once if you see the scratches again.",
    b"New sizes.  You asked for a bracket of 150 mm.  We can make one "
    b"from the same steel, with the same plating, and we can show you "
    b"samples in April.  The price depends on the order, and we shall send "
    b"you a quotation when you know how many you need.",
    b"The terms.  The agreement runs on for a year at the end of June unless "
    b"one of us gives notice in writing by the end of April.  We do not "
    b"intend to give notice, and we hope that you do not either.",
    b"The next meeting.  We should like to meet again in September, at your "
    b"works this time, so that our people can see the line where the "
    b"brackets are used.  Please tell me which week suits you best.",
    b"Thank you again for the year.  We value the business that we do with "
    b"you, and we look forward to the next one.",
]


def review(kind):
    d = frame(kind, "Sample business letter of two pages: the second page "
              "has a head with the recipient, the date and the page "
              "number. " + PAPER[kind], b"Review of the supply agreement")
    for p in REVIEW:
        put(d, "Body", p)
    return finish(d, kind, copy=[b"cc: Mr. Paul Reeve, Accounts"])


def notice(kind):
    d = frame(kind, "Sample business letter set in a sans serif face: a "
              "notice of new opening hours. " + PAPER[kind],
              b"New opening hours from April 1", faces=[(2, b"Text (sans)")])
    d.text_font(2)
    put(d, "Body", b"From April 1 our warehouse will be open for collections "
        b"from seven in the morning until five in the afternoon, Monday to "
        b"Friday.  On Saturdays it will be open from eight until noon.")
    put(d, "Body", b"The office keeps its present hours.  Orders by "
        b"telephone are taken between eight and half past four, and orders "
        b"by e-mail to orders@example.com at any time.")
    put(d, "Body", b"This letter is set in a sans serif face.  The name of "
        b"the font that holds the text of the letter has the word sans in "
        b"it, and that is all that the Printer needs to print the whole "
        b"letter in that face; the formats of the letter are the same as "
        b"in the other letters.")
    return finish(d, kind)


def utility(kind):
    d = frame(kind, "Sample business letter: a customer's complaint to a "
              "utility about a bill. " + PAPER[kind],
              b"Disputed bill: account 40-1183-7752, meter GM-2209-4417",
              party=CUSTOMERS)
    if kind == "us":
        figures = (b"$1,284.60", b"14,800 units", b"3,215 units",
                   b"about 400 units a month", b"$164.10")
    else:
        figures = (b"GBP 1,284.60", b"14,800 kWh", b"3,215 kWh",
                   b"about 400 kWh a month", b"GBP 164.10")
    bill, est, real, usual, paid = figures
    put(d, "Body", b"I am writing to dispute the bill that you dated "
        b"February 27 for the gas that you supplied to my home between "
        b"December 1 and February 26.  The bill is for " + bill + b" and "
        b"rests on an estimated meter reading of " + est + b".  I read the "
        b"meter myself on March 2 and photographed it: it shows " + real +
        b" in all since the meter was fitted.")
    put(d, "Body", b"My household uses " + usual + b" in winter, as the "
        b"bills of the last year, which I enclose, show.  I have paid the part of the bill that I do not dispute, " +
        paid + b", and I will pay the rest as soon as I have a corrected "
        b"bill.")
    put(d, "Body", b"You told me by letter that an engineer would read the "
        b"meter on March 4 between eight and noon.  I stayed at home; nobody "
        b"came, and nobody telephoned.  I called your "
        b"office on March 5 and again on March 9; each time I was told "
        b"that a corrected bill would be sent, and none has come.")
    put(d, "Body", b"I ask you to send me a corrected bill based on the "
        b"reading that I enclose, to take back the late fee that you added "
        b"on March 6, to stop any collection of the disputed sum while this "
        b"is settled, and to confirm all this to me in writing within 14 "
        b"days of the date of this letter.  If I have no answer by then I "
        b"shall take the matter to the regulator of the gas supply.  You can "
        b"reach me at helena.brandt@example.com.")
    return finish(d, kind, enclosure=[b"encl: The bill dated February 27",
                                      b"encl: Photograph of the meter",
                                      b"encl: The bills of the last year"],
                  copy=[b"cc: Consumer advice office"])


PAPER = {"us": "US Letter paper.", "a4": "A4 paper."}
# The letters: the name of the file, and the function that makes the letter.
LETTERS = {
    "enquiry": enquiry,
    "quotation": quotation,
    "complaint-reply": reply,
    "complaint-to-utility": utility,
    "order-confirmation": confirmation,
    "enclosures": enclosures,
    "two-pages": review,
    "sans-serif": notice,
}


def names():
    """The names of the files that are written, without the folder."""
    return sorted("%s-%s.doc" % (n, k) for n in LETTERS for k in PAPER)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: mkletters.py FOLDER\n")
        return 2
    os.makedirs(argv[1], exist_ok=True)
    for name, make in LETTERS.items():
        for kind in PAPER:
            path = os.path.join(argv[1], "%s-%s.doc" % (name, kind))
            with open(path, "wb") as f:
                f.write(make(kind).bytes())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
