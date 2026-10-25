#!/usr/bin/env python3
"""Port mcfish's history onto fcfish, one gated commit per mcfish commit.

mcfish is this tree's C23 sibling: the same engine and decomposition, one language
revision apart. Porting it is a 3-way merge, not a rewrite. `mirror` lowers every
mcfish commit to C17 and stores the result as a commit chain under
refs/mirror/mcfish. Cherry-picking a mirror commit is then a merge with
base = lower(parent), theirs = lower(commit) and ours = fcfish, so fcfish's own
work -- the ACSL contracts, the Frama-C gates, its pages -- rides through every
pick, and only a hunk both trees touched conflicts.

Commands (run from the repository root, on the branch that receives the ports):

  mirror [BASE]      lower mcfish BASE..HEAD into refs/mirror/mcfish. BASE is
                     the mcfish commit this tree already carries; the default is
                     the one the newest "Port mcfish <sha>." line on HEAD names.
  replay [--until SHA]
                     pick the next mirror commits, gate each and commit it with
                     mcfish's author and date. Stop at the first conflict or red
                     gate, leaving the pick staged, or after the pick SHA names
                     (a mirror or an mcfish sha; resume and skip take it too).
  resume [--note TEXT]...
                     gate and commit the staged pick after a manual fix, then
                     replay on. Each --note becomes a paragraph of the body.
  skip --reason TEXT drop the staged pick -- a change with no C17 counterpart --
                     record why in an empty commit, then replay on.
  conflicts FILE     print each conflict in FILE as its two halves.
  lower PATH         lower stdin as the file PATH would be, and print it. Page
                     renumbering needs a tree's page list, so `lower` leaves
                     page names as they are.

A build that fails only because a file the pick touched spells `bool`, `true`,
`false` or `NULL` with no header behind it -- C23 keywords, C17 macros -- gets the
header and is rebuilt, and the commit body says so; any other build failure stops.

The gates per pick, each run only when the pick can move it:
  format   the touched src/ and tests/ C files, with the clang-format major the
           parity workflow pins (as build.sh's find_clang_format resolves it)
  build    ./build.sh build; a warning is logged, not fatal, because mcfish's own
           intermediate commits carry some
  anchor   ./build.sh signature, against the golden the pick itself carries --
           mcfish's anchor at that commit. Inside an upstream range mcfish leaves
           the golden at the range's start, so a drift is accepted when the bench
           equals a number the mcfish message states (see range_anchor)
  docs     ./build.sh docs-lint and cite-check, when a page or the lint changed, and
           ./build.sh shellcheck, when a shell script did (once mcfish's lint exists)
  frama-c  ./build.sh frama-c and wp, when the pick touches a file carrying ACSL
           or adds a construct the analyser's kernel is known to refuse
  gcc      CC=gcc-14 into build/fcfish-gcc, when the pick adds a construct the two
           compilers lower differently

What a gate does not catch is the prose. Lowering the project name makes any
mcfish sentence ABOUT mcfish -- a sweep it ran, a figure it measured -- read as a
claim about this tree. Review every ported line that names a sibling or a
measurement, and attribute it to mcfish where the fact is mcfish's.

State lives in the worktree's git directory (mcfish-port/), never in the tree, so
a wiped scratch directory or a fresh shell resumes where the last run stopped.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import textwrap
from collections.abc import Callable
from pathlib import Path

MCFISH = Path(os.environ.get("MCFISH_REPO", "../mcfish"))
MIRROR_REF = "refs/mirror/mcfish"
PARITY_WORKFLOW = Path(".github/workflows/fcfish_parity.yml")

# Give each stop reason its own exit code, so a caller can branch on why a replay halted.
STOP_CONFLICT, STOP_BUILD, STOP_ANCHOR, STOP_FRAMAC, STOP_WP, STOP_GCC, STOP_DOCS = range(10, 17)


# ---------------------------------------------------------------- lowering


class Lowering:
    """Lower one mcfish tree from C23 to the C17 this tree is written in.

    Every rule is mechanical and reproduces a spelling fcfish already uses; a
    change that needs judgement is left to the merge, where it conflicts or fails
    a gate instead of being guessed.
    """

    # `enum : uint8_t {`, `enum Tag : size_t {` -- C23's fixed underlying type.
    TYPED_ENUM = re.compile(r"\benum(\s+[A-Za-z_]\w*)?\s*:\s*[A-Za-z_]\w*\s*\{")

    def __init__(self, doc_names: list[str]) -> None:
        self.docs = self._docs_map(doc_names)
        keys = sorted(self.docs, key=len, reverse=True)
        self.docs_re = re.compile(r"\b(" + "|".join(map(re.escape, keys)) + r")") if keys else None

    @staticmethod
    def _docs_map(doc_names: list[str]) -> dict[str, str]:
        """Map each mcfish page past tooling-ci one number up.

        fcfish carries one page mcfish does not, Frama-C, directly after
        tooling-ci; every later page therefore sits one place further on here.
        """
        pages = sorted(n for n in doc_names if re.match(r"\d\d-.*\.md$", n))
        tooling = [int(n[:2]) for n in pages if n[3:] == "tooling-ci.md"]
        if not tooling:
            return {}
        return {n: f"{int(n[:2]) + 1:02d}-{n[3:]}" for n in pages if int(n[:2]) > tooling[0]}

    def path(self, rel: str) -> str:
        rel = rel.replace(".github/workflows/mcfish_", ".github/workflows/fcfish_")
        head, base = os.path.split(rel)
        if head == "docs" and base in self.docs:
            return os.path.join(head, self.docs[base])
        return rel

    def names(self, text: str) -> str:
        text = (
            text.replace("MCFISH", "FCFISH").replace("Mcfish", "Fcfish").replace("mcfish", "fcfish")
        )
        if self.docs_re:
            text = self.docs_re.sub(lambda m: self.docs[m.group(1)], text)
        return text

    def text(self, rel: str, text: str) -> str:
        text = self.names(text)
        return lower_c(text) if rel.endswith((".c", ".h")) else text


def lower_enums(s: str) -> str:
    """Drop each fixed underlying type; mark a typedef'd enum packed instead.

    `packed` is what keeps `typedef enum : uint8_t {...} Piece;` a one-byte type
    in C17, so every struct layout and table that depends on the width is
    unchanged. An anonymous `enum : T { ... };` only names constants and loses
    the type alone.
    """
    out: list[str] = []
    i = 0
    while m := Lowering.TYPED_ENUM.search(s, i):
        line_start = s.rfind("\n", 0, m.start()) + 1
        is_typedef = re.search(r"\btypedef\s+$", s[line_start : m.start()]) is not None
        out.append(s[i : m.start()] + "enum" + (m.group(1) or "") + " {")
        depth, k = 1, m.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(s[k], 0)
            k += 1
        out.append(s[m.end() : k])
        i = k
        if is_typedef and (nm := re.match(r"(\s*)([A-Za-z_]\w*)(\s*;)", s[i:])):
            out.append(f"{nm.group(1)}__attribute__((packed)) {nm.group(2)}{nm.group(3)}")
            i += nm.end()
    out.append(s[i:])
    return "".join(out)


def gnu_attribute(m: re.Match[str]) -> str:
    items = [x.strip().removeprefix("gnu::") for x in m.group(1).split(",")]
    return "__attribute__((" + ", ".join(items) + "))"


def lower_c(s: str) -> str:
    """Rewrite the C23 spellings mcfish uses into the C17 ones fcfish uses.

    Two rules are NOT equivalent, and each has a gate that sees it:

    - `constexpr -> const`: a C17 `const` object is not a constant expression.
      clang folds it inside `static_assert` and array bounds as an extension;
      Frama-C's kernel refuses, so a pick that adds `constexpr` runs the frama-c
      gate. Where it refuses, spell the constant as an enumeration constant, as
      the depth markers in search_common.h do.
    - `[[nodiscard]] -> warn_unused_result`: C23 and clang accept a `(void)` cast
      as a deliberate discard; gcc does not. A pick that adds either runs the gcc
      build. Where it refuses, name the result and void the name, as
      engine_options.c does: `const bool set = f(); (void) set;`.

    Only <stdbit.h>'s counts are lowered. Any other stdc_ call is left in place,
    so it fails the build loudly rather than being guessed at.
    """
    s = s.replace("nullptr", "NULL")
    s = lower_enums(s)
    s = s.replace("[[nodiscard]]", "__attribute__((warn_unused_result))")
    s = s.replace("[[maybe_unused]]", "__attribute__((unused))")
    s = s.replace("[[fallthrough]]", "__attribute__((fallthrough))")
    s = re.sub(r"\[\[(gnu::[^\]]*)\]\]", gnu_attribute, s)
    s = re.sub(r"\bconstexpr\b", "const", s)
    # <stdbit.h> is C23; its counts are the builtins the tree used before it. Take
    # a blank line with an include that stood alone, so no double blank is left.
    s = s.replace("\n\n#include <stdbit.h>\n\n", "\n\n")
    s = re.sub(r"^[ \t]*#include <stdbit\.h>\n", "", s, flags=re.M)
    s = s.replace("stdc_count_ones((unsigned)", "__builtin_popcount((unsigned)")
    s = s.replace("(int) stdc_count_ones(b)", "__builtin_popcountll(b)")
    return s.replace("stdc_count_ones(", "__builtin_popcountll(")


def lowering_rules_fired(added: str) -> list[str]:
    """Name the rules a change's added C lines needed, for the commit body."""
    rules = [
        ("nullptr", "nullptr -> NULL"),
        (
            r"\benum(\s+\w+)?\s*:\s*\w+\s*\{",
            "fixed-type enum -> plain enum (packed where typedef'd)",
        ),
        (r"\bconstexpr\b", "constexpr -> const"),
        (r"\[\[nodiscard\]\]", "[[nodiscard]] -> __attribute__((warn_unused_result))"),
        (r"\[\[maybe_unused\]\]", "[[maybe_unused]] -> __attribute__((unused))"),
        (r"\[\[gnu::", "[[gnu::x]] -> __attribute__((x))"),
        (r"stdc_|stdbit\.h", "<stdbit.h> counts -> __builtin_popcount{,ll}"),
    ]
    return [name for pattern, name in rules if re.search(pattern, added)]


