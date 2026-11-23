/*
 * Strata is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Strata is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Strata.  If not, see <https://www.gnu.org/licenses/>.
 */
/* Copyright (c) 2026 Logic Magicians Software */

/*
 * logodata.c - the definitions of the large logo and of the marks of Logic
 * Magicians Software as C strings.  This file is made by tests/logodata.py
 * --write from logo/stratalogo.def and logo/lmslogo.def: change those files
 * and make this one again.
 */
#include "logo.h"


const char *const logo_def_lines[] = {
    "% The large Strata logo, as the command \\stratalogo.",
    "%",
    "% The logo is a large S whose top bar runs out to the right, with "
    "the words",
    "% Technical Document on that bar.  Under the bar are the word Proc"
    "essor and",
    "% the letters RATA, in which the crossbar of the first T is joined"
    " to that of",
    "% the second one and covers the letters RA and the final A.",
    "%",
    "% The command takes the scale as an option: \\stratalogo[2] is twi"
    "ce as big.",
    "% Without it the scale is 1 and the logo is about 4.4 cm wide.",
    "%",
    "% The S is a drawing made with TikZ, which every file of the Print"
    "er loads:",
    "% one filled outline, so that no font file is needed and the edges"
    " are clean",
    "% at every size.  The fonts of the other letters are Palatino and "
    "Helvetica",
    "% of the psnfss package, which a standard TeX Live installation ha"
    "s.",
    "%",
    "\\makeatletter",
    "\\newlength{\\strbig@u}",
    "\\newlength{\\strbig@stw}",
    "\\newlength{\\strbig@bart}",
    "\\newlength{\\strbig@capk}",
    "\\newlength{\\strbig@tga}",
    "\\newlength{\\strbig@tgb}",
    "\\newlength{\\strbig@tgc}",
    "\\newlength{\\strbig@barover}",
    "\\newlength{\\strbig@barend}",
    "\\newlength{\\strbig@barw}",
    "\\newsavebox{\\strbig@bS}",
    "\\newsavebox{\\strbig@bRA}",
    "\\newsavebox{\\strbig@bA}",
    "\\newsavebox{\\strbig@bstem}",
    "\\newsavebox{\\strbig@bword}",
    "\\newsavebox{\\strbig@bbar}",
    "\\newsavebox{\\strbig@bcol}",
    "\\newsavebox{\\strbig@ball}",
    "\\newsavebox{\\strbig@btitle}",
    "% The S: a stroke of 6 pt that follows a center line with bends of"
    " 9 pt, a",
    "% body of 46 pt by 43.5 pt, and a top bar that runs out to 126 pt."
    "  The path",
    "% is the outline of the stroke: the left side from the end of the "
    "top bar",
    "% around to the bottom bar, the flat end of the bottom bar, and th"
    "e other",
    "% side back.  All numbers are in the unit \\strbig@u, so the S fol"
    "lows the",
    "% scale of the logo.  The picture is as wide as the top bar, and t"
    "he box",
    "% that holds it is as wide as the body only (see \\strbig@logo).",
    "\\newcommand{\\strbig@drawS}{%",
    "  \\begin{tikzpicture}[baseline=0pt,x=\\strbig@u,y=\\strbig@u]",
    "    \\fill (126,37.5) -- (12,37.5)",
    "      arc[start angle=90,end angle=180,radius=6] -- (6,30.75)",
    "      arc[start angle=180,end angle=270,radius=6] -- (34,24.75)",
    "      arc[start angle=90,end angle=0,radius=12] -- (46,12)",
    "      arc[start angle=0,end angle=-90,radius=12] -- (0,0) -- (0,6)",
    "      -- (34,6)",
    "      arc[start angle=-90,end angle=0,radius=6] -- (40,12.75)",
    "      arc[start angle=0,end angle=90,radius=6] -- (12,18.75)",
    "      arc[start angle=270,end angle=180,radius=12] -- (0,31.5)",
    "      arc[start angle=180,end angle=90,radius=12] -- (126,43.5) --"
    " cycle;",
    "  \\end{tikzpicture}}",
    "\\newcommand{\\stratalogo}{\\@ifnextchar[{\\strbig@logo}{\\strbig@"
    "logo[1]}}",
    "\\def\\strbig@logo[#1]{%",
    "  \\leavevmode",
    "  \\begingroup",
    "  \\setlength{\\strbig@u}{#1pt}%",
    "  \\fontencoding{T1}\\fontfamily{phv}\\fontseries{b}\\fontshape{n}"
    "\\selectfont",
    "  % The stems of the two Ts are rules with the stroke of the lette"
    "r I of",
    "  % the font of the small letters, as in the text \\strata (latex."
    "c): the",
    "  % stroke is 0.149 of the size of the letter (21 u), the bar is 0"
    ".9 of it,",
    "  % and a capital is 0.726 of its size high in the drawing of the "
    "font.",
    "  % The stems end half a bar below the top, hidden in the bar.",
    "  \\setlength{\\strbig@stw}{\\dimexpr0.149\\dimexpr21\\strbig@u\\r"
    "elax\\relax}%",
    "  \\setlength{\\strbig@bart}{\\dimexpr0.9\\strbig@stw\\relax}%",
    "  \\setlength{\\strbig@capk}{\\dimexpr0.726\\dimexpr34\\strbig@u\\"
    "relax\\relax}%",
    "  \\setlength{\\strbig@barover}{7\\strbig@u}%",
    "  \\setlength{\\strbig@barend}{3\\strbig@u}%",
    "  \\setlength{\\strbig@tga}{2.1\\strbig@u}%",
    "  \\setlength{\\strbig@tgb}{2.2\\strbig@u}%",
    "  \\setlength{\\strbig@tgc}{1.7\\strbig@u}%",
    "  \\sbox{\\strbig@bS}{\\rlap{\\strbig@drawS}\\rule[0pt]{0pt}{43.5\\"
    "strbig@u}%",
    "    \\hspace{46\\strbig@u}}%",
    "  \\sbox{\\strbig@bRA}{\\fontsize{\\the\\dimexpr21\\strbig@u\\rela"
    "x}{0pt}%",
    "    \\selectfont RA}%",
    "  \\sbox{\\strbig@bA}{\\fontsize{\\the\\dimexpr21\\strbig@u\\relax"
    "}{0pt}\\selectfont A}%",
    "  \\setlength{\\strbig@barw}{\\dimexpr\\strbig@barover+\\strbig@st"
    "w%",
    "    +\\strbig@tga+\\wd\\strbig@bRA+\\strbig@tgb+\\strbig@stw%",
    "    +\\strbig@tgc+\\wd\\strbig@bA+\\strbig@barend\\relax}%",
    "  \\sbox{\\strbig@bstem}{\\rule[0pt]{\\strbig@stw}%",
    "    {\\dimexpr\\strbig@capk-\\strbig@bart/2\\relax}}%",
    "  \\sbox{\\strbig@bword}{\\hbox to \\strbig@barw{\\hss",
    "    \\fontfamily{ppl}\\fontseries{m}\\fontshape{sc}%",
    "    \\fontsize{\\the\\dimexpr12\\strbig@u\\relax}{0pt}%",
    "    \\selectfont processor\\hss}}%",
    "  \\sbox{\\strbig@bbar}{\\hbox to \\strbig@barw{%",
    "    \\rlap{\\raisebox{\\dimexpr\\strbig@capk-\\strbig@bart\\relax}"
    "%",
    "      {\\rule{\\strbig@barw}{\\strbig@bart}}}%",
    "    \\kern\\strbig@barover\\usebox{\\strbig@bstem}\\kern\\strbig@t"
    "ga",
    "    \\usebox{\\strbig@bRA}\\kern\\strbig@tgb\\usebox{\\strbig@bste"
    "m}%",
    "    \\kern\\strbig@tgc\\usebox{\\strbig@bA}\\hss}}%",
    "  \\sbox{\\strbig@bcol}{\\vbox{\\box\\strbig@bword\\kern\\strbig@u"
    "\\box\\strbig@bbar}}%",
    "  \\sbox{\\strbig@ball}{\\hbox{\\usebox{\\strbig@bS}\\kern5\\strbi"
    "g@u",
    "    \\usebox{\\strbig@bcol}}}%",
    "  \\sbox{\\strbig@btitle}{\\hbox to \\wd\\strbig@ball{\\hss",
    "    \\fontfamily{ppl}\\fontseries{m}%",
    "    \\fontsize{\\the\\dimexpr11\\strbig@u\\relax}{0pt}\\selectfont",
    "    Technical Document}}%",
    "  \\mbox{\\rlap{\\raisebox{\\dimexpr\\ht\\strbig@ball+2.5\\strbig@"
    "u\\relax}%",
    "    {\\usebox{\\strbig@btitle}}}\\usebox{\\strbig@ball}}%",
    "  \\endgroup}",
    "\\makeatother",
    NULL};

