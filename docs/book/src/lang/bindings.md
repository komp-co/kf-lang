# Values, bindings, and mutability

KFlat has two ways to introduce a name: `val` and `var`.

## val: immutable

A `val` binding names a value and cannot be changed after it is set:

```kflat
fun main(): void {
    val answer = 42
    println(answer)
}
```

Trying to reassign a `val` produces an error:

```console
$ komp check .
src/main.kf:3:5: error: cannot reassign `a`: it is not a `var` binding
        a = 2
        ^
```

## var: mutable

A `var` binding can be reassigned:

```kflat
fun main(): void {
    var counter = 1
    println(counter)
    counter = counter + 1
    println(counter)
}
```

```console
$ komp run .
1
2
```

The reassignment must be to the same type — a `var` binding's type is fixed at the point it is declared.

Assignment never declares. A name must be bound first:

```console
$ komp check .
src/main.kf:4:5: error: cannot assign to `current`: it is not declared; declare it with `var current = ...`
        current = 4
        ^
```

## Every binding needs an initializer

Both `val` and `var` require `= expression` in the declaration. There are no
uninitialized variables:

```console
$ cat > src/main.kf << 'EOF'
fun main(): void {
    var a: int32
}
EOF
$ komp check .
src/main.kf:2:17: error: expected `=` after the variable name
    (a binding needs an initializer), found end of line
        var a: int32
                    ^
```

## Type annotation

The type of a binding is inferred from its initializer. You only need to
write a type when the initializer is ambiguous or when you want a narrower or
wider type than the default:

```kflat
val a = 42          // int32, inferred
val b: int64 = 42   // int64, because you asked for it
```

Without the annotation, `42` defaults to `int32` — but only if nothing better
is available.

### A literal takes its type from its uses

An unannotated binding whose initializer is a number literal stays open until
a use needs a type of its own. The literal adopts that type, so a counter
does not need the annotation its comparison implies:

```kflat
var i = 0
while i < xs.size() {      // xs.size() is uint64, so i is uint64
    ...
}
```

A call argument, a `return`, a store into the binding, an assignment into a
typed place, and the other operand of an operator all need a type of their
own. A use that works at any width, such as `n + n` or printing `n`, decides
nothing, and a later use still can. If no use decides, the default stands, so
`small + 1` alone leaves `small` an `int32`.

Once a use has decided, every other use must agree, and one that does not is
reported with the use that decided:

```console
$ komp check .
src/main.kf:4:12: error: `n` is `uint64`, as an earlier use decided, but this use needs `int8`
        narrow(n)
               ^
  = note: decided `uint64` here (at src/main.kf:3:10)
          wide(n)
               ^
```

One shape is not covered yet: a use inside a sub-expression (`while i + 1 < n`
looks at `i + 1`, which has no type of its own). Annotate that.

### A literal never becomes a type its crate cannot see

`String` lives in `alloc`. A bare string literal becomes a `String` where that
is visible, and stays a `str` where it is not — so core, and any crate without
`alloc` in its dependency graph, can write `val s = "text"` and get the `str`
its functions take. Before this the binding claimed a type the crate could not
name, and the C compiler was the first to say so.

The type of a binding cannot change after declaration. If you annotate a type,
the initializer must match it or the compiler reports a type error.

## Shadowing

Declaring a binding with the same name as one already in scope is a *shadow*.
It replaces the old binding with a new one, which may have a different type
and mutability:

```kflat
val a = 1
var a = 2           // shadows the val — a is now mutable
a = 3               // allowed
println(a)          // 3
```

Shadowing works across blocks too:

```kflat
var n = 0
if true {
    val n = 99      // inner scope — shadows outer n
    println(n)      // 99
}
println(n)          // 0 — outer n unaffected
```

The compiler renames each shadowed binding internally so that move-checking,
drop insertion, and code generation see them as distinct variables. The
inner binding's drop (if any) runs when its block ends, independently of the
outer one.