# ---------------------------------------------------------------- git and tools


def run(cmd: list[str], *, cwd: Path | None = None, check: bool = True, **kw) -> str:
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False, **kw)
    if check and r.returncode != 0:
        sys.exit(f"mcfish_port: {' '.join(cmd)} failed ({r.returncode}):\n{r.stderr.strip()}")
    return r.stdout


def git(*args: str, cwd: Path | None = None, check: bool = True) -> str:
    return run(["git", *args], cwd=cwd, check=check)


def state_dir() -> Path:
    d = Path(git("rev-parse", "--git-dir").strip()) / "mcfish-port"
    d.mkdir(exist_ok=True)
    return d


def read_state(name: str) -> str:
    p = state_dir() / name
    return p.read_text().strip() if p.exists() else ""


def write_state(name: str, value: str) -> None:
    (state_dir() / name).write_text(value + "\n")


def append_state(name: str, line: str) -> None:
    with (state_dir() / name).open("a") as fh:
        fh.write(line.rstrip("\n") + "\n")


def clang_format() -> str:
    """Resolve the clang-format CI pins, exactly as build.sh's find_clang_format does.

    A different major reflows this tree, so formatting a pick with the host's
    default would pass locally and fail the pinned CI lane.
    """
    m = re.search(r'^  CLANG_VERSION: "(\d+)"$', PARITY_WORKFLOW.read_text(), re.M)
    if not m:
        sys.exit(f"mcfish_port: no CLANG_VERSION pin in {PARITY_WORKFLOW}")
    want = m.group(1)
    for c in (
        os.environ.get("CLANG_FORMAT", ""),
        f"clang-format-{want}",
        f"/usr/lib/llvm-{want}/bin/clang-format",
        "clang-format",
    ):
        if c and shutil.which(c):
            v = re.search(r"version (\d+)", run([c, "--version"], check=False))
            if v and v.group(1) == want:
                return c
    sys.exit(f"mcfish_port: no clang-format {want}; set CLANG_FORMAT (see ./build.sh fmt)")