const char *const lms_def_lines[] = {
    "% The Logic Magicians Software marks, as the commands \\lms and \\"
    "lmslogo.",
    "%",
    "% \\lms prints the name in italics: Logic Magicians Software.  It "
    "is spaced",
    "% correctly before punctuation and cannot break across lines.",
    "%",
    "% \\lmslogo draws an AND gate with two inputs on the left, the let"
    "ters LMS",
    "% inside and an output on the right that ends in a five-point star"
    ".  The",
    "% command takes the scale as an option: \\lmslogo[2] is twice as b"
    "ig.  Without",
    "% it the scale is 1 and the mark is about 2.5 cm wide at the size "
    "of the",
    "% text of a document (11 pt): the drawing is made in units of the "
    "font, so",
    "% that it follows the size of the text around it.",
    "%",
    "% The mark is black.  It is a drawing made with TikZ, which every "
    "file of the",
    "% Printer loads, so that no font file is needed and the edges are "
    "clean at",
    "% every size.  Both commands are robust: they can be used in runni"
    "ng text,",
    "% in headlines, in the cells of a table, and in headers and footer"
    "s.",
    "%",
    "\\usetikzlibrary{shapes.geometric}",
    "\\makeatletter",
    "\\DeclareRobustCommand{\\lms}{\\mbox{\\textit{Logic Magicians Soft"
    "ware}}\\xspace}",
    "\\DeclareRobustCommand{\\lmslogo}{%",
    "  \\@ifnextchar[{\\lmsl@go}{\\lmsl@go[1]}}",
    "\\def\\lmsl@go[#1]{%",
    "  \\leavevmode",
    "  \\begingroup",
    "  \\begin{tikzpicture}[baseline=-0.9ex,x=2.6em,y=2.6em,scale=#1,",
    "    every node/.style={transform shape},line width=0.05em*#1]",
    "    % The body of the gate: flat on the left, round on the right.",
    "    \\draw (0,0.45) -- (0.9,0.45) arc[start angle=90,end angle=-90"
    ",radius=0.45]",
    "          -- (0,-0.45) -- cycle;",
    "    % The two inputs and the output.",
    "    \\draw (-0.35,0.22) -- (0,0.22);",
    "    \\draw (-0.35,-0.22) -- (0,-0.22);",
    "    \\draw (1.35,0) -- (1.75,0);",
    "    % The star at the end of the output.",
    "    \\node[star,star points=5,star point ratio=2.2,fill,inner sep="
    "0pt,",
    "          minimum size=0.73em] at (1.97,0) {};",
    "    % The initials inside the gate.",
    "    \\node[font=\\sffamily\\bfseries\\footnotesize] at (0.62,0) {L"
    "MS};",
    "  \\end{tikzpicture}%",
    "  \\endgroup\\xspace}",
    "\\makeatother",
    NULL};
