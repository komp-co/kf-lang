# Modules, crates, and visibility

A KFlat project is one **crate** — a `kf.toml` manifest plus a `src/`
directory tree. Each directory under `src/` is a **module**.

## A module is a directory

Every `.kf` file in one directory belongs to the same module, and its files
share one scope: a function declared in one file is callable from any other
file of that directory, `pub` or not, with no import. The directory `src/`
itself is the crate's root module, named after the crate.

```
src/
  main.kf           # module `shapes` (the crate root)
  helper.kf         # module `shapes`: shares main.kf's scope
  geometry/
    point.kf        # module `shapes.geometry`
    distance.kf     # module `shapes.geometry`
```

A function name is unique within its crate, even across modules: a crate is
one C namespace, so a second `answer` in another directory is an error that
names the first one's module.

Splitting a file within its directory changes nothing for anyone else.
Adding a sub-directory makes a new module, and every file that uses it
imports it.

## Imports

A module's name is its crate's name followed by the directories below `src/`,
joined with dots. An import names a module and either one function in it or
all of them:

```kflat
import shapes.geometry.manhattan   // one function
import shapes.geometry.*           // every pub function of the module
```

A function from any other module — a sibling directory in this crate, or
another crate — needs an import, and must be `pub`. A
[top-level `val`](bindings.md#top-level-values) is imported the same way.
Imports appear before any declarations.

```kflat
// src/geometry/point.kf
pub struct Point {
    pub val x: int32
    pub val y: int32
}

impl Point {
    pub fun sum(): int32 { return self.x + self.y }
}
```

```kflat
// src/geometry/distance.kf
pub fun manhattan(p: &Point): int32 { return magnitude(p.x) + magnitude(p.y) }

fun magnitude(v: int32): int32 { return if v < 0 { -v } else { v } }
```

```kflat
// src/main.kf
import shapes.geometry.manhattan

fun main(): int32 {
    val p = Point { x: 3, y: 4 }
    return manhattan(&p) + p.sum() + helper()   // 14
}
```

```kflat
// src/helper.kf
fun helper(): int32 { return 0 }
```

`helper` needs no import: it is in main.kf's own directory. Drop the import
and call the private `magnitude` directly, and `komp check` reports both:

```kflat
// src/main.kf, without the import
fun main(): int32 {
    val p = Point { x: 3, y: 4 }
    return manhattan(&p) + p.sum() + magnitude(-1)
}
```

```console
src/main.kf:3:12: error: cannot find function `manhattan` in this scope: `shapes.geometry` declares it, and this file does not import it
src/main.kf:3:38: error: cannot find function `magnitude` in this scope: it is private to `shapes.geometry`
```

Imports govern free functions and extension functions. Types, traits,
methods and enum variants are visible without one: `Point` and `p.sum()`
above need no import.

A path always starts with a crate's name, including inside the crate itself;
`import geometry.*` is an error that names the path to write instead.

## Calling through an alias

`import <module> as <name>` names a module instead of its contents. Its
functions are then called as `<name>.<function>(...)`, which says at the
call where the function comes from:

```kflat
import shapes.geometry as geometry

fun main(): int32 {
    val p = Point { x: 3, y: 4 }
    return geometry.manhattan(&p) + helper()   // 7
}
```

An aliased import brings in no bare names: `manhattan(&p)` alone is an
error in this file. The function must still be `pub`, and a call through the
alias reaches only that module, never a same-named function elsewhere —
including one this file declares itself. A local variable of the same name
shadows the alias.

An alias names a module, so `import shapes.geometry.* as g` is an error. An
extension function cannot be called through an alias yet ([#194]).

[#194]: https://github.com/komp-co/komp/issues/194

## Dependencies

If `kf.toml` declares a dependency on `my_lib`, its root module is `my_lib`
and its sub-directories are `my_lib.<dir>`, imported the same way:

```kflat
import my_lib.answer
import my_lib.text.two
```

## Exports

`export <names> from <module>` makes functions of another module part of
this module's surface: whoever imports this module sees them too. A library
can then present one module while its code lives in several:

```
src/
  lib.kf            # export * from my_lib.emit
                    # export read, raw as parse_raw from my_lib.parse
  emit/emit.kf      # pub fun write_it()
  parse/parse.kf    # pub fun read(), pub fun raw(), pub fun other()
```

```kflat
import my_lib.*        // write_it, read and parse_raw, but not other

fun main(): int32 { return write_it() + read() + parse_raw() }
```

The names come first, so `lib.kf` reads as the list of what the crate
offers. `*` exports every `pub` function of the module; a list names some,
each optionally renamed with `as`. A renamed function is known to the
module's users only by its new name — `raw()` is not offered above — while
the library keeps calling it by its own.

`import my_lib.parse_raw` and `import my_lib as lib` (then
`lib.parse_raw()`) work the same way, an export of an export is followed,
and a crate's own modules see its exports too. Moving `read` to another
module then changes only `lib.kf`, not the crate's users.

An export only offers: it does not bring the names into its own file, which
imports them separately if it calls them. Only `pub` functions can be
exported. Exports appear with the imports, before any declarations. An
extension is called through its receiver, so it keeps its name: `as` on one
is an error. `export` and `from` are not reserved words; they are read this
way only at the start of an export.

## pub

`pub` makes a function visible outside its module — to the other modules of
its crate and to dependent crates alike, in both cases through an import.
Without `pub`, a function is visible only inside its own directory.

On a struct or a field, `pub` is not checked yet: a private field can be
read and written from any module and any crate, and naming a dependency's
private struct fails in cc rather than in the checker ([#190]).

[#190]: https://github.com/komp-co/komp/issues/190

## Interface files (.kfi)

When the compiler builds a crate with dependencies, it reads each
dependency's **interface file** — a `.kfi` file produced during compilation
of that dependency. The `.kfi` carries the public declarations, type
signatures, and trait implementations of the depender's view of the crate,
stripped of bodies. This is how separate compilation works: the compiler does
not re-parse the dependency's source, it reads the interface.
