#!/usr/bin/env python3
"""What could be less visible than it is declared.

    scripts/visibility.py                  # every crate under compiler/
    scripts/visibility.py kf-core kf-parse # only what these crates declare
    scripts/visibility.py --all kf-core    # list what is fine too
    scripts/visibility.py --apply kf-core  # rewrite the declarations it reports

For each function, top-level `val`, type and method a crate declares, this
finds every file that names it and says how far away the farthest one is:

    pub       named from another crate
    internal  named from another module of its own crate
    private   named only inside its own module (the directory)
    unused    named nowhere but in its own declaration

and reports the declarations written wider than that.

It reads text, not types, so it is a list of candidates, not of facts:

  * A top-level name counts as used by a file that names it and can see it:
    one in its module, or one importing it by name or by wildcard.
  * A method counts as used wherever `.name(` or `Type.name(` is written.
    A method name several types share cannot be attributed, so those are
    listed apart and not judged.
  * A type stays as visible as the widest declaration whose signature
    mentions it, since a caller holds one without ever writing its name.
  * Comments are ignored, and so is a string's text apart from what it
    interpolates, so a name used only inside a test fixture's embedded
    source does not count.

`--apply` narrows what the report lists as private or internal, in the named
crates only, and leaves alone what is named nowhere: that is a question of
deleting it, which is not this script's to answer.

The compiler has the last word: after `--apply`, run `komp lint` on every
crate and widen again whatever it objects to.
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKSPACE = os.path.join(ROOT, "compiler")
RANK = {"unused": 0, "private": 1, "internal": 2, "pub": 3}
WORD = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")

TOP = re.compile(
    r'^(pub |internal )?(?:mutating )?(?:view )?(?:extern "C" )?'
    r"(fun|struct|enum|trait|val) (?:<[^>]*> )?(?:[A-Za-z_<>&, \[\]?]+\.)?([A-Za-z_][A-Za-z_0-9]*)"
)
IMPL = re.compile(r"^impl(?:<[^>]*>)? ([A-Za-z_][A-Za-z_0-9]*)(?:<[^>]*>)? \{")
TRAIT_IMPL = re.compile(r"^impl(?:<[^>]*>)? [A-Za-z_][\w<>, ]* for ")
METHOD = re.compile(r"^    (pub |internal )?((?:static |mutating )*)fun ([a-z_][a-z_0-9]*)\(")


def interpolated(text):
    """What a string literal runs: the code inside each `${...}`."""
    out, i = [], 0
    while True:
        i = text.find("${", i)
        if i < 0:
            return " ".join(out)
        depth, j = 1, i + 2
        while j < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[j], 0)
            j += 1
        out.append(text[i + 2:j - 1])
        i = j


def strip(src):
    """The code alone: no comments, and of a string only what it interpolates."""
    keep = lambda m: '"" ' + interpolated(m.group(0)) + " "
    src = re.sub(r'""".*?"""', '""', src, flags=re.S)
    src = re.sub(r'"(?:\\.|[^"\\\n])*"', keep, src)
    return re.sub(r"//[^\n]*", "", src)


class File:
    def __init__(self, path, crate, module):
        self.path, self.crate, self.module = path, crate, module
        self.is_test = path.endswith("_test.kf")
        raw = open(path).read()
        self.lines = raw.split("\n")
        self.imports, self.wildcards = set(), set()
        for line in self.lines:
            if line and not line.startswith(("import ", "export ", "//")):
                break
            m = re.match(r"import ([\w.]+?)(\.\*)?(?: as \w+)?$", line)
            if m and m.group(2):
                self.wildcards.add(m.group(1))
            elif m:
                self.imports.add(m.group(1))
            m = re.match(r"export (.+) from ([\w.]+)$", line)
            if m and m.group(1).strip() == "*":
                self.wildcards.add(m.group(2))
            elif m:
                for name in m.group(1).split(","):
                    self.imports.add(m.group(2) + "." + name.split(" as ")[0].strip())
        code = strip(raw)
        self.word_counts = defaultdict(int)
        for word in WORD.findall(code):
            self.word_counts[word] += 1
        self.words = set(self.word_counts)
        self.method_calls = set(re.findall(r"\.([a-z_][a-z_0-9]*)\(", code))
        self.static_calls = set(re.findall(r"\b([A-Z]\w*)\.([a-z_][a-z_0-9]*)\(", code))

    def sees(self, module, name):
        return self.module == module or module in self.wildcards or f"{module}.{name}" in self.imports


class Decl:
    def __init__(self, file, line, kind, name, written, owner=None, static=False):
        self.file, self.line, self.kind, self.name = file, line, kind, name
        self.written, self.owner, self.static = written, owner, static
        self.signature = ""
        self.needed = "unused"
        self.only_tests = False

    def label(self):
        return f"{self.owner}.{self.name}" if self.owner else self.name

    def where(self):
        return f"{os.path.relpath(self.file.path, ROOT)}:{self.line}"


def crate_name(crate_dir):
    for line in open(os.path.join(crate_dir, "kf.toml")):
        m = re.match(r'name = "([\w-]+)"', line)
        if m:
            return m.group(1)
    return os.path.basename(crate_dir).replace("-", "_")


def load():
    files = []
    for entry in sorted(os.listdir(WORKSPACE)):
        crate_dir = os.path.join(WORKSPACE, entry)
        src = os.path.join(crate_dir, "src")
        if not os.path.isfile(os.path.join(crate_dir, "kf.toml")) or not os.path.isdir(src):
            continue
        crate = crate_name(crate_dir)
        for directory, _dirs, names in os.walk(src):
            sub = os.path.relpath(directory, src)
            module = crate if sub == "." else crate + "." + sub.replace(os.sep, ".")
            for name in sorted(names):
                if name.endswith(".kf"):
                    files.append(File(os.path.join(directory, name), crate, module))
    return files


def declarations(file):
    """A file's functions, vals, types and methods, with their signatures."""
    out, owner, in_trait_impl = [], None, False
    for number, line in enumerate(file.lines, 1):
        if line.startswith("}"):
            owner, in_trait_impl = None, False
        if TRAIT_IMPL.match(line):
            in_trait_impl = True
            continue
        m = IMPL.match(line)
        if m:
            owner = m.group(1)
            continue
        m = TOP.match(line)
        if m:
            written = (m.group(1) or "private").strip()
            decl = Decl(file, number, m.group(2), m.group(3), written)
            decl.signature = line
            out.append(decl)
            continue
        m = METHOD.match(line)
        if m and owner and not in_trait_impl:
            written = (m.group(1) or "private").strip()
            decl = Decl(file, number, "method", m.group(3), written, owner, "static" in m.group(2))
            decl.signature = line
            out.append(decl)
    # A struct's public fields and an enum's payloads are part of what it shows.
    current = None
    for line in file.lines:
        m = re.match(r"^(?:pub |internal )?(?:view )?(struct|enum) (\w+)", line)
        if m:
            current = next((d for d in out if d.kind == m.group(1) and d.name == m.group(2)), None)
        elif line.startswith("}"):
            current = None
        elif current and (current.kind == "enum" or re.match(r"^    pub (val|var) ", line)):
            current.signature += " " + re.sub(r"//.*", "", line)
    return out


