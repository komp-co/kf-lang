# Extension functions

An extension function adds a method to a type you did not write. It is
declared at the top level with the receiver type before its name, and called
like any method:

```kflat
struct Point {
    val x: int32
    val y: int32
}

fun Point.manhattan(): int32 {
    return self.x + self.y
}

fun int32.squared(): int32 {
    return self * self
}

fun main(): int32 {
    val p = Point { x: 2, y: 3 }
    return p.manhattan() + 4.squared()    // 5 + 16
}
```

Inside the body, `self` is the receiver. For a struct or enum, `self` is a
`&Receiver` borrow, so calling an extension never moves or copies the value.
Numbers, `bool`, `char`, `str` and `Ptr` are cheap to copy, so for those
`self` is the value itself.

An extension is a function, not part of the type. It cannot read private
fields a function in the same module could not read. It cannot make a type
implement a trait, so a generic function bounded by `T: Shape` still needs an
`impl Shape`. It is resolved at compile time, so it does not work through a
[trait object](trait-objects.md).

## Mutating the receiver

`mutating` gives the extension a `&var` borrow of its receiver, which then
has to be writable where it is called, as for a
[mutating method](functions.md#mutating-methods):

```kflat
struct Counter {
    var n: int32
}

mutating fun Counter.bump(by: int32): void {
    self.n = self.n + by
}

fun main(): int32 {
    var c = Counter { n: 1 }
    c.bump(4)
    return c.n    // 5
}
```

With `val c`, the call is an error: `cannot mutably borrow an immutable place`.

## Generic receivers

A receiver can name a specific instance of a generic type, or be generic
itself. A name in the receiver that is not a type in scope becomes a type
parameter of the extension, as in `impl List<T>`:

```kflat
import alloc.list.*

struct Point {
    val x: int32
    val y: int32
}

fun List<Point>.total_x(): int32 {
    var total = 0
    var i: uint64 = 0
    while i < self.size() {
        total = total + self.at(i).x
        i = i + 1
    }
    return total
}

fun <T: Copy> List<T>.second_or(fallback: T): T {
    if self.size() < 2 { return fallback }
    return self.get(1)
}

fun main(): int32 {
    var points = List.new<Point>()
    points.push(Point { x: 2, y: 0 })
    points.push(Point { x: 3, y: 0 })
    var numbers = List.new<int32>()
    numbers.push(7)
    return points.total_x() + numbers.second_or(0)    // 5 + 0
}
```

`total_x` applies only to `List<Point>`; `second_or` applies to any list of a
`Copy` element. A bound on one of the receiver's own parameters is written in a
list before the receiver, as `second_or` does. Type parameters the function
needs beyond the receiver's are declared after its name, as for any function:
`fun List<T>.pair_with<U>(other: U)`.

A receiver can also be a bare parameter, which extends every type, or a
trait, which extends every type implementing it. Both are covered in
[Generic and trait receivers](extension-receivers.md).

## Which method a call gets

When a call could mean several methods, the most specific wins:

1. the type's own method, from its `impl`;
2. a method of a trait the type implements;
3. an extension. Among extensions, one for a specific type (`List<Point>`)
   beats one generic in its arguments (`List<T>`), which beats one for a bare
   parameter or a trait (`T`, `Iterable<Point>`). A bound makes a parameter
   more specific than an unbounded one in the same position, and a bound whose
   arguments are all types (`Iterable<Point>`, `T: Loud`) more specific than
   one with a parameter in them (`Iterable<T>`).

So an extension never changes the meaning of a call that already worked:

```kflat
struct Point {
    val x: int32
}

impl Point {
    fun label(): int32 { return 1 }
}

fun Point.label(): int32 { return 2 }

fun main(): int32 {
    return Point { x: 0 }.label()    // 1: the type's own method
}
```

Two extensions that apply equally well are an error, not a guess:

```console
error: ambiguous extension `tag` on `Point`: `T.tag`, `U.tag` all apply; import only the one you mean
```

## Calling one as a function

An extension can also be called as a function, with the receiver as the first
argument. That is the way to pick one explicitly:

```kflat
struct Point {
    val x: int32
    val y: int32
}

fun Point.manhattan(): int32 {
    return self.x + self.y
}

fun main(): int32 {
    val p = Point { x: 2, y: 3 }
    return manhattan(&p)    // 5
}
```

The first argument's type chooses among extensions of that name, by the same
rules as a method call. An ordinary function of the same name takes
precedence over the extensions.

## Across crates

An extension is visible where a function of the same name would be: in its
own module, in other modules of its crate when it is `pub`, and in other
crates through an import. The import names the extension the way callers
write it:

```kflat
import geometry.Point
import geometry.manhattan

fun main(): int32 {
    return Point { x: 1, y: 2 }.manhattan()
}
```

Without the import, `manhattan` is not a method of `Point` in this file.

## Limitations

- An extension cannot be called through a [module alias](modules.md#calling-through-an-alias)
  such as `geometry.manhattan(&p)` ([#194](https://github.com/komp-co/komp/issues/194)).
  Two imported extensions that tie are separated by importing only one.