# ---------------------------------------------------------------- mirror


def mcfish_sha_of(commit: str) -> str:
    """Return the full mcfish sha a mirror commit was lowered from."""
    m = re.search(r"^mcfish: ([0-9a-f]{40})$", git("log", "-1", "--format=%B", commit), re.M)
    if not m:
        sys.exit(f"mcfish_port: {commit} carries no 'mcfish:' line; is it a mirror commit?")
    return m.group(1)


def last_ported() -> str:
    """Return the mcfish commit the newest 'Port mcfish <sha>.' line on HEAD names."""
    m = re.search(r"^Port mcfish ([0-9a-f]{7,40})", git("log", "--format=%B", "-n", "2000"), re.M)
    if not m:
        sys.exit("mcfish_port: no 'Port mcfish <sha>.' line on HEAD; pass BASE explicitly")
    return git("rev-parse", m.group(1), cwd=MCFISH).strip()


def cmd_mirror(base: str | None) -> None:
    base = git("rev-parse", base, cwd=MCFISH).strip() if base else last_ported()
    commits = [base, *git("rev-list", "--reverse", f"{base}..HEAD", cwd=MCFISH).split()]
    style_dir = state_dir() / "styles"
    style_dir.mkdir(exist_ok=True)
    formatter = clang_format()

    cat = subprocess.Popen(
        ["git", "cat-file", "--batch"], cwd=MCFISH, stdin=subprocess.PIPE, stdout=subprocess.PIPE
    )
    assert cat.stdin and cat.stdout

    def blob(sha: str) -> bytes:
        assert cat.stdin and cat.stdout
        cat.stdin.write(sha.encode() + b"\n")
        cat.stdin.flush()
        size = int(cat.stdout.readline().split()[2])
        data = cat.stdout.read(size)
        cat.stdout.read(1)
        return data

    fi = subprocess.Popen(["git", "fast-import", "--quiet", "--force"], stdin=subprocess.PIPE)
    assert fi.stdin
    out = fi.stdin
    # Start the chain afresh: without the reset, fast-import continues from
    # whatever the ref held, and the base would not be a root.
    out.write(f"reset {MIRROR_REF}\n\n".encode())

    marks: dict[bytes, int] = {}
    memo: dict[tuple[str, str, str, str], int] = {}

    def emit(data: bytes) -> int:
        key = hashlib.sha1(data).digest()
        if key not in marks:
            marks[key] = len(marks) + 1
            out.write(b"blob\nmark :%d\ndata %d\n" % (marks[key], len(data)) + data + b"\n")
        return marks[key]

    for n, c in enumerate(commits):
        entries = []
        for rec in git("ls-tree", "-r", "-z", c, cwd=MCFISH).split("\0"):
            if rec:
                meta, path = rec.split("\t", 1)
                mode, kind, sha = meta.split()
                if kind == "blob":
                    entries.append((mode, sha, path))
        low = Lowering([os.path.basename(p) for _, _, p in entries if os.path.dirname(p) == "docs"])
        docs_key = repr(sorted(low.docs.items()))
        style = next((s for _, s, p in entries if p == ".clang-format"), "")
        style_file = style_dir / f"{style}.clang-format"
        if style and not style_file.exists():
            style_file.write_bytes(blob(style))

        files = []
        for mode, sha, path in entries:
            key = (sha, path, docs_key, style)
            if key not in memo:
                data = blob(sha)
                if mode != "120000" and b"\0" not in data[:8192]:
                    try:
                        new = low.text(path, data.decode()).encode()
                    except UnicodeDecodeError:
                        new = data
                    # Re-format only what the lowering changed: mcfish's own
                    # bytes are already clean under the pinned major.
                    if (
                        new != data
                        and path.endswith((".c", ".h"))
                        and path.startswith(("src/", "tests/"))
                    ):
                        new = run(
                            [formatter, f"--style=file:{style_file}", f"--assume-filename={path}"],
                            input=new.decode(),
                        ).encode()
                    data = new
                memo[key] = emit(data)
            files.append((mode, memo[key], low.path(path)))

        fmt = "%an%x00%ae%x00%at %ad%x00%cn%x00%ce%x00%ct %cd%x00%B"
        an, ae, ad, cn, ce, cd, body = git(
            "log", "-1", f"--format={fmt}", "--date=format:%z", c, cwd=MCFISH
        ).split("\0", 6)
        if n == 0:
            body = f"lower(mcfish {c[:9]}): the base fcfish already carries\n"
        msg = (body.rstrip("\n") + f"\n\nmcfish: {c}\n").encode()
        out.write(
            f"commit {MIRROR_REF}\nauthor {an} <{ae}> {ad}\ncommitter {cn} <{ce}> {cd}\n".encode()
        )
        out.write(b"data %d\n" % len(msg) + msg + b"deleteall\n")
        for mode, mark, path in files:
            out.write(f"M {mode} :{mark} {path}\n".encode())
        out.write(b"\n")

    out.close()
    if fi.wait() != 0:
        sys.exit("mcfish_port: git fast-import failed")
    cat.stdin.close()
    root = git("rev-list", "--max-parents=0", MIRROR_REF).strip()
    write_state("last", root)
    print(f"mirror: {len(commits) - 1} mcfish commits over {base[:9]} -> {MIRROR_REF}")


