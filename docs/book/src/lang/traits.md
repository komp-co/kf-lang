# Traits

A trait declares a set of methods that a type must implement. Once a type
implements a trait, any generic function bounded by that trait can call those
methods on the type. Dispatch through a bound is static — monomorphized at
compile time to a direct call. To let the caller choose the implementation at
run time instead, see [trait objects](trait-objects.md).

## Declaring a trait

```kflat
trait Equal {
    fun equals(other: Self): bool
}
```

`Self` in the position of a parameter type stands for the implementing type.
A trait can also declare methods that operate on other types:

```kflat
trait Containable {
    fun contains(item: int32): bool
}
```

Trait names are unique across the program. A declaration cannot reuse the name
of a trait supplied by a dependency such as `core`.

## Implementing a trait

```kflat
struct Point {
    var x: int32
    var y: int32
}

impl Equal for Point {
    fun equals(other: &Point): bool {
        return self.x == other.x && self.y == other.y
    }
}
```

The `for` clause names the type. Every method the trait declares must have a
body in the impl. The method signatures must match exactly.

## Calling through a trait

A generic function with a trait bound works for any type that implements the
trait:

```kflat
fun same<T: Equal>(a: T, b: T): bool {
    return a.equals(b)
}
```

The bound `T: Equal` tells the compiler that `T` has the `equals` method.
The call `a.equals(b)` dispatches directly to the concrete implementation —
no indirection, no vtable. A separate copy of `same` is compiled for each `T`
it is used with; [`&dyn Equal`](trait-objects.md) is the form that compiles
once and decides at run time.

Multiple bounds are separated by `+`:

```kflat
fun describe<T: Equal + Display>(v: T): String {
    // ...
}
```

## Requiring another trait

A trait can require another. `trait Ranked: Named` means no type implements
`Ranked` without also implementing `Named`:

```kflat
trait Named { fun name(): int32 }

trait Ranked: Named { fun rank(): int32 }

struct Card { var n: int32 }

impl Named for Card { fun name(): int32 { return self.n } }
impl Ranked for Card { fun rank(): int32 { return self.n * 10 } }

fun total<T: Ranked>(c: T): int32 { return c.rank() }
```

Several requirements are separated by `+` — `trait Sortable: Compare + Clone`
— and each may carry type arguments, including the declaring trait's own:
`trait Call1<A>: CallMut1<A>`. The separator is the one a bound uses, because
it means the same thing: several constraints on one thing. A comma separates
different things, and writing one here says so:

```console
error: a trait's requirements are separated by `+`, as a bound's are: `trait Sortable: Compare + Clone`
    trait Both: A, B { }
                 ^
```

The requirement is an obligation, not a shortcut: it does not write the `Named`
impl for you. Leaving it out is an error on the impl that is missing it, rather
than at some later call that needed the method:

```console
$ komp check .
src/main.kf:6:1: error: Card implements Ranked, which requires Named — add impl Named for Card
    impl Ranked for Card { fun rank(): int32 { return self.n } }
    ^~~~
```

A requirement cannot loop back on itself. `trait A: B` with `trait B: A` is
rejected at both declarations.

A bound reads through the requirement. `T: Ranked` offers `Named`'s methods as
well as `Ranked`'s, because the impl check has already guaranteed they are
there:

```kflat
fun label<T: Ranked>(c: T): int32 { return c.name() + c.rank() }
```

The trait named in the bound wins a name it shares with one it requires — the
same way a method written on a type wins over one it inherits.

A generic requirement is checked at its arguments, not just by name. `Sink<A>`
requiring `Drain<A>` means an `impl Sink<int32>` needs `Drain<int32>`
specifically:

```kflat
trait Drain<A> { fun drain(a: A): int32 }
trait Sink<A>: Drain<A> { fun sink(a: A): int32 }

struct Bin { var n: int32 }

impl Drain<str> for Bin { fun drain(a: str): int32 { return 0 } }
impl Sink<int32> for Bin { fun sink(a: int32): int32 { return a * 2 } }
```

```console
$ komp check .
src/main.kf:7:1: error: Bin implements Sink<int32>, which requires Drain<int32> — add impl Drain<int32> for Bin
```

`impl Drain<str>` implements the right trait at the wrong type, and the error
says so rather than claiming `Drain` is missing.

A trait that requires several others, and declares nothing of its own, is a
name for a list of bounds — so a signature says what it needs once instead of
repeating it:

```kflat
struct Money { val cents: int32 }

impl Add for Money {
    fun add(other: &Money): Money { return Money { cents: self.cents + other.cents } }
}

impl Compare for Money {
    fun compare(other: &Money): int32 { return self.cents - other.cents }
}

trait Amount: Add + Compare {
}

impl Amount for Money {
}

fun larger_sum<T: Amount>(a: T, b: T, floor: &T): T {
    val total = a + b
    if total.compare(floor) < 0 { return total }
    return total
}

fun main(): int32 {
    val m = larger_sum(Money { cents: 40 }, Money { cents: 2 }, &Money { cents: 0 })
    return m.cents    // 42
}
```

`T: Amount` carries `Add`, so `a + b` is an ordinary addition inside the
function — an operator reads through a requirement exactly as a method does.
The requirement may also be satisfied by a type the crate does not own:
`impl Amount for int32` needs no `impl Add for int32` of its own, because
core already wrote one.

An [associated type](associated-types.md) is inherited the same way. It is
declared once, on the trait that owns it, and named through any bound that
reaches it:

