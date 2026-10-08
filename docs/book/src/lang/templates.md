# Templates

A template is code an annotation adds beside each struct or enum it marks. It
is written in KFlat, in the library that declares the annotation, and read over
the struct's fields, or the enum's variants, while the program compiles:

```kflat
annotation fields_equal on struct

template fields_equal on struct T {
    impl Equal for T {
        fun equals(other: &T): bool {
            while field in T.fields {
                if field.of(self) != field.of(other) { return false }
            }
            return true
        }
    }
}

@fields_equal
struct Point {
    val x: int32
    val y: int32
}

fun main(): int32 {
    val a = Point { x: 1, y: 2 }
    val b = Point { x: 1, y: 2 }
    return if a == b { 0 } else { 1 }
}
```

`Point` gets an `impl Equal` comparing `x`, then `y`, as if it were written by
hand beside the struct. It is then checked like any other code.

## Declaring one

`template NAME on struct T { declarations }` belongs to the annotation `NAME`,
which must be declared in the same crate and mark structs. `T` names the
marked struct inside the template. `on enum T` is the same for enums, as
[below](#enums) says. An annotation marking both may have one template of each,
but at most one per kind, or per kind and [key](#keyed-templates).

The template holds ordinary declarations: trait impls, `impl T` blocks, and
extension functions such as `fun T.describe()`. `template`, like `annotation`,
is a keyword only at the head of the declaration.

A template only adds. It never changes the struct it marks, and a trait impl
or method it adds that the struct already has by hand is an error naming both.

## Member loops and facts

`while field in T.fields` runs while the program compiles, not when it runs.
It becomes one block per field, in order, with `field` standing for that field.
In each block:

| Written | Becomes |
|---|---|
| `field.of(self)` | the field read from `self`, `self.x` |
| `field.name` | its name as a string, `"x"` |
| `field.index` | its position, `0` |
| `field.line` | the line it is written on |
| `field.type` (in a type) | its type, `int32` |
| `field.has<A>()` | whether the field carries `@A` |
| `field.get<A>()` | `@A`'s arguments on the field, as `A`'s argument struct |

These facts are declared in core, in `libs/core/src/template.kf`: `field` is
a `TemplateField`, and the enum facts [below](#enums) are a `TemplateVariant`
and a `TemplatePayload`, with `site` a `TemplateSite`. They are never built:
each fact becomes a literal or code as the template expands. The declarations
are there to be read, and an editor uses them inside a template, which is never
checked as written: hovering `field` shows its type, and completion after
`field.` lists its facts.

Outside a loop, `T.name` is the struct's name, `T.fields.size()` the number of
fields, and `args` the arguments the annotation's use was written with.
`site.file`, `site.line` and `site.column` are where the struct's name is
written, the file relative to the crate root as an `annotated<A>()` entry gives
it, `src/user.kf`. They are literals, so generated code can say where the
struct is without anything computed when it runs:

```kflat
annotation located on struct

template located on struct T {
    fun T.where(): String { return "${T.name} at ${site.file}:${site.line}" }
}

@located
struct User {
    val id: int64
}

fun main(): int32 {
    val user = User { id: 1 }
    return if user.where() == "User at src/main.kf:8" { 0 } else { 1 }
}
```

`T.build(|field| e)` is a literal of the marked struct, `e` written once per
field with `field` standing for it: `T { x: e(x), y: e(y) }`. Inside a member
loop the two binders name different fields, so a template can rebuild a value
with one field changed:

```kflat
annotation rebuilt on struct

template rebuilt on struct T {
    impl T {
        while field in T.fields {
            fun with_${field.name}(value: field.type): T {
                return T.build(|other| if other.index == field.index { value } else { other.of(self) })
            }
        }
    }
}

@rebuilt
struct Point {
    val x: int32
    val y: int64
}

fun main(): int32 {
    val p = Point { x: 1, y: 2 }.with_y(40)
    return p.x + p.y as int32 - 41
}
```

The condition is facts alone, so for each field only one branch is written:
`with_y` builds `Point { x: self.x, y: value }`, and the other branch, an
`int64` where an `int32` goes, is never checked.

### Mapping and filtering the fields

`T.fields.map(|field| e)` writes `e` once per field into an array literal,
`[e(x), e(y)]`. An array is `Iterable`, so `all`, `any`, `count` and `fold`
take it, and nothing is allocated, in a crate on `core` alone too.
`T.fields.all(|field| c)` is shorter for a condition: `c(x) && c(y)`, and
`true` with no fields; `any` joins them with `||`, and is `false` with none.
`T.fields.filter(|field| c)` keeps the fields `c` holds for, and `size()`
counts them:

```kflat
annotation hidden on field
annotation compared on struct

template compared on struct T {
    impl Equal for T {
        fun equals(other: &T): bool {
            return T.fields.map(|field| field.of(self) == field.of(other)).all(|same: bool| same)
        }
    }

    fun T.shown_total(): int64 {
        val shown = T.fields.filter(|field| !field.has<hidden>()).map(|field| field.of(self) as int64)
        return shown.fold(0 as int64, |sum: int64, x: int64| sum + x)
    }
}

@compared
struct Point {
    val x: int32
    @hidden
    val secret: int32
    val y: int64
}

fun main(): int32 {
    val a = Point { x: 1, secret: 99, y: 2 }
    if !(a == a) { return 1 }
    return (a.shown_total() - 3) as int32
}
```

Each copy of `e` is checked on its own, so `field.of(self)` may have a
different type for each field, but the copies must share one type: they are
one array's elements. Every element is evaluated before an adapter sees any,
so each field is compared even when an earlier one already made `all` false;
a member loop that returns early is the choice when that matters.

`filter` decides which fields are kept while the program compiles, so its
condition is facts alone: `has<A>()`, `name` and `index`. A member loop may
walk a filter too, `while field in T.fields.filter(|field| !field.has<hidden>())`.

An `if` or `when` whose condition is made of these facts alone is decided while
the program compiles: only the branch taken is kept, so the other need not make
sense for that field. `get<A>()` on a field without `@A` is an error, so guard it
with `has<A>()`:

```kflat
annotation described(prefix: String) on struct
annotation hidden on field
annotation caption(text: String) on field

template described on struct T {
    fun T.describe(): String {
        var out = "${args.prefix}${T.name} with ${T.fields.size()} fields:"
        while field in T.fields {
            if !field.has<hidden>() {
                val shown = if field.has<caption>() { field.get<caption>().text } else { field.name }
                out.append(" ${field.index}=${shown}")
            }
        }
        return out
    }
}

@described("user ")
struct User {
    val id: int64
    @caption("login")
    val name: String
    @hidden
    val password: String
}

fun main(): int32 {
    val user = User { id: 7, name: "ann", password: "hunter2" }
    println(user.describe())
    return 0
}
```

```console
user User with 3 fields: 0=id 1=login
```

Any other variable in a template is an ordinary one, living while the program
runs. `field` itself is not a value: use one of its facts, or read it with
`field.of(x)`. That is a place as well as a value, so `field.of(out) = value`
assigns to `out`'s field.

A member loop is written out once per field rather than run, but `break` and
`continue` act as in any loop. One the facts decide, such as
`if field.has<last>() { break }`, ends the copies there, and nothing of it is
left in the program. One decided while the program runs,
`if field.of(self) == 0 { break }`, skips the copies after it, or, for
`continue`, the rest of this one. A `break` or `continue` in a loop of its own
inside the member loop belongs to that loop.

### Where a member loop goes

A member loop may also stand where declarations go, among a template's
declarations or an `impl`'s methods, and among the fields of a struct the
template adds. What it holds is then written once per field:

```kflat
annotation accessors on struct

template accessors on struct T {
    struct ${T.name}Parts {
        while field in T.fields {
            val ${field.name}: field.type
        }
    }

    impl T {
        while field in T.fields {
            fun get_${field.name}(): field.type { return field.of(self) }
        }
    }
}

@accessors
struct Point {
    val x: int32
    val y: int32
}

fun main(): int32 {
    val p = Point { x: 3, y: 4 }
    val parts = PointParts { x: p.get_x(), y: p.get_y() }
    return parts.x + parts.y - 7
}
```

`Point` gets `get_x` and `get_y`, and `PointParts` a field for each of
`Point`'s. A name written out per field is built from `field.name`, as
[below](#names-built-from-facts) says, or every copy would have the same one.
A member loop inside another is an error, wherever either stands.

## Enums

A template `on enum T` walks `T.variants`. A member loop may also stand among
a `when`'s arms, where it writes its arms once per variant. In an arm's
pattern, `${variant.name}(..a)` matches the variant and binds each of its
payloads, which `payload.of(a)` reads; with no payloads it is `${variant.name}`
alone. `variant.payloads` is a list as `T.fields` is: loop over it, or `map`,
`filter`, `all`, `any` or `size()` it.

```kflat
annotation same on enum

template same on enum T {
    impl Equal for T {
        fun equals(other: &T): bool {
            return when (self) {
                while variant in T.variants {
                    ${variant.name}(..a) => when (other) {
                        ${variant.name}(..b) => variant.payloads.all(|payload| payload.of(a) == payload.of(b))
                        _ => false
                    }
                }
            }
        }
    }

    fun T.shown(): String {
        var out = String.from("")
        when (self) {
            while variant in T.variants {
                ${variant.name}(..a) => {
                    out.append(variant.name)
                    while payload in variant.payloads {
                        out.append(if payload.index == 0 { "(" } else { ", " })
                        out.append("${payload.of(a)}")
                    }
                    if variant.payloads.size() > 0 { out.append(")") }
                }
            }
        }
        return out
    }
}

@same
enum Shape {
    Circle(int32)
    Rect(int32, int32)
    Empty
}

fun main(): int32 {
    val a = Shape.Rect(1, 2)
    if !(a == Shape.Rect(1, 2)) || a == Shape.Empty { return 1 }
    return if a.shown() == "Rect(1, 2)" && Shape.Empty.shown() == "Empty" { 0 } else { 2 }
}
```

A variant has the facts `name`, `index`, `line`, `has<A>()` and `get<A>()`, and a
payload `index`, and `type` where a type goes. `variant.build(|payload| e)`
makes the variant with `e` per payload, `T.Rect(e(0), e(1))`, and
`T.${variant.name}` names one without payloads. A `return` the facts decide
ends a member loop's copies, as `break` does, so a `Default` can return the
first variant without payloads:

```kflat
annotation zero on enum

template zero on enum T {
    impl Default for T {
        static fun default(): T {
            while variant in T.variants.filter(|variant| variant.payloads.size() == 0) {
                return T.${variant.name}
            }
        }
    }
}

@zero
@derive(Equal)
enum Shape {
    Circle(int32)
    Empty
}

fun main(): int32 { return if Shape.default() == Shape.Empty { 0 } else { 1 } }
```

An enum with no such variant gets a `default` that does not return, which is
reported with a note naming the enum.

## Arguments as code

`args.p` is the value the annotation's use gave `p`. A parameter taking a
[name](annotations.md#names-and-the-rest) gives code: `args.by(x)` calls the
function it names, `args.with` in a type is the type, and `${args.t}` writes
the name where one goes, so `impl ${args.t} for T` implements the trait. A
variadic argument is a list, and a loop over it is written out once per value,
like a member loop, with the binder standing for that value:

```kflat
annotation checked(rules: fun..) on struct

fun positive(n: int64): bool { return n > 0 }
fun small(n: int64): bool { return n < 100 }

template checked on struct T {
    fun T.valid(): bool {
        while field in T.fields {
            while rule in args.rules {
                if !rule(field.of(self)) { return false }
            }
        }
        return true
    }
}

@checked(positive, small)
struct Reading {
    val level: int64
}

fun main(): int32 {
    val ok = Reading { level: 5 }
    val high = Reading { level: 500 }
    return if ok.valid() && !high.valid() { 0 } else { 1 }
}
```

`rule(field.of(self))` becomes `positive(self.level)`, then `small(self.level)`.
A function an argument names is called as the use wrote it, so a private one in
the user's module is reached, wherever the template is written. A loop over an
argument goes where statements go; to add declarations for each name, a
template is [keyed](#keyed-templates) on it.

## Keyed templates

An annotation with one parameter taking a declaration's name may have a
template for each name a use can give, written with the name as its key:

```kflat
annotation deriving(traits: trait..) on <struct, enum>

trait Counted {
    fun count(): int64
}

trait Named {
    fun label(): String
}

template deriving(Counted) on struct T {
    impl Counted for T {
        fun count(): int64 { return T.fields.size() }
    }
}

template deriving(Named) on struct T {
    impl Named for T {
        fun label(): String { return String.from(T.name) }
    }
}

@deriving(Counted, Named)
struct Point {
    val x: int32
    val y: int32
}

fun main(): int32 {
    val p = Point { x: 1, y: 2 }
    return if p.count() == 2 && p.label() == "Point" { 0 } else { 1 }
}
```

A use expands the keyed template of each name it lists. A keyed template
lives in the crate declaring its key, as an impl lives with its trait, not
necessarily beside its annotation: a crate declaring a trait can add its own
template for another crate's `deriving`, and a use reaches it through that
crate. Once an annotation has keyed templates, a name a use lists without one
for the kind it marks is an error at the name, "`Named` has no `deriving`
template on `enum`". A template is worth keying when what it adds differs in
shape per name, such as the trait it implements; when only what it calls
differs, one unkeyed template calls the name from `args`.

## Where the code lives

What a template adds belongs to the marked struct's module: an extension
function it adds is called there without an import. Names in the template
resolve where the template is written, so it can call its own module's
private helpers, and a user of the annotation imports only the annotation.

A template is written in the crate declaring its annotation and travels in
that crate's interface, so every crate importing the annotation expands it for
its own structs:

```kflat
// in a library crate named `weigh`
pub trait Weighed {
    fun weight(): int64
}

pub annotation weighed(scale: int64 = 1) on struct
pub annotation skip on field

fun scaled(n: int64, scale: int64): int64 { return n * scale }

template weighed on struct T {
    impl Weighed for T {
        fun weight(): int64 {
            val kept = T.fields.filter(|field| !field.has<skip>()).map(|field| field.of(self) as int64)
            return scaled(kept.fold(0 as int64, |sum: int64, x: int64| sum + x), args.scale)
        }
    }
}

// in a crate depending on it
import weigh.Weighed
import weigh.skip
import weigh.weighed

@weighed(10)
struct Parcel {
    val a: int32
    @skip
    val b: int32
    val c: int64
}
```

`Parcel` gets an `impl Weighed` whose `weight()` is 30. The template calls
`scaled`, private to `weigh`, as code written in `weigh` would, and the crate
using it still cannot: its own names are its own. What the template asks of
the program is about the crate it expands into, so `annotated<A>()` in its
code lists that crate's uses. An error in the code it adds for a field is
reported at the field, with a note showing the template's source.

## Names built from facts

`${...}` inside a name builds it from facts, as it does inside a string
literal: `${T.name}Summary` is `PointSummary` for `Point`. So what a template
adds can be named after the struct it marks, and two marked structs do not
collide:

```kflat
annotation summarized(verb: str) on struct

template summarized on struct T {
    struct ${T.name}Summary {
        val count: int64
    }

    fun T.${args.verb}_summary(): ${T.name}Summary {
        return ${T.name}Summary { count: T.fields.size() as int64 }
    }
}

@summarized("make")
struct Point {
    val x: int32
    val y: int32
}

@summarized("take")
struct Size {
    val width: int32
    val height: int32
}

fun main(): int32 {
    val p: PointSummary = Point { x: 1, y: 2 }.make_summary()
    val s: SizeSummary = Size { width: 3, height: 4 }.take_summary()
    return (p.count + s.count - 4) as int32
}
```

A name is built from `T.name`, a member loop's `field.name` or
`variant.name`, and text in
`args`; nothing else, and no casing or other change to them. Any name in a
template may be built: a declaration, a parameter or a field, a variable, and
a name that refers to one of them, in a type too. A type is never built from a
field: `field.type` is the field's type. Outside a template a built name is an
error, since nothing writes it out.

## Errors

An error in code a loop added for one field is reported at that field, with a
note pointing at the template:

```console
src/main.kf:20:9: error: operator requires `impl Equal for Handle`
        val handle: Handle
            ^~~~~~
  = note: in what `@fields_equal`'s template adds for field `Holder.handle` (at src/main.kf:3:10)
      template fields_equal on struct T {
               ^~~~~~~~~~~~
```

Any other error in a template's code is reported in the template, with a note
naming the struct it was adding to.

### Reporting from a template

A template can say what is wrong in its own words. `compile_error(message)`
stops the build and `compile_warning(message)` warns, both while the template
expands, at the field or variant a member loop is on, or else at the marked
struct or enum:

```kflat
annotation summed on struct
annotation label on field

template summed on struct T {
    fun T.total(): int64 {
        var sum: int64 = 0
        while field in T.fields {
            if field.has<label>() {
                compile_error("`${field.name}` is a label, which `@summed` cannot add")
            } else {
                sum = sum + field.of(self) as int64
            }
        }
        if T.fields.size() > 8 {
            compile_warning("`${T.name}` sums ${T.fields.size()} fields")
        }
        return sum
    }
}

@summed
struct Point {
    val x: int32
    val y: int32
}

fun main(): int32 { return (Point { x: 1, y: 2 }.total() - 3) as int32 }
```

Only a branch the facts decide may hold one, so it reports for the fields
whose facts take it: `@label` on a field of `Point` would stop the build there.
In a branch decided when the program runs, or with a message that is not
literals and facts, either is an error, and outside a template neither exists.
A warning is the lint `template_warning`, which `@allow` and `lint.toml` turn
down like any other.

## Limits

A template does not add to a generic struct or enum. A `pub` extension function a
template adds cannot be imported by another module. [Limitations](../limitations.md#templates)
lists each with its issue.
