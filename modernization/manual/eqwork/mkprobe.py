import sys
sys.path.insert(0, "..")
out = [".doc Equation probe", ".h1 Probe"]
n = 0
for ln in open(sys.argv[1]):
    ln = ln.rstrip("\n")
    if ln.startswith("#") or not ln.strip():
        continue
    tex = ln.partition(" ;; ")[0]
    n += 1
    out.append("")
    out.append("N%d {eq:%s}" % (n, tex.replace("{", "{{").replace("}", "}}")))
open(sys.argv[2], "w").write("\n".join(out) + "\n")
