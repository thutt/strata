% The keycap macro of the Strata print path (src/latex.c, preamble_b).
\newbox\strbox
\newdimen\strem
\newcommand{\strkey}[5]{\begingroup \strem=1em
 \setbox\strbox\hbox{\textcolor[gray]{#1}{#4\upshape #5}}%
 \scalebox{#3}{\tikz[baseline=0pt]{%
  \path[draw=black,line width=0.4pt,
   rounded corners=\the\dimexpr0.2\strem\relax,
   fill={rgb,1:red,#2;green,#2;blue,#2}]
   (0,{-\the\dimexpr0.333\strem\relax}) rectangle
   ({\the\dimexpr\wd\strbox+0.62\strem\relax},
   {\the\dimexpr1.0\strem\relax});
  \node[anchor=base west,inner sep=0pt]
   at ({\the\dimexpr0.31\strem\relax},0) {\box\strbox};}}%
 \endgroup}
% More macros of the print path that draw (src/latex.c, preamble_b).
\newcommand{\strlong}[1]{\fcolorbox{black!40}{black!5}{\ttfamily
 \footnotesize #1}}
\newcommand{\strpic}[2]{\adjustbox{max width=\linewidth,
 max height=0.85\textheight}{\includegraphics[#1]{#2}}}
\newcommand{\strsmile}[2]{{\strem=1em
 \tikz[baseline={-0.3\strem},line width=0.3pt]{%
  \draw[fill=#1] (0,0) circle (0.42\strem);
  \fill[#2] (-0.15\strem,0.1\strem) circle (0.05\strem)
   (0.15\strem,0.1\strem) circle (0.05\strem);
  \draw[#2] (-0.2\strem,-0.08\strem) .. controls
   (-0.1\strem,-0.25\strem) and (0.1\strem,-0.25\strem) ..
   (0.2\strem,-0.08\strem);}}}
