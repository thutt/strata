# Check every line of an equation data file with the wrapper that Strata uses.
import subprocess, sys, os, tempfile
from concurrent.futures import ThreadPoolExecutor

def entries(path):
    out = []
    for ln in open(path):
        ln = ln.rstrip("\n")
        if ln.startswith("#") or not ln.strip():
            continue
        tex, _, meaning = ln.partition(" ;; ")
        out.append((tex, meaning))
    return out

def run(tex):
    d = tempfile.mkdtemp(prefix="eqchk")
    src = ("\\documentclass[preview,border=2pt]{standalone}\n"
           "\\usepackage{amsmath,amssymb}\n\\begin{document}\n"
           "$\\displaystyle " + tex + "$\n\\end{document}\n")
    open(d + "/eq.tex", "w").write(src)
    env = dict(os.environ, openin_any="p", openout_any="p")
    r = subprocess.run(["latex", "-no-shell-escape", "-interaction=nonstopmode",
        "-halt-on-error", "eq.tex"], cwd=d, env=env, capture_output=True,
        text=True)
    msg = ""
    if r.returncode != 0:
        for l in open(d + "/eq.log", errors="replace"):
            if l.startswith("!"):
                msg = l.strip(); break
    subprocess.run(["rm", "-rf", d])
    return r.returncode, msg

es = entries(sys.argv[1])
with ThreadPoolExecutor(8) as ex:
    res = list(ex.map(lambda e: run(e[0]), es))
bad = 0
for (tex, m), (rc, msg) in zip(es, res):
    if rc != 0:
        bad += 1
        print("FAIL", tex, "|", msg)
print(len(es), "entries,", bad, "fail")
