# Generic and trait receivers

An [extension function](extensions.md) whose receiver is `List<Point>`
extends one type. This chapter covers receivers that extend many: a bare type
parameter, which extends every type, and a trait, which extends every type
implementing it. Either way the extension is [generic](generics.md), so each
receiver type it is called on gets its own compiled copy.

## A name that is not a type

In a receiver, a name that is not a type in scope becomes a type parameter of
the extension: `T` in `fun List<T>.second_or()`. A misspelled type therefore
does not fail. It quietly makes the extension generic. komp warns when the
name is one or two edits from a type in scope:

```console
warning: `Pont` is not a type in scope, so it makes this extension generic over a parameter named `Pont`; did you mean `Point`?
    fun List<Pont>.total_x(): int32 {
             ^~~~
```

## Extensions on every type

A receiver that is a bare type parameter applies to every type. That is how
core's [scope functions](../libs/core.md#scope-functions) are written, and
how you write your own:

```kflat
struct Point {
    val x: int32
}

fun T.let_in<F: Call1<&T>>(f: F): F.Out {
    return f(self)
}

fun main(): int32 {
    val p = Point { x: 5 }
    return p.let_in(|q: &Point| q.x + 1)    // 6
}
```

To bound the parameter, declare it in a list before the receiver — the
receiver is where it is introduced, so that is where it takes its bound. The
extension then applies only to types that meet the bound, and it beats an
unbounded extension of the same name for those types:

```kflat
trait Loud {
    fun volume(): int32
}

struct Horn {
    val x: int32
}

impl Loud for Horn {
    fun volume(): int32 { return 9 }
}

struct Point {
    val x: int32
}

fun <T: Loud> T.level(): int32 { return self.volume() }
fun U.level(): int32 { return 1 }

fun main(): int32 {
    val horn = Horn { x: 0 }
    val p = Point { x: 0 }
    return horn.level() * 10 + p.level()    // 91
}
```

Without the unbounded `U.level`, `p.level()` is an error that names the reason:

```console
error: no method `level` on `Point`: `T.level` needs `Loud`, which `Point` does not implement
```

An extension that returns the receiver itself needs a copy of it, since the
receiver is borrowed, so it bounds its parameter by `Clone`:

```kflat
fun <T: Clone> T.or_else_if<F: Call1<&T>>(other: T, swap: F): T {
    if swap(self) { return other }
    return self.clone()
}

fun main(): int32 {
    return 7.or_else_if(0, |x| *x > 3)    // 0
}
```

## Where a parameter is declared

A receiver introduces its parameters, and a list before the receiver bounds
them. The list after the name keeps its own meaning: the parameters the
function itself declares. Both appear in `or_else_if`, whose `T` is the
receiver and whose `F` is the lambda it is given.

Each parameter therefore has one place, and writing it in the other one says
so:

```console
error: `T` is introduced by the receiver, so the list after the name cannot declare it again; declare it before the receiver instead, as `fun <T: Loud> ...`
    fun T.level<T: Loud>(): int32 { return self.volume() }
                ^
```

```console
error: `U` is not used by the receiver, so it is this function's own type parameter; declare it after the name, as `fun ... .name<U>(...)`
    fun <U> List<T>.count_all(): uint64 { return self.size() }
         ^
```

A bound written inside the receiver is the third way to spell it, and the one
that reads best — but a receiver whose name has no `<...>` has nowhere to put
it, so it is not the form the language took:

```console
error: `Iterable`'s arguments name types, not bounds; declare `T: Add` in a list before the receiver, as `fun <T: Add> ...`
    pub fun Iterable<T: Add>.sum(): T {
                      ^
```

A plain function needs none of this — it has only its own parameters, and
declares them after its name, so a list in front of one is an error.

## Extensions on a trait

A trait in receiver position means every type that implements it:

```kflat
struct Point {
    val x: int32
}

fun Iterable<Point>.total_x(): int32 {
    var total = 0
    while p in self {
        total = total + p.x
    }
    return total
}

fun Iterable<T>.count_all(): int32 {
    var n = 0
    while _item in self {
        n = n + 1
    }
    return n
}

fun main(): int32 {
    var points = List.new<Point>()
    points.push(Point { x: 2 })
    points.push(Point { x: 3 })
    return points.total_x() + points.count_all()    // 5 + 2
}
```

`fun Iterable<Point>.total_x()` is shorthand for a bare parameter bounded by
the trait, `fun <C: Iterable<Point>> C.total_x()`. Write that form when the
body has to name the receiver's type. In the body, `self` has only the
methods the trait provides, so `while p in self` works and `self.size()` does
not:

```console
error: no method `size` on `self`: an extension on `Iterable<Point>` has only that trait's methods
```

A name in the trait's arguments that is not a type is a parameter, as
anywhere in a receiver. Nothing passes a `T` to `count_all`: it is read off
the receiver's impl, so for a `List<Point>` it is `Point`.

The trait's arguments have to match. A `List<int32>` implements
`Iterable<int32>`, not `Iterable<Point>`:

```console
error: no method `total_x` on `List<int32>`: `Iterable<Point>.total_x` needs `Iterable<Point>`, which `List<int32>` does not implement
```

An extension on a trait is less specific than one on a type: a
`fun List<Point>.total_x()` would win for lists. Between two trait receivers,
one whose arguments are all types (`Iterable<Point>`) beats one with a
parameter (`Iterable<T>`).

## In generic code

A value whose type is a type parameter can call an extension that applies to
every type, or one whose bound the parameter carries:

```kflat
trait Loud {
    fun volume(): int32
}

struct Horn {
    val x: int32
}

impl Loud for Horn {
    fun volume(): int32 { return 9 }
}

fun <T: Loud> T.doubled(): int32 { return self.volume() * 2 }

fun report<V: Loud>(v: &V): int32 {
    return v.doubled()
}

fun main(): int32 {
    val horn = Horn { x: 0 }
    return report(&horn)    // 18
}
```

Without the bound on `V`, the call says what to add:

```console
error: no method `doubled` on `V`: `T.doubled` needs `Loud`, which `V` is not bounded by; add it: `V: Loud`
```

## Limitations

- A value known only by its `Iterable` bound is iterated by value.
  `while x in &self` needs a borrowing iterator (`next_ptr()`), which no trait
  declares, so it is an error: drop the `&`.
- `Self` cannot be written in an extension to name the receiver's type. Use
  the explicit form, `fun <C: Trait> C.name()`, and name `C`.
