# kf-core

What every pass shares: the tree they pass along, the names they must spell
the same way, and how they report. It depends on `core` and `alloc` only, and
no compiler crate may be imported from here.

| Module | Holds |
|---|---|
| `kf_core.ast` | The tree. The module itself holds only its root: `Module`, `Crate`, `Import`, and `NameIndex`, which finds a module's declarations by name without a scan. Each node kind is a sub-module, listed below. |
| `kf_core.intern` | `Sym` and `CrateId`: interned names, compared by id. |
| `kf_core.names` | Every emitted C name that more than one crate spells: mangle fragments, reserved prefixes, crate prefixes. Codegen writes these and kf-interface publishes them, so each has exactly one definition, here. |
| `kf_core.lang` | What the language fixes and the libraries or the user fill in, as data: lang items, intrinsics, derives, the annotations the compiler accepts. |
| `kf_core.runtime` | The functions every generated translation unit may call without a declaration. |
| `kf_core.diagnostic` | Diagnostics, their rendering, and the lint registry. |
| `kf_core.span` | Source positions and the map from them back to files and lines. |

## Inside `kf_core.ast`

| Sub-module | Holds |
|---|---|
| `declaration` | `TopDecl` and what it is made of: parameters, fields, variants, `DeclId`, `Linkage`, the template `MemberLoop` an item is written in, and filtering a declaration list in place. |
| `annotation` | An annotation as written, and what a declared one may mark. |
| `expression` | `Expr`, its kinds, the coercion recorded on it, and the names a list literal parses to. |
| `statement` | `Stmt`, its kinds, the lowered `Switch`, and what a statement yields as a block's value. |
| `pattern` | `Pattern` and the `when` arm that carries one. |
| `types` | Both type representations: `TypeRef` as written, `Type` as resolved, and a type's identity. |
| `borrow` | What counts as a borrow or a view, and where a returned one comes from. |
| `dyn_meta` | Trait-object layouts and vtable instances. |
| `subst` | `Substitution`, and applying one to every node kind. |

## Rules

- A node's pass annotations (`Expr.ty`, `TopDecl.crate`) sit on the outer
  struct and are filled in as passes run; the parser leaves them poison.
- Poison (`Type`, `TypeRef`, `Expr`, `Stmt`, `Pattern`) propagates silently:
  whoever creates one has already reported the error.
- A spelling goes in `names` as soon as a second crate needs it. A copy in
  another crate is a link error waiting for the two to drift.