def reach(decl, user):
    if user.crate != decl.file.crate:
        return "pub"
    if user.module != decl.file.module:
        return "internal"
    return "private"


def used_in_own_file(d):
    """Named again in the file that declares it, beyond the declaration."""
    f = d.file
    if d.kind != "method":
        return f.word_counts[d.name] > 1
    if d.static:
        return (d.owner, d.name) in f.static_calls or ("Self", d.name) in f.static_calls
    # `.name(` in its own file, or the bare word again for a call written `name(x)`.
    return d.name in f.method_calls or f.word_counts[d.name] > 1


def widen(decl, level, from_test):
    if RANK[level] > RANK[decl.needed]:
        decl.needed = level
    if not from_test:
        decl.only_tests = False


def judge(decls, files):
    shared = defaultdict(set)
    for d in decls:
        if d.kind == "method":
            shared[d.name].add(d.owner)
    for d in decls:
        d.ambiguous = d.kind == "method" and not d.static and len(shared[d.name]) > 1
        users = []
        for f in files:
            if f is d.file:
                continue
            if d.kind == "method":
                named = (d.owner, d.name) in f.static_calls if d.static else d.name in f.method_calls
                # An extension on a type is also called as a free function.
                named = named or (not d.static and d.name in f.words and f.sees(d.file.module, d.name))
            else:
                named = d.name in f.words and f.sees(d.file.module, d.name)
            if named:
                users.append(f)
        d.only_tests = bool(users) and all(f.is_test for f in users)
        for f in users:
            widen(d, reach(d, f), f.is_test)
        if used_in_own_file(d):
            widen(d, "private", False)
        d.users = len(users)
    # A type is held by whoever calls something that mentions it.
    types = {d.name: d for d in decls if d.kind in ("struct", "enum", "trait")}
    changed = True
    while changed:
        changed = False
        for d in decls:
            shown = min(RANK[d.written], RANK[d.needed]) if d.kind != "method" else RANK[d.needed]
            for word in set(WORD.findall(d.signature)):
                t = types.get(word)
                if t and t is not d and t.file.crate == d.file.crate and shown > RANK[t.needed]:
                    t.needed = [k for k, v in RANK.items() if v == shown][0]
                    t.only_tests = False
                    changed = True


