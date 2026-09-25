# Control flow

## if / else

`if` evaluates a condition of type `bool`. The body of each branch is a block:

```kflat
if x > 0 {
    println("positive")
} else {
    println("zero or negative")
}
```

An `else` branch is optional. The condition goes directly between `if` and
`{`, without parentheses:

```kflat
if ready {
    do_work()
}
```

A struct literal inside a condition is fine as long as it is wrapped in
parentheses:

```kflat
struct Node { pub val v: int32 }

if take_node(Node { v: 7 }) != 7 {
    // ...
}
```

### if as a value

The same `if` produces a value. There is one rule separating the two uses:
**`else` is optional as a statement, and required as a value** — a value has
to exist on every path.

```kflat
val label = if n > 0 { "positive" } else { "not positive" }
```

A branch may be a block. Its value is the block's last statement:

```kflat
val scaled = if wide {
    val factor = base * 2
    factor + 1
} else {
    0
}
```

That last statement may itself be an `if` or a `when` — it produces a value
through each of its own branches:

```kflat
val bucket = if known {
    val n = measure()
    if n > 100 { 2 } else if n > 10 { 1 } else { 0 }
} else {
    0
}
```

Both branches must agree on a type, and the result is the wider of the two,
so a literal in either branch widens to fit the slot rather than the branch
that happens to be written first.

An `if` used as a value with no `else` is an error:

```kflat
val v = if c { 1 }
// error: this `if` is used as a value, so it needs an `else` branch that
//        produces one (both paths must)
```

The unbraced guard form is statement-only, since it has no second path:

```kflat
if (ready) return 0
```

The parentheses are what make it unambiguous — the body's start is only
knowable after the condition ends, and a parenthesised condition ends at a
`)` the reader can see. Any other condition keeps its braces.

## while

`while` is the only loop keyword. There is no `for` loop — `for` appears in
the language solely as part of `impl Trait for Type`.

A `while` takes either a condition or an iteration header:

```kflat
while i < n     { ... }    // run while the condition holds
while i in a..b { ... }    // count over a range
while x in xs   { ... }    // walk a collection
```

The two are told apart by a name followed by `in`. `in` is reserved and
cannot appear in an expression, so a condition can never be mistaken for an
iteration header.

### The condition form

Runs its body as long as the condition is `true`:

```kflat
var i = 0
while i < 3 {
    println(i)
    i = i + 1
}
```

```console
$ komp run .
0
1
2
```

`break` exits the loop immediately; `continue` jumps to the next iteration.
Both work in every form.

### while i in a..b

Counts from `a` up to but **not including** `b`:

```kflat
var sum = 0
while i in 0..4 {
    sum = sum + i      // 0 + 1 + 2 + 3
}
```

`..` is an ordinary binary operator, and `a..b` is an ordinary value — a
[`Range`](../libs/core.md) you can bind, pass, and return:

```kflat
val r = 0..4
fun sum_of(r: Range<int32>): int32 { ... }
sum_of(0..5)
```

It binds looser than arithmetic, so each endpoint absorbs its own expression:
`1 + 1..2 * 3` is `2..6`.

A range is lazy — two endpoints and a step, never a materialised sequence — so
iterating one allocates nothing. Its element type comes from its endpoints, so
`0..xs.size()` counts in `uint64` and indexes a list without a cast. An empty
range (`3..3`) and a reversed one (`5..2`) both yield nothing. Iterating a
range does not consume it; each loop takes a fresh cursor.

Writing `..` is all it takes — the module it comes from is pulled in for you,
so there is nothing to import.

There is no `for`-loop special case underneath any of this: `a..b` is a value
that knows how to iterate, so the range and collection forms are the same
construct with different subjects.

### while x in xs

Iterates over a collection by borrowing it. The loop does not consume the
container — the collection remains usable afterwards:

```kflat
var xs = List.new<int32>()
xs.push(10)
xs.push(20)
xs.push(30)

var sum = 0
while x in xs {
    sum = sum + x
}
// xs is still alive here
```

The compiler desugars `while x in xs` to the iterator protocol: `xs.iter()`
returns an iterator whose `next()` method yields `Option<T>`. The loop body
runs for each `Some` and stops at `None`. Any type with `iter()` and a
`next()` method on the returned iterator works — the dispatch is duck-typed
by method name.

`break` and `continue` work here the same way:

```kflat
while x in xs {
    if x == 20 { break }     // exit the loop
    if x < 0  { continue }   // skip to next element
    process(x)
}
```

Nested loops are fine — each loop gets its own distinct iterator.

The binder carries its element type, so it can be passed straight to a generic
function — `twice(x)` infers from it, with no annotated `val` in between.

### while x in &xs

The binder above is an OWNED element: for a type that owns heap memory, each
iteration deep-clones it and drops it again, even when the body only reads it.
Writing the subject with `&` borrows each element instead:

```kflat
while s in &xs {
    total = total + s.byte_len()
}
```

What that costs, over 2,000,000 `String`s at `-O2`:

| loop | |
| --- | --- |
| an index loop with `xs.at(i)` | 2 ms |
| `while s in xs` | 75 ms |
| `while s in &xs` | 2 ms |

The gap is entirely the clone — one allocation, one copy and one free per
element. For a primitive element there is no heap to copy and the two forms are
within a few percent of each other, so `&` is about move-only elements.

The two forms are genuinely different, not one optimized:

- `while x in xs` binds a value you own. You can move it out of the loop, store
  it, or return it.
- `while x in &xs` binds a borrow. You can read it and call methods on it; it
  cannot outlive the container. Moving it out is a move out of a borrow like any
  other, so it auto-clones and warns — which is the by-value form again, for
  that one element.

Neither consumes the container: `xs` is alive after both.

`&` asks for the borrowing half of the iterator protocol — `next_ptr()`, which
answers the element's address and null when exhausted. `List` has it. A type
that implements only `next()` is told so, and told what to add:

```console
$ komp check .
./src/main.kf:21:5: error: `while v in &...` needs a borrowing iterator, and `CountIter` has no `next_ptr()`. Give it `mutating fun next_ptr(): Ptr<T>` answering the element's address and null when exhausted, or drop the `&` to iterate by value
```

## Where line breaks are legal

The parser is line-oriented: a newline ends a statement unless the line is
obviously unfinished. An expression continues onto the next line when a binary
operator ends the line, or when one starts the next:

```kflat
val c = a +
        b        // continues — the `+` held the line open

val d = a
        + b      // continues — a line cannot start with `+`, so it joins
```

A struct literal at the top level of a condition is ambiguous, because `{` is
also the block opener. Keep it inside a call or parentheses:

```kflat
if take_node(Node { v: 7 }) != 7 { ... }
```