# ---------------------------------------------------------------- the tooling page

# The tooling page is where fcfish's three Frama-C gates sit inside tables and lists
# mcfish keeps editing, so the same three conflict shapes recur with every gate
# mcfish adds. Resolve exactly those; anything else stays a conflict.

CONFLICT = re.compile(r"^<<<<<<< [^\n]*\n(.*?)^=======\n(.*?)^>>>>>>> [^\n]*\n", re.S | re.M)
FRAMAC_ROWS = ("| `frama-c`", "| `eva`", "| `wp`")
FRAMAC_PARA = "\nThe last three are the Frama-C gates."
PARITY_ROW = "| `parity` | the aggregate |"
# fmt: off
NUMBERS = (
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
    "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
    "eighteen", "nineteen", "twenty", "twenty-one", "twenty-two", "twenty-three",
    "twenty-four", "twenty-five",
)
# fmt: on


def resolve(text: str, fn: Callable[[str, str], str | None]) -> str | None:
    """Replace each conflict with fn(ours, theirs); None if fn declines any."""
    parts, last = [], 0
    for m in CONFLICT.finditer(text):
        r = fn(m.group(1), m.group(2))
        if r is None:
            return None
        parts += [text[last : m.start()], r]
        last = m.end()
    return "".join(parts) + text[last:] if parts else None


# The gate-table rows that are fcfish's own: the three Frama-C gates, and the fmt
# row, whose pinned-major rule mcfish's gate does not have.
FMT_ROW = "| `fmt` / `fmt-fix` |"


def gate_table_rows(ours: str, theirs: str) -> str:
    """Take mcfish's rows, keeping fcfish's fmt row and appending its Frama-C rows."""
    mine = ours.splitlines(keepends=True)
    fmt = next((r for r in mine if r.startswith(FMT_ROW)), None)
    rows = [fmt if fmt and r.startswith(FMT_ROW) else r for r in theirs.splitlines(keepends=True)]
    return "".join(rows) + "".join(r for r in mine if r.startswith(FRAMAC_ROWS) and r not in rows)


