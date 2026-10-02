# when

`when` is the pattern-matching expression. It matches enums, integers,
booleans, characters and strings by their literal spelling, ranges of
integers and characters, and any other value through a [guard](#guards). The
compiler checks that every case is covered.

## Matching scalars

```kflat
fun describe(n: uint64): str {
    when (n) {
        0 => return "zero"
        1 => return "one"
        2 => return "two"
        _ => return "many"
    }
}
```

Each arm before the `=>` is a literal value. The wildcard `_` catches any
value not matched by earlier arms. An arm after `_` would be rejected — the
wildcard consumes all remaining cases.

Booleans work the same way, and a two-arm `when b { true => ... false => ... }`
is exhaustive — omitting either arm produces an error.

The subject needs no parentheses, exactly like an `if` or `while` condition.
Parentheses are still accepted, since they read as an ordinary parenthesized
expression:

```kflat
when n { ... }      // both spellings are the same grammar
when (n) { ... }
```

## Patterns

An arm can match a literal, a string, a range of values, or several
alternatives at once; [Patterns](patterns.md) covers each form and which arms
the compiler reports as unreachable.

## Guards

An arm can carry an `if` condition. The arm fires only when its pattern
matches **and** the guard holds; otherwise control moves on to the next arm:

```kflat
fun describe(n: int32): str {
    return when n {
        0 => "zero"
        v if v < 0 => "negative"
        v if v > 100 => "large"
        _ => "ordinary"
    }
}
```

A guard sees the arm's own bindings, including an enum payload:

```kflat
fun area(s: Shape): int32 {
    return when s {
        Circle(r) if r > 100 => 0     // too big to bother with
        Circle(r) => r * 3
        Square(w) => w * w
    }
}
```

A guard is any `bool` expression. It can call a method on the binder, or
compare it with `==` through the type's `Equal`, which is how a `when` matches
a value that no literal pattern can spell:

```kflat
fun classify(p: Point, target: &Point): int32 {
    return when p {
        v if v == *target => 1
        v if v.is_origin() => 2
        _ => 3
    }
}
```

A guarded arm covers nothing for exhaustiveness purposes — its condition may
be false at runtime — so the `when` above still needs the unguarded
`Circle(r)` arm. Dropping it is an error, not a silent fallthrough.

## Matching enums

When the subject is an enum, each arm names a variant. A variant with a
payload binds the payload to a new name:

```kflat
enum Shape {
    Circle(int32)
    Square(int32)
    Empty
}

fun area(s: &Shape): int32 {
    when (s) {
        Circle(r) => { return r * 3 }
        Square(w) => { return w * w }
        Empty => { return 0 }
    }
    return -1
}
```

The compiler checks exhaustiveness: if `Empty` were missing, the compiler
would reject the `when`. A wildcard arm also catches all remaining variants.

A variant whose payload has no values cannot be built, so it needs no arm.
`Ok` of a `Result<Never, E>` is one; so is any variant holding an enum with no
variants:

```kflat
import core.traits.Never

fun widen<E>(r: Result<Never, E>): Result<bool, E> {
    when (r) {
        Err(e) => { return Result.Err<bool, E>(e) }
    }
}
```

## The lowercase-binds rule

A bare name in a pattern arm that starts with a lowercase letter **binds the
subject** rather than naming a variant. A name starting with an uppercase
letter names a variant:

```kflat
enum Colour {
    Red
    Green
    Blue
}

fun rank(c: Colour): int32 {
    when (c) {
        Red => { return 1 }       // matches the Red variant
        other => {                // binds c to the name `other`
            when (other) {
                Green => { return 2 }
                Blue => { return 3 }
                _ => { return 0 }
            }
        }
    }
    return 0
}
```

This is how you destructure enums in layers: the outer `when` peels off one
variant, the inner `when` dispatches the remainder through a binding.

Note the `_` arm on the inner `when`. Binding does **not** narrow the type:
`other` is still a `Colour`, so the exhaustiveness check still demands `Red`
even though the outer arm already handled it. Every binding-and-rematch needs
a wildcard or a dead arm for the variants above it.

## Matching a borrowed enum

A `when` on a borrow (`&Enum`) matches through the borrow. The payload is a
borrow too:

```kflat
fun area(s: &Shape): int32 { ... }

val c = Shape.Circle(4)
area(&c)    // borrow, not move
```

The subject is not consumed, so the enum remains alive after the `when`.

## Changing a payload in place

When an arm writes through a binder, the binder is the payload itself. A
write means calling a `mutating` method on it, storing into one of its
fields, or borrowing it `&var`. The subject must be a place you may write:
a `var`, a field of one, or a `&var` borrow. The change is then made in the
subject:

```kflat
struct Counter {
    var n: int32
}

impl Counter {
    mutating fun bump(): int32 {
        self.n = self.n + 1
        return self.n
    }
}

struct Holder {
    var counter: Counter?
}

impl Holder {
    mutating fun bump(): int32 {
        return when (self.counter) {
            Some(c) => c.bump()
            None => -1
        }
    }
}
```

Calling `bump` twice on a `Holder` returns `2` the second time.

A binder the arm only reads is still a copy. So is one of a `val` subject,
or of a temporary such as a call's result, where there is no place to
change.

## when as a value

A `when` produces a value in a binding or a `return`. Each arm's value is the
last statement of its body, so an arm may do work before yielding:

```kflat
val cost = when (kind) {
    1 => {
        val base = lookup()
        base + 1
    }
    _ => { 0 }
}
```

That last statement may itself be an `if` or another `when`, in which case the
arm yields through each of those branches in turn:

```kflat
val cost = when (kind) {
    1 => {
        val base = lookup()
        if wide { base * 2 } else { base }
    }
    _ => { 0 }
}
```

The arms must agree on a type, and the result is the widest of them — the same
rule [`if` as a value](control-flow.md#if-as-a-value) follows, because it is
the same question with a different number of paths.

An arm that yields nothing — one ending in a binding, or in an `if` with no
`else` — is an error, since the `when` as a whole would have no value on that
path.

An arm that never finishes needs no value. It may end in `return`, `break` or
`continue`, or in a call that cannot return, such as `panic` or a function
returning [`Never`](functions.md#functions-that-never-return):

```kflat
while x in &xs {
    val half = when (x % 2) {
        0 => x / 2
        _ => continue
    }
    total = total + half
}
```

Such an arm is typed `Never`, which joins any other arm's type. The same holds
for a branch of [`if` as a value](control-flow.md#if-as-a-value).
