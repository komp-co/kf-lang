# Structs

A struct declares named fields. Each field has a name and a type.

## Declaring a struct

```kflat
struct Point {
    var x: int32
    var y: int32
}
```

One field per line. There is no separator between fields — not a comma and not
a semicolon — so a struct cannot be written on a single line. The same is true
of enum variants.

Fields are mutable by default when the struct itself is in a `var` binding.
You can mark a field `pub` to export it from the crate:

```kflat
pub struct Point {
    pub val x: int32    // both struct and field are public
    pub val y: int32
}
```

A `pub` struct with a private field is visible outside the crate but the
caller cannot access the private field directly. A struct without `pub` is
entirely crate-internal — even its public fields are invisible to dependents.

## Construction

A struct literal is the type name followed by `{ field: value, ... }`.
Fields may be separated by commas or newlines:

```kflat
val origin = Point { x: 0, y: 0 }

val unit = Point {
    x: 1
    y: 1
}
```

Every declared field must appear exactly once, and every initializer must have
the field's declared type. A literal with an unknown, duplicate, or missing
field is an error.

Struct literals work anywhere an expression is expected — as a function
argument, in a condition, as a return value:

```kflat
fun distance(p: Point): int32 { ... }

val d = distance(Point { x: 1, y: 2 })
```

In a condition, wrap the literal in parentheses so the `{` after it is read
as the body opener:

```kflat
if take_node(Node { v: 7 }) != 7 { ... }
```

## impl blocks

Methods live in `impl` blocks on the struct. The first parameter is `self`,
which is the receiver and is written implicitly:

```kflat
impl Point {
    fun norm(): int32 {
        return self.x * self.x + self.y * self.y
    }
}
```

Inside a method, `self.x` (or `self.y`) reads a field, and `self` refers to
the whole struct. A non-`mutating` method sees `self` as immutable; to write
fields, declare the method `mutating fun`:

```kflat
impl Point {
    mutating fun translate(dx: int32, dy: int32): void {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}
```

A value method call checks its argument count and types just like a free
function call. Calling a name the receiver does not provide is an error.

A `static` method takes no `self` and is called on the type name:

```kflat
impl Point {
    pub static fun origin(): Point {
        return Point { x: 0, y: 0 }
    }
}

val p = Point.origin()
```

### The target has to be a type this crate declares

An `impl` block writes its methods into the target's namespace, so the target
has to name a struct or an enum, and it has to be one this crate declares:

```kflat
impl Greet { }        // error: `Greet` is a trait, not a type
impl NoSuchThing { }  // error: no type named `NoSuchThing`
impl String { }       // error: `String` is declared in crate `core`
```

A trait is the way to add a method to a type you do not own — including the
builtins, which no crate declares:

```kflat
trait Shout {
    fun shout(): String
}

impl Shout for String {
    fun shout(): String { return self.clone() }
}
```

## Private type, public method

A crate-private struct can still export methods. The method is visible in
dependent crates, but the type it is defined on is not — callers can invoke
it through a value they obtained, but they cannot name the type themselves:

```kflat
struct Widget {
    pub val value: int32
}

impl Widget {
    pub fun get(): int32 {
        return self.value
    }
}
```