def tooling_page_shape(ours: str, theirs: str) -> str | None:
    if FRAMAC_PARA in ours:  # the parity list, and the paragraph under it
        if theirs.rstrip().endswith("`tb`.") and "`frama-c`, `eva`, `wp`" not in theirs:
            theirs = theirs.rstrip()[:-1] + ", `frama-c`, `eva`, `wp`.\n"
        if FRAMAC_PARA not in theirs:
            theirs += ours[ours.index(FRAMAC_PARA) :]
        return theirs
    if all(line.startswith("|") for line in (ours + theirs).splitlines()):  # the gate table
        return gate_table_rows(ours, theirs)
    if "runs on dispatch" in ours or "**The schedules of" in ours:  # the CI section
        theirs = theirs.replace(
            "runs weekly.\n", "runs on dispatch; its weekly cron is commented out.\n"
        )
        keep = ours.find("**The schedules of")
        if keep >= 0 and "**The schedules of" not in theirs:
            theirs += "\n" + ours[keep:]
        return theirs
    if PARITY_ROW in ours and PARITY_ROW in theirs:  # the gate count alone
        return theirs  # resolve_tooling_page recounts it from do_parity
    return None


def resolve_tooling_page(path: Path) -> bool:
    new = resolve(path.read_text(), tooling_page_shape)
    if new is None:
        return False
    # Re-count the gates the table says parity runs: neither side's number is
    # this tree's once three gates differ between them.
    body = re.search(r"^do_parity\(\) \{\n(.*?)^\}", Path("build.sh").read_text(), re.S | re.M)
    n = len(re.findall(r"^\s+do_[a-z_]+", body.group(1), re.M)) if body else 0
    new = re.sub(
        r"\| the [a-z-]+ gates listed below it", f"| the {NUMBERS[n]} gates listed below it", new
    )
    path.write_text(new)
    return True


# fcfish's three Frama-C step functions sit where mcfish adds every new step of its
# own, so each new mcfish gate conflicts with their tail. Keep both, ours first.
FRAMAC_STEPS_END = 'green "wp OK: every contract goal proved"'


# fcfish smokes one unlaned tool mcfish does not -- this one -- at the end of
# do_tools_smoke, where mcfish appends each tool it adds.
SMOKE_OURS = "  # mcfish_port.py: "
SMOKE_HEADER = re.compile(r"^  # [A-Za-z_]+(?:\.py|\.sh)?: ", re.M)


def build_sh_shape(ours: str, theirs: str) -> str | None:
    if ours.rstrip().endswith(FRAMAC_STEPS_END) and re.search(r"^do_[a-z_]+\(\) \{", theirs, re.M):
        return ours + "}\n\n" + theirs  # the shared closing brace then closes theirs
    if ours.startswith(SMOKE_OURS) and SMOKE_HEADER.match(theirs):
        return ours + "  fi\n\n" + theirs  # the shared `fi` then closes theirs
    return None


def recount_smoke(text: str) -> str:
    """Restate tools-smoke's roster size from the per-tool headers it holds."""
    body = re.search(r"^do_tools_smoke\(\) \{\n(.*?)^\}", text, re.S | re.M)
    if not body:
        return text
    n = len(SMOKE_HEADER.findall(body.group(1)))
    return re.sub(r"tools-smoke: \d+ of \d+ unlaned", f"tools-smoke: {n} of {n} unlaned", text)


def resolve_known(path: str) -> bool:
    """Resolve PATH if every conflict in it is one of the recurring fcfish shapes."""
    if re.match(r"docs/\d\d-tooling-ci\.md$", path):
        return resolve_tooling_page(Path(path))
    if re.match(r"docs/\d\d-.*\.md$", path):  # a page's own gate table
        new = resolve(
            Path(path).read_text(),
            lambda o, th: (
                gate_table_rows(o, th)
                if all(line.startswith("|") for line in (o + th).splitlines())
                else None
            ),
        )
        if new is not None:
            Path(path).write_text(new)
            return True
    if path == "build.sh":
        new = resolve(Path(path).read_text(), build_sh_shape)
        if new is not None:
            Path(path).write_text(recount_smoke(new))
            return True
    return False


# ---------------------------------------------------------------- replay

# Constructs Frama-C's kernel is known to refuse, and a lowered `constexpr`, which is
# a constant expression in mcfish and not one here. Matched on mcfish's added lines.
KERNEL_SENSITIVE = re.compile(
    r"#pragma|__asm|\basm\b|__int128|_Float|__builtin_|_Atomic|\[\[|static_assert|\bconstexpr\b"
)
# Constructs gcc and clang diagnose differently under the tree's -Werror set.
COMPILER_SENSITIVE = re.compile(r"\[\[nodiscard\]\]|\(void\) \w+\(|\benum\b|static_assert")


# The C23 keywords that are macros in C17, and the header that defines each. A file
# mcfish adds after it dropped <stdbool.h> spells them with no include behind them.
C17_HEADERS = {"bool": "stdbool.h", "true": "stdbool.h", "false": "stdbool.h", "NULL": "stddef.h"}
MISSING = re.compile(
    r"^(\S+?):\d+:\d+: error: (?:unknown type name|use of undeclared identifier) '(\w+)'", re.M
)


