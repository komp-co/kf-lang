# Enums, Option, and Result

An enum is a type with a fixed set of variants, each optionally carrying a
payload.

## Declaring an enum

```kflat
enum Shape {
    Circle(int32)
    Square(int32)
    Empty
}
```

`Circle` and `Square` carry an `int32` payload; `Empty` carries nothing.

A generic enum takes type parameters:

```kflat
enum Res<T> {
    Ok(T)
    Bad
}
```

The type argument cannot be written on the variant — `Res.Bad<uint8>` does
not parse. A payload supplies it; a variant without one needs the binding
annotated:

```kflat
val a = Res.Ok(7)                  // Res<int32>, from the payload
val b: Res<uint8> = Res.Bad        // no payload to infer from
```

A constructor with the wrong number of arguments or the wrong payload type is
rejected at compile time.

## Option and Result are ordinary enums

`Option<T>` and `Result<T, E>` are not built-in types. They are defined in
`core` as enums:

```kflat
// (simplified from libs/core/src/option.kf)
enum Option<T> {
    Some(T)
    None
}

enum Result<T, E> {
    Ok(T)
    Err(E)
}
```

Because they are ordinary enums, they work with `when`, `for`, and generic
code the same way a user-defined enum does. No special syntax is required.

To construct a value:

```kflat
val found: Option<int32> = Option.Some(42)
val missing: Option<int32> = Option.None
```

When a `when` destructures an `Option`, the compiler checks exhaustiveness:
both `Some` and `None` must be handled:

```kflat
when (opt) {
    Some(value) => { use(value) }
    None => { default }        // required: otherwise not exhaustive
}
```

## The T? sugar

`T?` is syntactic sugar for `Option<T>`. A function returning a nullable can
write the short form:

```kflat
fun find(n: int32): int32? {    // same as Option<int32>
    if n > 0 {
        return Option.Some(n)
    }
    return Option.None
}
```

Only `Option<T>` gets the `T?` shorthand. `Result<T, E>` does not.

`null` is `Option.None` wherever an optional is expected. Comparing an optional
with it tests which variant it holds, so `x == null` and `x != null` work for
any payload, whether or not it implements `Equal`. `null` goes on the right:

```kflat
struct Message {
    val id: String?
}

fun has_id(m: &Message): bool {
    return m.id != null
}
```

## The ?: (elvis) operator

`?:` unwraps an `Option` or bails. `val v = opt ?: <bail>` binds the payload
when `opt` is `Some`, and runs the right-hand side when it is `None` — after
which `v` does not exist, so the right-hand side **must diverge**:

```kflat
fun lookup(key: str): int32? {
    if key == "a" { return Option.Some(1) }
    return Option.None
}

fun resolve(key: str): int32 {
    val v = lookup(key) ?: return -1
    return v                            // v is a plain int32 here
}
```

`return`, `break` and `continue` are all accepted. A plain value is not — this
is not a defaulting operator:

```console
$ komp check .
src/main.kf:8:26: error: the `?:` right-hand side must diverge (end it with `return`, `break` or `continue`) — after it the binding does not exist, so control cannot continue
```

The payoff is that the binding is not optional afterwards: `v` is an `int32`,
not an `int32?`, with no unwrap and no `when`.

## The ?. (safe call) operator

`?.` reads a field or calls a method through an `Option`, and answers an
`Option` either way. On `None` it does not touch the receiver:

```kflat
struct P { pub var n: int32 }

fun find(k: int32): P? {
    if k == 1 { return Option.Some(P { n: 7 }) }
    return Option.None
}

fun main(): int32 {
    val a = find(1)?.n          // Option.Some(7)
    val b = find(2)?.n          // Option.None — `.n` never runs
    when (a) {
        Some(v) => println("a = ${v}")
        None    => println("a is none")
    }
    when (b) {
        Some(v) => println("b = ${v}")
        None    => println("b is none")
    }
    return 0
}
```

```console
a = 7
b is none
```

Unlike `?:`, the result stays optional — this is the operator for carrying
absence forward, where `?:` is the one for leaving it behind.

## The !! (unwrap) operator

`!!` takes the payload and aborts if there is none. It is the escape hatch for
when you know better than the type does, and it says so at the call site:

```kflat
val a = find(1)!!           // 7
val b = find(2)!!           // aborts
```

```console
$ komp run .
Option.unwrap called on a None
$ echo $?
1
```

The message names the operation, not the file — so prefer `?:` or a `when`
wherever the `None` is a case you can answer.