```kflat
trait Producer {
    type Out
    fun make(): Self.Out
}

trait Doubler: Producer {
    fun twice(): Self.Out
}

fun run<T: Doubler>(t: T): T.Out {
    return t.twice()
}
```

Only `impl Producer for Widget` writes a `type Out = ...`; `impl Doubler for
Widget` binds nothing, because `Doubler` declares nothing to bind. Redeclaring
`Out` on `Doubler` is an error rather than an override — one bound would reach
two declarations, and `T.Out` would have no way to pick:

```console
$ komp check .
src/main.kf:7:5: error: trait Doubler redeclares associated type Out, already declared by Producer
    type Out
    ^~~~
```

A trait with requirements cannot be used as a
[trait object](trait-objects.md). See
[limitations](../limitations.md#supertraits).

## Sealed traits

`sealed` before `trait` keeps the set of implementers to the trait's own
crate. Any crate that sees the trait may bound by it and call its methods;
only its crate may write `impl ... for` it.

```kflat
pub sealed trait Endian {
    fun swaps(): bool
}

pub struct Little {}
pub struct Big {}

impl Endian for Little { fun swaps(): bool { return false } }
impl Endian for Big { fun swaps(): bool { return true } }
```

In another crate, `fun read<E: Endian>(order: &E)` is fine, and an impl is
not:

```console
error: `Endian` is sealed: only its crate, `bytes`, implements it
    impl Endian for Middle { fun swaps(): bool { return true } }
    ^~~~
```

Seal a trait when its implementers are a closed list: the choices an API
offers, or a promise that `unsafe` code relies on. It also leaves the trait
free to grow, since adding a method breaks no impl outside the crate. The
boundary is the crate, not the module, so the impls may live anywhere in it.
`sealed` is a word only there; a function or variable may still be named
`sealed`.

## Lending a view: `Deref`

A type that holds or stands for another can lend that other's methods.
Implementing core's `Deref` names a view of the value; `DerefMut` names a view
that can change it:

```kflat
struct Counter {
    var hits: int32
}

impl Counter {
    fun total(): int32 { return self.hits }
    mutating fun bump(): void { self.hits = self.hits + 1 }
}

struct Tracked {
    var counter: Counter
    val label: String
}

impl Deref for Tracked {
    type Target = &Counter
    fun deref(): &Counter { return &self.counter }
}

impl DerefMut for Tracked {
    type TargetMut = &var Counter
    mutating fun deref_mut(): &var Counter { return &var self.counter }
}

fun report(c: &Counter): int32 { return c.total() }

fun main(): int32 {
    var t = Tracked { counter: Counter { hits: 0 }, label: "clicks" }
    t.bump()                      // Counter's, through deref_mut
    val n = t.total()             // Counter's, through deref
    return n + report(&t)         // a &Tracked fills a &Counter slot
}
```

When `x` has no method `m`, by its own impls, its traits or an extension,
`x.m()` is called on `x.deref()` if the target's `m` leaves it unchanged, and
on `x.deref_mut()` if `m` is `mutating`; that one needs `x` to be a `var` or a
`&var` borrow. The receiver's own methods always win, and the lookup follows
a chain of targets. An argument whose slot is the target type is lent the
same way, so a `&Tracked` fills a `&Counter` parameter.

The target is a view or a borrow of the value, checked like any borrow a
method returns. A type's targets are fixed by its impls: `String` lends `str`,
so every `str` method is a `String` method; a `List` and an array lend
`Slice` and `SliceMut`. The other direction is never implicit: a `str` becomes
a `String` only through `String.from`, which copies.

## Trait-qualified calls

When two traits provide methods with the same name, qualify the call with the
trait name:

```kflat
impl A for Point { fun tag(): String { ... } }
impl B for Point { fun tag(): String { ... } }

val p = Point { x: 1, y: 2 }
val a = A.tag(p)    // calls A's impl
val b = B.tag(p)    // calls B's impl
```

The syntax is `Trait.method(value)` — the value moves to the first argument
position.

## The core traits

Several traits are defined in `core` and are used throughout the standard
library:

| Trait | Method(s) | Purpose |
|---|---|---|
| `Clone` | `clone(): Self` | Deep copy |
| `Copy` | — (requires `Clone`) | Values are duplicated by copying their bytes |
| `Drop` | `drop(): void` | Destructor — runs when value goes out of scope |
| `Equal` | `equals(other: Self): bool` | Equality (`==`, `!=`) |
| `Compare` | `compare(other: Self): int32` | Ordering (`<`, `>`, `<=`, `>=`) |
| `Display` | `display(out: &var dyn Write): void` | Rendering (`println`, interpolation, `v.display(): String`) |
| `From<T>` | `static from(value: T): Self` | Explicit value conversion |
| `Deref` / `DerefMut` | `deref()`, `mutating deref_mut()` | Lend a view's methods, and fill its slot; see [above](#lending-a-view-deref) |
| `Default` | `default(): Self` | Default value |
| `Add` / `Sub` / `Mul` / `Div` / `Mod` | `add(...)`, etc. | Arithmetic operators |
| `Call0` through `Call3` | `call(...)` | Callable values with shared captures; a local `f(...)` desugars to `f.call(...)` |
| `CallMut0` through `CallMut3` | `mutating call(...)` | Callable values with mutable or owned captures |

Implementing any of these gives your type the corresponding operator or
standard-library integration. An impl with bounds, such as
`impl Equal for Wrap<T: Equal>`, gives the operator only where they hold, as
it gives the method: `==` on a `Wrap<T>` whose `T` has no `Equal` is an error.
