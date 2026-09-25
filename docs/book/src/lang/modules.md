# Modules, crates, and visibility

A KFlat project is one **crate** — a `kf.toml` manifest plus a `src/`
directory tree. Files and directories under `src/` become modules.

## Files are modules

Every `.kf` file under `src/` is a module. The file `src/data.kf` is the
module `data`. A subdirectory `src/util/format.kf` is `util.format`.

```
src/
  main.kf           # crate root — where main() lives
  data.kf           # module `data`
  util/
    format.kf       # module `util.format`
```

The crate root file (usually `main.kf` for a binary or `lib.kf` for a
library) is the entry point. It is not a module — it is the top-level
scope for the crate.

## Imports

The `import` statement brings names from other modules into scope. It must
appear before any declarations:

```kflat
import data.*             // everything from src/data.kf
import util.format.*      // everything from src/util/format.kf
```

The `.*` suffix is required — KFlat does not have item-level imports. An
import makes every `pub` declaration from that module visible.

You can import a dependent crate the same way. If `kf.toml` declares a
dependency on `my_lib`, then:

```kflat
import my_lib.*
```

brings every `pub` declaration from that crate into scope.

## pub: the crate boundary

`pub` makes a declaration visible to code outside the crate. Without `pub`,
the declaration is accessible only within the crate that defines it:

```kflat
// src/data.kf
pub fun query(): int32 { ... }    // visible to dependent crates
fun helper(): void { ... }        // crate-internal only
```

The same applies to fields: a `pub` field on a `pub` struct is visible
outside the crate; a non-`pub` field on a `pub` struct is not.

A crate-private type can still have public methods — the method is exported,
but the type it lives on is not nameable by dependents. They can call the
method through a value they already have, but they cannot declare a new
variable of that type.

## Interface files (.kfi)

When the compiler builds a crate with dependencies, it reads each
dependency's **interface file** — a `.kfi` file produced during compilation
of that dependency. The `.kfi` carries the public declarations, type
signatures, and trait implementations of the depender's view of the crate,
stripped of bodies. This is how separate compilation works: the compiler does
not re-parse the dependency's source, it reads the interface.