def add_c17_headers(build_log: str, changed: list[str]) -> list[str]:
    """Include the header each missing C17 macro needs, in the files the pick touched.

    Return one note per edited file, or nothing when any error in the log is of
    another kind or lands in a file the pick did not touch -- that failure is real,
    and the replay must stop on it.
    """
    wanted: dict[str, set[str]] = {}
    errors = MISSING.findall(build_log)
    if not errors or len(errors) != build_log.count(" error: "):
        return []
    for path, ident in errors:
        path = os.path.normpath(path)
        if ident not in C17_HEADERS or path not in changed:
            return []
        wanted.setdefault(path, set()).add(C17_HEADERS[ident])
    notes = []
    for path, headers in sorted(wanted.items()):
        text = Path(path).read_text()
        for h in sorted(headers - {m for m in C17_HEADERS.values() if f"#include <{m}>" in text}):
            system = re.search(r"^#include <[^>]+>\n", text, re.M)
            local = list(re.finditer(r'^#include "[^"]+"\n', text, re.M))
            # Join an existing block of system includes, which clang-format then
            # sorts; with none, open one below the project includes.
            if system:
                text = text[: system.start()] + f"#include <{h}>\n" + text[system.start() :]
            elif local:
                at = local[-1].end()
                gap = "" if text[at:].startswith("\n") else "\n"
                text = text[:at] + f"\n#include <{h}>\n" + gap + text[at:]
            else:
                return []
        Path(path).write_text(text)
        run([clang_format(), "-i", path])
        git("add", path)
        names = ", ".join(f"<{h}>" for h in sorted(headers))
        notes.append(
            f"{path} includes {names}: C23 spells bool and nullptr as keywords, C17 as macros."
        )
    return notes


def range_anchor(mirror: str, signature_log: str) -> str | None:
    """Accept a drifted bench inside an upstream range, or return None to stop.

    mcfish ports an upstream range one commit at a time but re-derives
    tools/signature.golden only at the pin advance, so every pick in between moves
    the bench away from the golden it carries. Accept exactly two cases:

    - the bench MOVED, to a number the mcfish commit's own message states --
      upstream's `Bench:` at that point, which mcfish checks itself against;
    - the bench did NOT move, and the message states no number but that one.

    A message that names both ends ("A -> B") therefore accepts B and refuses A: a
    port that failed to move the bench stops instead of passing on the old number.
    """
    plain = re.sub(r"\x1b\[[0-9;]*m", "", signature_log)
    m = re.search(r"signature DRIFT: expected (\d+), got (\d+)", plain)
    if not m:
        return None
    carried = read_state("anchor") or m.group(1)
    got = m.group(2)
    body = git("log", "-1", "--format=%B", mcfish_sha_of(mirror), cwd=MCFISH)
    stated = re.findall(r"(?<![\d,.])(\d{6,9})(?![\d,.])", body)
    if (got in stated and got != carried) or (got == carried and set(stated) <= {got}):
        write_state("anchor", got)
        return got
    return None


def mcfish_added(mirror: str) -> str:
    """Return the C lines the mcfish commit behind MIRROR added, as mcfish spells them."""
    mc = mcfish_sha_of(mirror)
    diff = git("diff", f"{mc}^", mc, "--", "*.c", "*.h", cwd=MCFISH)
    return "\n".join(
        ln[1:] for ln in diff.splitlines() if ln.startswith("+") and not ln.startswith("+++")
    )