def report(decls, wanted, show_all):
    by_crate = defaultdict(list)
    for d in decls:
        if d.file.is_test:
            continue
        by_crate[d.file.crate].append(d)
    for crate in sorted(by_crate):
        if wanted and crate not in wanted:
            continue
        rows = by_crate[crate]
        # `main` is called by the runtime, not by anything that names it.
        rows = [d for d in rows if not (d.kind == "fun" and d.name == "main")]
        narrow = [d for d in rows if not d.ambiguous and RANK[d.needed] < RANK[d.written]]
        unsure = [d for d in rows if d.ambiguous and d.written != "private"]
        print(f"\n== {crate}: {len(rows)} declarations, {len(narrow)} wider than their use, "
              f"{len(unsure)} not judged")
        for target in ("unused", "private", "internal"):
            group = [d for d in narrow if d.needed == target]
            if not group:
                continue
            title = {"unused": "named nowhere, not even in its own file", "private": "could be private (module only)",
                     "internal": "could be internal (crate only)"}[target]
            print(f"\n  {title}: {len(group)}")
            for d in sorted(group, key=lambda d: (d.file.path, d.line)):
                note = "  [tests only]" if d.only_tests else ""
                print(f"    {d.written:8} {d.kind:6} {d.label():44} {d.where()}{note}")
        if unsure:
            print(f"\n  not judged, the method name is shared by several types: {len(unsure)}")
            if show_all:
                for d in sorted(unsure, key=lambda d: (d.file.path, d.line)):
                    print(f"    {d.written:8} method {d.label():44} {d.where()}")
        if show_all:
            fine = [d for d in rows if not d.ambiguous and RANK[d.needed] >= RANK[d.written]]
            print(f"\n  as narrow as their use allows: {len(fine)}")


def apply(decls, wanted):
    """Rewrite each judged declaration to the visibility its use needs."""
    edits = defaultdict(list)
    for d in decls:
        if d.file.is_test or d.file.crate not in wanted or d.ambiguous:
            continue
        if d.kind == "fun" and d.name == "main":
            continue
        if d.needed in ("private", "internal") and RANK[d.needed] < RANK[d.written]:
            edits[d.file.path].append(d)
    count = 0
    for path, group in edits.items():
        lines = open(path).read().split("\n")
        for d in group:
            old = d.written + " "
            new = "" if d.needed == "private" else "internal "
            line = lines[d.line - 1]
            indent = line[:len(line) - len(line.lstrip())]
            if line.lstrip().startswith(old):
                lines[d.line - 1] = indent + new + line.lstrip()[len(old):]
                count += 1
        open(path, "w").write("\n".join(lines))
    print(f"narrowed {count} declarations in {len(edits)} files")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if "-h" in sys.argv or "--help" in sys.argv:
        print(__doc__)
        return
    files = load()
    decls = [d for f in files for d in declarations(f)]
    judge(decls, files)
    wanted = {a.replace("-", "_") for a in args}
    if "--apply" in sys.argv:
        if not wanted:
            sys.exit("--apply needs the crates to rewrite named")
        apply(decls, wanted)
        return
    report(decls, wanted, "--all" in sys.argv)


if __name__ == "__main__":
    main()
