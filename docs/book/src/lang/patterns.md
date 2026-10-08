# Patterns

The left side of a `when` arm is a pattern. Enum variants and the
lowercase-binds rule are covered with [`when`](when.md); this chapter covers
the patterns that compare a value.

## Literals

An arm can match an integer, a negative integer, a boolean, or a character:

```kflat
fun step(delta: int32): str {
    return when delta {
        -1 => "back"
        0 => "still"
        1 => "forward"
        _ => "far"
    }
}
```

A literal pattern must be of the subject's own kind: an integer pattern needs
an integer subject, `'a'` a `char`, and `true` a `bool`. Anything else is
rejected, naming both:

```kflat
fun main(): int32 {
    val s = String.from("abc")
    return when s {
        1 => 5
        _ => 0
    }
}
```

```console
$ komp check .
src/main.kf:4:9: error: this pattern is an integer, but the subject is `String`
```

There is deliberately no float literal pattern: matching on float equality is
a trap, and no language worth copying allows it.

## Strings

A string literal matches a `str`, a `String`, or a borrow of either, by
content:

```kflat
fun code(verb: str): int32 {
    return when verb {
        "get" => 1
        "put" => 2
        _ => 0
    }
}
```

No set of strings is ever complete, so a string `when` always needs `_` or a
binding. Like any other arm, a string arm may carry a guard (`"get" if loud`).
An interpolated string is not a pattern.

A string pattern on anything else is rejected:

```kflat
fun main(): int32 {
    val n = 3
    return when n {
        "three" => 1
        _ => 0
    }
}
```

```console
$ komp check .
src/main.kf:4:9: error: this pattern is a string, but the subject is `int32`
```

## Ranges

A range matches every value between its bounds. `..` stops before its upper
bound, as it does in a loop; `..=` includes it:

```kflat
fun escape_class(byte: uint8): int32 {
    return when byte {
        34 => 1
        92 => 2
        0..32 => 3
        127..=255 => 4
        _ => 0
    }
}
```

Both bounds are integer literals, negative ones included (`-1000..0`), or both
are characters (`'a'..='z'`). A range that matches nothing is rejected:

```console
$ komp check .
src/main.kf:4:9: error: this range matches nothing: its upper bound is not above its lower bound
```

A range is compiled to a single `case low ... high:` label, so it costs no
more than one literal arm.

## Alternatives

`|` gives several patterns one body:

```kflat
enum Op {
    Plus
    Minus
    Star
    Slash
}

fun precedence(op: Op): int32 {
    return when op {
        Plus | Minus => 1
        Star | Slash => 2
    }
}

fun is_vowel(c: char): bool {
    return when c {
        'a' | 'e' | 'i' | 'o' | 'u' => true
        _ => false
    }
}
```

Naming every variant across the alternatives makes the `when` exhaustive, as
`precedence` shows. Any pattern above can be an alternative, ranges and strings
included; `|` binds looser than `..`, so `0..5 | 9` is two alternatives. A
guard applies to the whole arm: in `1 | 2 | 3 if loud`, `loud` is tested
whichever alternative matched.

A long alternation wraps like a long expression: a trailing `|` continues it
on the next line. A line may also start with `|`, which puts each alternative
on a line of its own:

```kflat
fun is_vowel(c: char): bool {
    return when c {
        'a' | 'e' | 'i' |
        'o' | 'u' => true
        _ => false
    }
}

fun is_additive(op: Op): bool {
    return when op {
        Plus
        | Minus => true
        Star
        | Slash => false
    }
}
```

An alternative cannot bind a name, since the name would be unset whenever
another alternative matched:

```kflat
fun main(): int32 {
    val o: int32? = 4
    return when o {
        Some(x) | None => 1
    }
}
```

```console
$ komp check .
src/main.kf:4:9: error: an alternative in `A | B` cannot bind a name; split the arm, or bind the whole subject with a guard
```

## Testing one variant: `is`

`value is Pattern` asks whether a `when` arm with that pattern would match,
and is a `bool`. A bare variant matches whatever its payloads hold, so
`s is Circle` needs no `Circle(_)`; a written pattern narrows it like an arm
does. Nothing is bound, so write `_` where an arm would name a payload.

```kflat
enum Shape {
    Circle(int32)
    Rect(int32, int32)
    Empty
}

fun describe(s: &Shape): String {
    if s is Empty { return String.from("nothing") }
    if s is Rect(_, 0) || s is Rect(0, _) { return String.from("a flat rectangle") }
    return String.from("a shape")
}

fun main(): int32 {
    val shapes = [Shape.Circle(2), Shape.Rect(3, 0), Shape.Empty]
    val round = shapes.count(|s| s is Circle)
    println("${round} round, then ${describe(&shapes[1])}")
    return 0
}
```

```console
1 round, then a flat rectangle
```

`is` binds as tightly as `==`, so `a is Circle && b` tests before it joins,
and `!(s is Empty)` needs its parentheses. A name after `is` that is not one
of the enum's variants is an error, where in an arm it would be a binder
matching everything. `is` is a keyword only after an operand: a local may
still be called `is`.

## Unreachable arms

An arm whose every value an earlier unguarded arm already matches is
rejected: a repeated literal, a literal inside an earlier range, or a range
inside a wider one. A range that only overlaps an earlier one is accepted, and
its shared values go to the earlier arm.

```kflat
fun main(): int32 {
    val n = 5
    return when n {
        0..10 => 1
        5 => 2
        _ => 0
    }
}
```

```console
$ komp check .
src/main.kf:5:9: error: this arm is already covered by an earlier arm
```