def gate(mirror: str) -> tuple[list[str], list[str]]:
    """Run each gate the staged pick can move; return its body lines and notes."""
    lines: list[str] = []
    notes: list[str] = []
    changed = git("diff", "--cached", "--name-only", "HEAD").split()
    logs = state_dir()

    def attempt(label: str, cmd: list[str], env: dict[str, str] | None = None) -> tuple[bool, str]:
        r = subprocess.run(
            cmd, capture_output=True, text=True, check=False, env={**os.environ, **(env or {})}
        )
        out = r.stdout + r.stderr
        (logs / f"{label}.log").write_text(out)
        return r.returncode == 0, out

    def stop(label: str, out: str, code: int) -> None:
        print(f"STOP {label} {mirror[:9]}\n" + "\n".join(out.splitlines()[-25:]))
        sys.exit(code)

    def step(label: str, cmd: list[str], code: int, env: dict[str, str] | None = None) -> str:
        ok, out = attempt(label, cmd, env)
        if not ok:
            stop(label, out, code)
        return out

    cfiles = [f for f in changed if re.match(r"(src|tests)/.*\.[ch]$", f) and Path(f).exists()]
    if cfiles:
        run([clang_format(), "-i", *cfiles])
        git("add", *cfiles)

    if any(re.match(r"(src/|tests/|build\.sh$|tools/signature\.golden$)", f) for f in changed):
        ok, out = attempt("build", ["./build.sh", "build"])
        if not ok:
            fixed = add_c17_headers(out, changed)
            if not fixed:
                stop("build", out, STOP_BUILD)
            notes += fixed
            out = step("build", ["./build.sh", "build"], STOP_BUILD)
        if "warning:" in out:
            warns = [ln for ln in out.splitlines() if "warning:" in ln]
            append_state("warnings.log", f"== {mirror} {git('log', '-1', '--format=%s', mirror)}")
            for w in warns:
                append_state("warnings.log", w)
            lines.append("./build.sh build        built                           (exit 0)")
        else:
            lines.append("./build.sh build        built, no warnings              (exit 0)")
        ok, out = attempt("anchor", ["./build.sh", "signature"])
        if ok:
            write_state("anchor", "")
            sig = re.search(r"signature OK: \d+ nodes", out)
            lines.append(
                f"./build.sh signature    {sig.group(0) if sig else 'signature OK'}     (exit 0)"
            )
        else:
            got = range_anchor(mirror, out)
            if got is None:
                stop("anchor", out, STOP_ANCHOR)
            lines.append(f"./build.sh signature    drift to {got} nodes, accepted above (exit 1)")
            notes.append(
                f"The bench is {got}, the number the mcfish commit states for this point of "
                "the upstream range, while tools/signature.golden still holds the range's "
                "start: mcfish re-derives the golden once, at the pin advance, and this tree "
                "follows it there."
            )

    if Path("tools/shellcheck.sh").exists() and any(f.endswith(".sh") for f in changed):
        step("shellcheck", ["./build.sh", "shellcheck"], STOP_DOCS)
        lines.append("./build.sh shellcheck   shell clean                     (exit 0)")

    if any(f.endswith(".md") or f.startswith("tools/docs_") for f in changed):
        out = step("docs-lint", ["./build.sh", "docs-lint"], STOP_DOCS)
        dl = re.search(r"docs-lint passed \([^)]*\)", out)
        lines.append(
            f"./build.sh docs-lint    {dl.group(0) if dl else 'docs-lint passed'}   (exit 0)"
        )
        if Path("tools/docs_cite.sh").exists():
            out = step("cite-check", ["./build.sh", "cite-check"], STOP_DOCS)
            cc = re.search(r"every cited SHA resolves \(\d+ of \d+\)|\d+ of \d+ cited SHAs", out)
            lines.append(f"./build.sh cite-check   {cc.group(0) if cc else 'clean'}   (exit 0)")

    added = mcfish_added(mirror)
    acsl = any(
        f.endswith((".c", ".h")) and Path(f).exists() and "/*@" in Path(f).read_text()
        for f in changed
    )
    if (
        acsl
        or KERNEL_SENSITIVE.search(added)
        or any(f.startswith("tools/framac/") for f in changed)
    ):
        step("frama-c", ["./build.sh", "frama-c"], STOP_FRAMAC)
        lines.append("./build.sh frama-c      parses and typechecks           (exit 0)")
        out = step("wp", ["./build.sh", "wp"], STOP_WP)
        wp = re.search(r"\d+ / \d+ goals proved", out)
        lines.append(f"./build.sh wp           {wp.group(0) if wp else 'proved'}   (exit 0)")

    if COMPILER_SENSITIVE.search(added) and shutil.which("gcc-14"):
        step(
            "gcc",
            ["./build.sh", "build"],
            STOP_GCC,
            env={"CC": "gcc-14", "BIN": "build/fcfish-gcc"},
        )
        lines.append("CC=gcc-14 ./build.sh build   built                      (exit 0)")
    return lines, notes


def message(mirror: str, gates: list[str], notes: list[str]) -> str:
    mc = mcfish_sha_of(mirror)
    subject, _, body = git("log", "-1", "--format=%B", mc, cwd=MCFISH).strip("\n").partition("\n")
    low = Lowering([])
    added = mcfish_added(mirror)
    parts = [low.names(subject), f"Port mcfish {mc[:9]}."]
    if body.strip():
        parts.append(low.names(body.strip("\n")))
    rules = lowering_rules_fired(added)
    if rules:
        parts.append("Lowered to C17: " + "; ".join(rules) + ".")
    parts += [textwrap.fill(n, 80) for n in notes]
    if gates:
        parts.append("Gated on this tree:\n\n" + "\n".join("  " + g for g in gates))
    return "\n\n".join(parts) + "\n"


def commit(mirror: str, msg: str, *, allow_empty: bool = False) -> None:
    fmt = "%an%x00%ae%x00%aD"
    an, ae, ad = git("log", "-1", f"--format={fmt}", mirror).split("\0")
    env = {
        **os.environ,
        "GIT_AUTHOR_NAME": an,
        "GIT_AUTHOR_EMAIL": ae,
        "GIT_AUTHOR_DATE": ad.strip(),
    }
    args = ["git", "commit", "-q", "-F", "-"] + (["--allow-empty"] if allow_empty else [])
    r = subprocess.run(args, input=msg, text=True, env=env, check=False)
    if r.returncode != 0:
        sys.exit("mcfish_port: git commit failed")


