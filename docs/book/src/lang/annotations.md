# Annotations

Annotations start with `@` and apply to the declaration that follows. KFlat
has `@allow(...)`, `@derive(...)`, `@no_mangle`, `@lang(...)` and `@prelude`
built in, a crate may [declare its own](#declaring-an-annotation), and the
`testing` library declares [`@test` and `@disabled`](#test) this way. Any other
annotation is an error.

A declaration may carry several, one per line.

A method of an `impl` or a trait may carry `@allow(...)`, which then covers
only that method. Every other built-in annotation is an error on a method. A
method of an `impl` may carry a [declared one](#methods); a method of a trait
may not, since it has no body to name.

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

The result is an array with one entry per use, here an
`AnnotatedFunction<bench, () -> void>[1]`, so a query needs no allocator and
runs in a crate that depends on `core` alone. It is in declaration order, and
private declarations are in it, from any module of the crate. A query with no
uses is an empty array. Each entry has the declaration's `name`, the path of
the `module` declaring it, the `file` it is written in, relative to the crate
root (`src/parse/lexer.kf`), the `line` of its name, and the use's `args`.

An entry is a [view](memory.md#view-types): its `name`, `module` and `file`
are `str` borrowed from string literals. The array can be iterated, indexed and lent
as a slice, `&AnnotatedFunction<A, () -> int32>[]`, but a struct cannot hold
an entry; keep `String.from(entry.name)` instead.

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

A use gives its arguments positionally, by name in any order, or
positionally and then by name, as a [call](functions.md#default-values-and-named-arguments)
does. Each argument is a literal of its parameter's type, so a parameter is a
number, `bool`, `char`, `str`, `String`, or an enum whose variants carry
nothing. A `str` parameter borrows its string literal, so a crate on `core`
alone can give an annotation text; the arguments' struct is then a view. A
parameter may have a default, a literal of its type, and a use may leave it
out: `annotation bench(iterations: int32 = 100)` is used as a bare `@bench`.

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

A parameter written `&T` fits where the target says `&var T`, since a shared
borrow can always be lent from a mutable one. The reverse does not fit, and
the entry's `function` keeps the target's type either way.

#### Methods

A method is marked as the function it is as a
[value](functions.md#methods-as-values): its receiver first, `&Type`, or
`&var Type` for a `mutating` method. One `&var Server` target so marks both
kinds, and its entries are called with the receiver:

```kflat
struct Request {
    val path: String
}

struct Server {
    var hits: int32
}

annotation handler(path: String) on (&var Server, Request) -> int32

impl Server {
    @handler("/users")
    fun users(r: Request): int32 { return self.hits }

    @handler("/login")
    mutating fun login(r: Request): int32 {
        self.hits = self.hits + 1
        return 0
    }
}

fun serve(s: &var Server, r: Request): int32 {
    while h in &annotated<handler>() {
        if h.args.path == r.path { return h.function(s, r) }
    }
    return -1
}

fun main(): int32 {
    var s = Server { hits: 0 }
    serve(&var s, Request { path: "/login" })
    println(serve(&var s, Request { path: "/users" }))   // 1
    return 0
}
```

A method's entry is named `Server.users`. A method of a generic type cannot
carry a function-type annotation, since a value has one type.

#### Signature patterns

A function type written with wildcards marks every function it matches. `_`
stands for any one type, and `&_` or `&var _` for a borrow of any type. A
trailing `..` stands for any further parameters, none included:

```kflat
annotation flag on (bool, ..) -> bool     // a bool first, then anything
annotation pair on (bool, _) -> bool      // exactly one more parameter
annotation shown on (int32) -> _          // any result
annotation hook on (&_, bool) -> bool     // a method of any type

@flag
fun alone(on: bool): bool { return on }

@flag
@pair
fun with_label(on: bool, label: String): bool { return on }

@shown
fun label_of(n: int32): String { return "#${n}" }

fun main(): int32 {
    while f in &annotated<flag>() {
        println(f.name)
    }
    return 0
}
```

The functions a pattern matches have different types, so there is no one
type to call them through: the entries are `AnnotatedItem`s, with a name and
no `function`. `..` must come last, and `_` stands only for a whole parameter
or result, not a type argument (`List<_>`). `(..) -> _` matches every
function a value can name.

#### Type parameters

A name in a target that is not a type in scope is a type parameter, as a
name in an [extension's](extensions.md#generic-receivers) receiver is. Each
use binds it to what stands in its place, so where it appears twice, the two
types agree. Bounds go in a list before the annotation's name:

```kflat
struct Request {
    val path: String
}

struct Server {
    var hits: int32
}

trait Counted {
    fun count(): int32
}

impl Counted for Server {
    fun count(): int32 { return self.hits }
}

annotation same on (T, T) -> T
annotation handler on (&var S, Request) -> int32
annotation <T: Counted> counted on (&T) -> int32

@same
fun add(a: int32, b: int32): int32 { return a + b }

impl Server {
    @handler
    mutating fun login(r: Request): int32 {
        self.hits = self.hits + 1
        return 0
    }
}

@counted
fun hits(s: &Server): int32 { return s.count() }

fun main(): int32 {
    var s = Server { hits: 0 }
    while h in &annotated<handler<Server>>() {
        h.function(&var s, Request { path: "/login" })
    }
    println(annotated<same<int32>>().at(0).function(2, 3))   // 5
    println(annotated<counted<Server>>().at(0).function(&s))  // 1
    return 0
}
```

A query may pin every type parameter, as `annotated<handler<Server>>()` does.
It then lists only the uses binding them that way, and for an exact function
type, with no `_` or `..`, their entries are `AnnotatedFunction`s with a
callable `function` of the pinned type. Without pins, the entries are
`AnnotatedItem`s naming every use. Pinning some parameters but not all is an
error, and the order is the bounds list's, then the target's, left to right.

Like `_`, a type parameter stands for a whole parameter or result, or one
behind `&`. A bound is checked at each use, and a type parameter the target
never names is an error. A name close to a type in scope, such as `Pont` with
`Point` declared, is warned about, since it is likelier a typo.

`on` may instead name kinds of declaration: `fun`, `struct`, `enum` and
`trait`, or of member, `field` and `variant`. `on fun` marks every function,
whatever its signature, and `on any` marks the four declaration kinds. Several
kinds are separated by `,`. A struct or an enum
kind may take a bound after `:`, which every marked declaration of that kind
must implement:

```kflat
trait Plugin {
    fun start(): int32
}

annotation plugin(order: int32) on struct: Plugin
annotation hook on <struct: Plugin, enum>
annotation deprecated(since: String) on any

@plugin(1)
@hook
struct Logger {
    val level: int32
}

impl Plugin for Logger {
    fun start(): int32 { return self.level }
}

@hook
@deprecated("0.4")
enum Level {
    Low
    High
}

@deprecated("0.3")
trait Named {
    fun name(): String
}

fun main(): int32 {
    while p in &annotated<plugin>() {
        println("${p.module}.${p.name}, order ${p.args.order}")
    }
    while d in &annotated<deprecated>() {
        if d.kind == AnnotatedKind.Trait {
            println("trait ${d.name}, since ${d.args.since}")
        }
    }
    return 0
}
```

A list of kinds with a bound goes in `<...>`, as a type's generic parameters
do: `on <struct: Plugin + Default, enum>`. A bound on `fun` or `trait` is an
error, since neither implements a trait.

An entry of a kind target has the declaration's `kind`, but nothing to call:
it may name a type, a trait, or a function of any signature. A generic type
cannot carry an annotation whose kind has a bound, since whether it meets the
bound can depend on its arguments. A bound names a trait without type
arguments.

Without `on`, an annotation marks functions of type `() -> void`. Entries
are an `AnnotatedFunction<bench, F>` for a function type, or an
`AnnotatedItem<bench>` for kinds, from [core](../libs/core.md#annotatedfunction-and-annotateditem).

#### Fields and variants

The kinds `field` and `variant` mark a struct's fields and an enum's variants.
They are members, not declarations, so `on any` leaves them out; name them,
as in `on <field, variant>`. A use goes above the member, as it would above a
declaration:

```kflat
annotation skip on field
annotation rename(name: String) on field
annotation tag(code: int32) on variant

struct User {
    @rename("id")
    val user_id: int64
    @skip
    val password: String
}

enum Shape {
    @tag(1)
    Circle(float64)
    Square
}

fun main(): int32 {
    val _user = User { user_id: 7, password: "hunter2" }
    return 0
}
```

A member's uses are checked like any other, arguments and target, and kept
with its type, in a library's interface too. `annotated` does not list them,
since nothing names a field on its own at run time, and querying an
annotation that marks only members is an error. A [template](templates.md)
reads them instead. A built-in annotation cannot mark a member.

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

The name of a built-in annotation cannot be declared. `annotation`, `on`
and `any` are not reserved words; they are read this way only in an
annotation's declaration.

## @test

`@test` marks a function for `komp test`:

```kflat
import testing.test

@test
fun addition_works(): void {
    assert_eq(1 + 1, 2, "one plus one")
}
```

The [`testing`](../libs/testing.md) crate declares it and `@disabled` as
ordinary annotations, so a test file imports the ones it uses, and the crate
names `testing` in its
[`[dev-dependencies]`](../start/projects.md#dev-dependencies):

```kflat
annotation test(name: str = "", panics: FaultKind = FaultKind.NoFault) on () -> void
annotation disabled(reason: str) on () -> void
```

`name` is shown in the report in place of the function's name. `panics` makes
the test pass only when it panics with that kind of
[fault](../libs/core.md#faults), or with any for `FaultKind.AnyFault`:

```kflat
import testing.disabled
import testing.test

@test(name = "an index past the end panics", panics = FaultKind.IndexOutOfBounds)
fun indexes_past_the_end(): void {
    val xs = [1, 2]
    val _x = xs[5]
}

@test
@disabled("waiting on the parser fix")
fun parses_nested_generics(): void { }
```

A `@disabled` test is still checked, so it cannot rot, but it is not run: the
report lists it as ignored, with its reason. A test main is
`run_tests(&annotated<test>(), &annotated<disabled>())`, core's
[runner](../libs/testing.md#running-tests) over the crate's tests. See
[Writing tests](../tools/testing.md) for running and filtering tests.

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
`@lang("string")`, which makes it the type a string literal becomes where an
owned string is needed, the type `${...}` renders into, and the type a `str`
converts up into.

The compiler knows the keys, not the type names.

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
