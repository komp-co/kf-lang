# Annotations

Annotations start with `@` and apply to the declaration that follows. KFlat
has `@test`, `@test_disabled`, `@allow(...)`, `@derive(...)`, `@no_mangle`,
`@lang(...)` and `@prelude` built in, and a crate may
[declare its own](#declaring-an-annotation). Any other annotation is an error.

A declaration may carry several, one per line.

A method of an `impl` or a trait may carry `@allow(...)`, which then covers
only that method. Every other built-in annotation is an error on a method, and
so is a declared one: a method is not yet a value that `annotated<A>()` could
hand back.

## Declaring an annotation

`annotation NAME` declares `@NAME`, and `annotated<NAME>()` lists everything
in the crate being compiled that carries it:

```kflat
annotation bench

@bench
fun sort_a_thousand(): void { }

fun main(): int32 {
    while entry in &annotated<bench>() {
        println(entry.name)
        entry.function()
    }
    return 0
}
```

The list is in declaration order, and private declarations are in it, from
any module of the crate. Each entry has the declaration's `name`, the path of
the `module` declaring it, and the use's `args`.

### Parameters

An annotation may take parameters, written like a function's:

```kflat
enum Level {
    Low
    High
}

annotation bench(iterations: int32, label: String, level: Level)

@bench(1000, "sort", Level.High)
fun sort_a_thousand(): void { }

@bench(label = "parse", iterations = 10, level = Level.Low)
fun parse_a_file(): void { }

fun main(): int32 {
    while entry in &annotated<bench>() {
        if entry.args.iterations > 100 {
            println(entry.args.label)
            entry.function()
        }
    }
    return 0
}
```

A use gives every argument, positionally, by name in any order, or
positionally and then by name. Each argument is a literal of its parameter's
type, so a parameter is a number, `bool`, `char`, `String`, or an enum whose
variants carry nothing.

`annotation bench(...)` also declares a struct `bench` with one field per
parameter, and that struct is the type of `entry.args`. An annotation without
parameters declares an empty one. The struct shares the annotation's name,
so nothing else in the crate may be called `bench`.

### Targets

`on` says what an annotation marks. A function type marks functions of
exactly that type, and each entry's `function` is a value of it:

```kflat
struct Request {
    val body: String
}

annotation route(path: String) on (Request) -> int32

@route("/len")
fun length(r: Request): int32 { return r.body.byte_len() as int32 }

fun main(): int32 {
    val routes = annotated<route>()
    return routes.at(0).function(Request { body: "abcd" }) - 4
}
```

`on struct`, `on enum` and `on type` (either one) mark types, and a bound
after `:` requires every marked type to implement those traits:

```kflat
trait Plugin {
    fun start(): int32
}

annotation plugin(order: int32) on struct: Plugin

@plugin(1)
struct Logger {
    val level: int32
}

impl Plugin for Logger {
    fun start(): int32 { return self.level }
}

fun main(): int32 {
    while p in &annotated<plugin>() {
        println("${p.module}.${p.name}, order ${p.args.order}")
    }
    return 0
}
```

A type entry has no value to call: nothing at run time refers to a type.
A generic type cannot carry a type-target annotation, since whether it meets
a bound can depend on its arguments. A bound names a trait without type
arguments.

Without `on`, an annotation marks functions of type `() -> void`. Entries
are an `AnnotatedFunction<bench, F>` or an `AnnotatedType<bench>` from alloc.

### Across modules and crates

An annotation is a name like any other. Another module of the crate uses it
only if it is `pub` and imported, and another crate the same way:

```kflat
// in a library crate named `measure`
pub annotation bench

// in a crate depending on it
import measure.bench

@bench
fun parse_a_file(): void { }
```

The query only reaches the crate it is written in. A `measure` function
calling `annotated<bench>()` sees `measure`'s functions, not the ones of the
crate that imported `bench`.

The name of a built-in annotation cannot be declared. `annotation` and `on`
are not reserved words; they are read this way only in an annotation's
declaration.

## @test

`@test` marks a function for `komp test`:

```kflat
@test
fun addition_works(): void {
    assert_eq(1 + 1, 2, "one plus one")
}
```

It may only annotate a function. See [Writing tests](../tools/testing.md) for
running and filtering tests.

`@test_disabled` also annotates a function, but leaves it out of `komp test`.
It is used by the compiler's test suite for disabled integration tests.

## @derive

`@derive(...)` synthesizes implementations for the listed traits:

```kflat
@derive(Default, Equal)
struct Counter { pub var value: int32 }
```

It may annotate a struct or an enum, though not every trait reaches both:

| Trait | Struct | Enum | Generated behavior |
| --- | --- | --- | --- |
| `Default` | yes | — | Builds a value with each field's default value. |
| `Equal` | yes | yes | Compares every field, or matching enum payloads, with `==`. |
| `Hash` | yes | — | Combines every field's hash. |
| `Clone` | yes | yes | States that copying the value is allowed. |
| `Copy` | yes | yes | States that the value may be copied bitwise; also derives `Clone`. |

Every field used by a derived `Equal` implementation must itself implement
`Equal`.

`Clone` is the odd one: it generates no copying code, because there is none to
generate. The compiler already knows how to deep-copy any type — it synthesizes
that alongside the drop glue. What `@derive(Clone)` adds is the *permission*:
the type now satisfies a `T: Clone` bound, and `.clone()` on it is something
you asked for rather than something that happened to work.

`Copy` is checked where it is derived: every field must itself be `Copy`, and
the type must not implement `Drop`. See [Copy](memory.md#copy).

## @allow

`@allow(...)` silences the named lints for diagnostics inside the declaration
it annotates:

```kflat
@allow(unused_import, unused_variable)
fun scratch(): void { }
```

On a method, it covers that method and nothing else in its `impl`.

The names are the ones a diagnostic reports as its `code`. `lint.toml` sets
the same levels for a whole crate, and `-A`/`-W`/`-D` set them for one build;
the innermost setting wins, so an `@allow` beats both. See
[Linting](../tools/lint.md).

`implicit_copy` and `copy_after_move` — the copies the compiler inserts for you
— are the two worth knowing about, because they are the ones you may
deliberately accept. See [Memory](memory.md).

## @no_mangle

`@no_mangle` keeps a struct's or enum's C name exactly as written, without the
crate prefix komp normally adds, so hand-written C can name it:

```kflat
@no_mangle
pub struct Pair {
    pub var left: int32
    pub var right: int32
}
```

The C type is `Pair`, and its methods are `Pair_<method>`. It may only annotate
a struct or an enum, and two `@no_mangle` types with the same name anywhere in
the program are an error, since their C names would collide.

## @lang

`@lang("key")` is how the standard library tells the compiler which of its
types the language itself builds on. alloc's `String` carries
`@lang("string")`, which makes it the type a string literal becomes, the type
`${...}` renders into, and the type a `str` converts up into.

The compiler knows the keys, not the type names. `string` is the only key so
far.

Only `core`, `alloc` and `std` may use it, only on a struct or an enum, and
each key may be declared once in a program. Your own crates cannot use it:

```console
$ komp check .
./src/main.kf:2:8: error: `@lang(...)` is reserved for the standard library (core, alloc and std)
    struct Text {
           ^~~~
check: found errors
```

## @prelude

`@prelude` marks a function as part of the prelude — the names a program may
use without writing an import. `println`, `assert_eq`, `min`, `range` and the
common `str`/`String` methods are all `@prelude` in the standard library.

Resolution asks the mark, never the crate name: the caller's own module
answers first, then its imports, then any `@prelude` function. A name the
caller's module defines or imports still shadows a prelude name.

Only `core`, `alloc` and `std` may use it, and only on a function. Every
*other* `pub` function in the standard library is no longer ambient — it is
reachable only by importing its module. The prelude is deliberately small;
helpers like `str.last_index_of` or `str.replace` are not in it.