def finish(mirror: str, notes: list[str]) -> None:
    """Gate the staged pick and commit it; record an empty one as such."""
    if git("diff", "--name-only", "--diff-filter=U").strip():
        sys.exit("mcfish_port: unmerged paths remain; resolve and `git add` them first")
    gates, found = gate(mirror)
    notes = [*notes, *found]
    if not git("diff", "--cached", "--name-only", "HEAD").strip():
        notes = [
            "The change lowers to nothing here: every line it touches already reads this "
            "way in C17.",
            *notes,
        ]
        commit(mirror, message(mirror, [], notes), allow_empty=True)
        print(f"empty {mirror[:9]} {git('log', '-1', '--format=%s', mirror)[:70]}")
    else:
        commit(mirror, message(mirror, gates, notes))
        print(f"ok    {mirror[:9]} {git('log', '-1', '--format=%s', mirror)[:70]}")
    write_state("last", mirror)
    write_state("stop", "")


def next_picks(until: str | None) -> list[str]:
    last = read_state("last")
    if not last:
        sys.exit("mcfish_port: no replay state; run `mirror` first")
    picks = git("rev-list", "--reverse", f"{last}..{MIRROR_REF}").split()
    if until:
        stop = next(
            (
                i
                for i, c in enumerate(picks)
                if mcfish_sha_of(c).startswith(until) or c.startswith(until)
            ),
            None,
        )
        if stop is None:
            sys.exit(f"mcfish_port: --until {until} is not among the remaining picks")
        picks = picks[: stop + 1]
    return picks


def reached(mirror: str, until: str | None) -> bool:
    """Say whether MIRROR is the commit --until names, so a resume can stop on it."""
    return bool(until) and (mirror.startswith(until) or mcfish_sha_of(mirror).startswith(until))


def cmd_replay(until: str | None) -> None:
    for mirror in next_picks(until):
        write_state("stop", mirror)
        r = subprocess.run(
            ["git", "cherry-pick", "--no-commit", mirror],
            capture_output=True,
            text=True,
            check=False,
        )
        if r.returncode != 0:
            unmerged = git("diff", "--name-only", "--diff-filter=U").split()
            # build.sh first: the tooling page recounts its gates from do_parity.
            done = [f for f in sorted(unmerged, key=lambda f: f != "build.sh") if resolve_known(f)]
            if unmerged and len(done) == len(unmerged):
                git("add", *done)
                finish(
                    mirror,
                    [
                        "fcfish's three Frama-C gates (frama-c, eva, wp) keep their place: "
                        "beside the new step in build.sh, and on the tooling page in the "
                        "parity list, the gate table and the dispatch-only schedules."
                    ],
                )
                continue
            print(f"STOP conflict {mirror[:9]} {git('log', '-1', '--format=%s', mirror)}")
            print("\n".join(unmerged) or r.stderr.strip())
            if done:
                print(f"(resolved as known shapes, not staged: {' '.join(done)})")
            sys.exit(STOP_CONFLICT)
        finish(mirror, [])
    print("replay: done")


def cmd_resume(notes: list[str], until: str | None) -> None:
    mirror = read_state("stop")
    if not mirror:
        sys.exit("mcfish_port: no staged pick to resume")
    finish(mirror, notes)
    if not reached(mirror, until):
        cmd_replay(until)


def cmd_skip(reason: str, until: str | None) -> None:
    mirror = read_state("stop")
    if not mirror:
        sys.exit("mcfish_port: no staged pick to skip")
    touched = set(git("diff-tree", "--no-commit-id", "--name-only", "-r", mirror).split())
    dirty = {ln[3:] for ln in git("status", "--porcelain", "--untracked-files=no").splitlines()}
    if dirty - touched:
        sys.exit(
            f"mcfish_port: changes outside the pick, refusing to reset: {sorted(dirty - touched)}"
        )
    git("reset", "-q", "--hard", "HEAD")
    commit(mirror, message(mirror, [], [f"Nothing to port: {reason}"]), allow_empty=True)
    write_state("last", mirror)
    write_state("stop", "")
    print(f"skip  {mirror[:9]} {git('log', '-1', '--format=%s', mirror)[:70]}")
    if not reached(mirror, until):
        cmd_replay(until)


def cmd_conflicts(path: str) -> None:
    for i, m in enumerate(CONFLICT.finditer(Path(path).read_text())):
        print(f"=== {i} ours\n{m.group(1)}=== {i} theirs\n{m.group(2)}", end="")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("mirror").add_argument("base", nargs="?")
    for name in ("replay", "resume", "skip"):
        p = sub.add_parser(name)
        p.add_argument("--until")
        if name == "resume":
            p.add_argument("--note", action="append", default=[])
        if name == "skip":
            p.add_argument("--reason", required=True)
    sub.add_parser("conflicts").add_argument("file")
    sub.add_parser("lower").add_argument("path")
    a = ap.parse_args()

    if a.cmd == "lower":
        sys.stdout.write(Lowering([]).text(a.path, sys.stdin.read()))
    elif a.cmd == "conflicts":
        cmd_conflicts(a.file)
    elif a.cmd == "mirror":
        cmd_mirror(a.base)
    elif a.cmd == "replay":
        cmd_replay(a.until)
    elif a.cmd == "resume":
        cmd_resume(a.note, a.until)
    elif a.cmd == "skip":
        cmd_skip(a.reason, a.until)


if __name__ == "__main__":
    main()
