# Editor support

The editor tooling lives in two repositories beside the compiler:
[kf-extensions](https://github.com/komp-co/kf-extensions), whose `vscode/`
directory is the VS Code extension, and
[kf-lsp](https://github.com/komp-co/kf-lsp), a language server for other
editors.

Both editors below get the same things — highlighting, diagnostics, an
outline with folding ranges, hover, inlay hints, expand-selection and
signature help — by the same routes: a
TextMate grammar generated from the compiler's own lexer tables, `komp check
--diagnostic-format=json`, and [`komp query`](cli.md#komp-query).

## VS Code

`vscode/` in kf-extensions is the extension. It has no runtime dependencies,
so linking the folder into the extensions directory installs it. From a
kf-extensions checkout:

```sh
ln -s "$PWD/vscode" ~/.vscode/extensions/vscode-kflat
```

Restart VS Code afterwards. To build an installable file instead, run
`npx @vscode/vsce package` in that directory and
`code --install-extension` the `.vsix` it writes. To develop it, open the
directory in VS Code and press F5.

Point it at your compiler if `komp` is not on `PATH`:

```json
{ "kflat.kompPath": "/path/to/komp/.build/komp" }
```

`.build/komp` is the one `scripts/refresh-komp.sh` writes, and `bin/komp` is
a symlink to it, so either path names the same file. Pointing at anything
else is how an editor ends up reporting errors the terminal does not: the
message to recognise is a name the compiler gained recently being reported as
missing, which dates the binary rather than describing the code.

What it does:

| | |
|---|---|
| Highlighting | `.kf` files, from a grammar generated out of `kw_str` and `op_str` |
| Quick fixes | a repair for a diagnostic that carries one, from the `fix` field |
| Diagnostics | `komp check` on open and save, per crate, errors and warnings |
| Outline | breadcrumbs, the outline view and "go to symbol in file", from `komp query symbols` |
| Folding | the import block and every declaration, from `komp query folding` |
| Hover | the type of the expression under the pointer, from `komp query hover` |
| Inlay hints | the type of every `val`/`var` written without one, from `komp query inlays` |
| Expand selection | the chain of spans around the cursor, from `komp query selection` |
| Signature help | the callee's parameters while typing a call, from `komp query signature` |
| Completion | the members of a receiver after `.`, from `komp query completion` |
| Rename | a declaration and every use of it, from `komp query rename` |
| Go to definition | where a name was declared, from `komp query references` |
| Find references | every use of it in the crate, from the same query |
| Semantic highlighting | a name coloured by what it is, from `komp query tokens` |
| Status bar | error and warning count for the crate being edited |
| `KFlat: Run komp check on this crate` | check on demand |

Settings: `kflat.kompPath`, `kflat.checkOnSave`, `kflat.checkOnOpen`,
`kflat.checkTimeoutMs`, `kflat.queryTimeoutMs`.

The outline and the folds come from a parse, not a check, so they keep
answering while the file is half-written. Hover, inlay hints and signature
help come from a check, which costs what a `komp check` costs.

Every answer describes the buffer. An unsaved one is written to a temporary
file and passed as `komp query --overlay`, while `--file` stays the
document's own path, so the file keeps its place in its crate and only its
bytes come from the buffer.

Go-to-definition and find-references reach top-level declarations —
functions, structs, enums. A parameter or a local answers with nothing:
the resolver stamps only top-level names, and the typechecker's own scope
is not exposed to a query yet.

What it does not do: formatting.

### Where the highlighting comes from

The grammar is generated, not written. `scripts/generate-grammar.js` reads the
keyword spellings out of `kw_str`, the operator spellings out of `op_str`, and
the builtin type names out of `is_runtime_type_name` — the same tables the
compiler lexes and diagnoses with — and emits
`syntaxes/kflat.tmLanguage.json`. Regenerate it after changing any of them.
The generator reads a komp checkout beside kf-extensions, or the one
`KOMP_REPO` names:

```sh
cd vscode
node scripts/generate-grammar.js            # rewrite the grammar
node scripts/generate-grammar.js --check    # fail if it is out of date
```

Adding a keyword or an operator to the compiler makes the generator fail
until the new spelling is given a scope, rather than leaving it silently
uncolored.

A regex grammar cannot know what a name means, only what it looks like, so
its colouring is lexical: every `UpperCamelCase` word reads as a type, and a
variable is not distinguished from a function. `komp query tokens` answers
that properly — the grammar paints instantly and offline, and the semantic
tokens correct it once the crate has been checked.

The grammar paints every literal form the lexer reads. It marked four of
them as errors for as long as the lexer rejected them; the lexer grew all
four, and a rule that outlives its limitation paints correct source red —
so those rules are gone, and the grammar's test suite pins the scopes they
have now.

## Neovim

kf-lsp's `server.js` is a minimal language server — one Node file, no
dependencies, Node 18+. On open and save it runs
`komp check --diagnostic-format=json` on the file's crate (the nearest
ancestor with a `kf.toml`) and republishes the diagnostics; it answers
`textDocument/documentSymbol`, `foldingRange`, `hover`, `inlayHint`,
`selectionRange`, `signatureHelp`, `completion`, `rename`, `definition`,
`references` and `semanticTokens/full` by
running `komp query` over the one file. It never parses
`.kf` itself, so the compiler stays the single source of truth.

No plugin needed — start it from an autocmd:

```lua
vim.filetype.add({ extension = { kf = "kflat" } })

vim.api.nvim_create_autocmd("FileType", {
  pattern = "kflat",
  callback = function(args)
    vim.lsp.start({
      name = "kflat-lsp",
      cmd = { "node", vim.fn.expand("~/path/to/kf-lsp/server.js") },
      root_dir = vim.fs.root(args.buf, "kf.toml"),
      cmd_env = { KOMP_BIN = vim.fn.expand("~/path/to/komp/.build/komp") },
    })
  end,
})
```

The server takes the first `kf.toml` found above the opened file as the
project root. It negotiates `positionEncoding: utf-8` when the client offers
it, and converts byte columns to UTF-16 otherwise.

For highlighting, point a TextMate-compatible plugin at the generated
`vscode/syntaxes/kflat.tmLanguage.json` from kf-extensions, or write a Tree-sitter
grammar — there is not one yet.

## Both are subprocess bridges

Neither tool parses KFlat. Each shells out to komp: `komp check` per save,
which costs a process launch and a whole-crate re-check and gives answers
only as often as you save, and `komp query` per outline, fold, hover or
inlay request — a process launch and a re-parse of the one file, or, for the
two that report a type, a process launch and a re-check of the crate.

The direction is a KFlat-native language server that imports `kf_parse` and
`kf_typecheck` in-process, the way rust-analyzer links `rustc_lexer` rather
than shelling out to `rustc`. That is what semantic highlighting, hover and
go-to-definition all wait on
([kf-lsp#1](https://github.com/komp-co/kf-lsp/issues/1)).
