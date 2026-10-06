# Functions

## Declaration

A function has a name, a parameter list (each with a type), a return type, and
a body:

```kflat
fun add(a: int32, b: int32): int32 {
    return a + b
}
```

Every parameter needs an explicit type. There is no type inference for
parameters.

The return type is required. If the function returns nothing, write `void`:

```kflat
fun print_twice(msg: str): void {
    println(msg)
    println(msg)
}
```

A `void` function may use a bare `return` to exit early, but must not
return a value:

```kflat
fun noop(): void {
    return 7    // error
}
```

## Default values and named arguments

A parameter may have a default, written after its type. A call that leaves
the argument out gets the default:

```kflat
fun connect(host: str, port: int32 = 80, retries: int32 = 3): int32 {
    return port + retries
}

fun main(): int32 {
    val a = connect("example.com")            // port 80, retries 3
    val b = connect("example.com", 8080)      // retries 3
    val c = connect("example.com", retries = 5)
    return a + b + c - 8251
}
```

An argument written `name = value` binds to the parameter of that name, so a
call can skip a parameter with a default and give a later one. Positional
arguments come first; named ones follow in any order. A parameter without a
default must be given, either way.

```console
$ komp check .
src/main.kf:4:13: error: `connect` needs `host`
        val d = connect(port = 1)
                ^~~~~~~~~~~~~~~~~
```

The same message names a parameter given twice, a name the function does not
have, and a positional argument after a named one.

A default is a constant: a literal, a negated number, `null`, `[]`, an enum
variant without a payload, or a top-level `val`. It must have the
parameter's type, and it may not name another parameter. It is filled in at
each call, so a parameter's name and default are part of a public function's
API, and a dependent crate's calls see them through the crate's interface.

Methods, `static` functions and extension functions take defaults and named
arguments too; the receiver is never named. A function used as a value keeps
its full type, `(str, int32, int32) -> int32` above, so a call through a value
gives every argument by position.

## The return-path check

The compiler verifies that every path through a value-returning function
actually returns a value. If a branch does not, the build fails:

```console
$ cat > src/main.kf << 'EOF'
fun maybe(b: bool): int32 {
    if b {
        return 1
    }
}
EOF
$ komp check .
src/main.kf:1:21: error: function `maybe` does not return on all paths
        (a non-void function must return a value on every path)
    fun maybe(b: bool): int32 {
                        ^~~~~
```

A `return` in a value-returning function must include a value:

```console
$ cat > src/main.kf << 'EOF'
fun f(): int32 {
    return
}
EOF
$ komp check .
src/main.kf:2:5: error: `return` here needs a value:
        the function declares a return type
```

The returned value must match the declared return type, subject to the same
coercions available for arguments and bindings.

### Functions that never return

A function whose return type is an enum with no variants can never return,
since no value of that type exists. `core` declares one, `Never`:

```kflat
import core.traits.Never

fun stop(m: str): Never {
    panic(m)
}

fun pick(n: int32): int32 {
    if n > 0 { return n }
    stop("not positive")
}
```

A call to such a function, like a call to `panic` or the test helper `fail`,
ends a path for the return-path check, and its value fits any slot: `stop`
may be an arm of a [`when` used as a value](when.md#when-as-a-value).

## pub

`pub` on a function makes it visible outside its module — to the crate's
other directories and to dependent crates, each through an import. Without
`pub`, the function is accessible only within its own directory:

```kflat
pub fun add(a: int32, b: int32): int32 { return a + b }

fun helper(a: int32, b: int32): int32 { return a * b }
```

`helper` can be called from any file in the same directory, but not from
another module. `add` is visible to any file that imports it; see
[Modules](modules.md).

## static functions

A function inside an `impl` block without `self` is a static (associated)
function. It is called on the type name, not on a value:

```kflat
impl List {
    pub static fun new<T>(): List<T> { ... }
}

var xs = List.new<int32>()
```

A `static` function cannot access `self`. It works like an ordinary function
scoped to the type.

## mutating methods

A method declared `mutating fun` takes a writable reference to `self`.
Assigning to `self` fields or calling another mutating method on `self` is
only allowed inside a `mutating` body:

```kflat
pub struct Counter { var n: int32 }

impl Counter {
    mutating fun inc(): void {
        self.n = self.n + 1   // allowed: this is a mutating method
    }
}
```

A non-`mutating` method sees `self` as immutable. Attempting to write
`self.field` in a plain `fun` method produces a compile error.

## Functions as values

A function's name, written where a value is expected, is a *function value*:
the function's address, with nothing captured. Its type lists the parameter
types in parentheses and the result after `->`:

```kflat
fun double(x: int32): int32 { return x * 2 }
fun inc(x: int32): int32 { return x + 1 }

fun apply(f: (int32) -> int32, x: int32): int32 {
    return f(x)
}

fun main(): int32 {
    val step: (int32) -> int32 = double
    println(apply(step, 4))   // 8
    println(apply(inc, 4))    // 5
    return 0
}
```

A function that takes nothing is `() -> void`, and a parameter keeps its
passing mode: `(&String) -> uint64` borrows its argument, `(String) -> String`
takes it over. `(T)` with no arrow is just `T`, which is how an optional one is
written: `((int32) -> int32)?`.

A function value is `Copy`: it owns no heap and has nothing to drop. It can be
kept in a local, a field or a `List`, returned, and called through any of them.
A field holding one is called like a method, and the result of a call can be
called again (`pick(true)(5)`):

```kflat
import alloc.list.*

struct Button {
    val label: String
    val on_click: (int32) -> void
}

fun log_click(times: int32): void { println("clicked ${times}") }

fun main(): int32 {
    val ok = Button { label: String.from("OK"), on_click: log_click }
    ok.on_click(1)                        // clicked 1

    var steps = List.new<(int32) -> int32>()
    steps.push(double)
    steps.push(inc)
    var value = 3
    while i in 0..steps.size() {
        val step = steps.get(i)
        value = step(value)
    }
    println(value)                        // 7
    return 0
}

fun double(x: int32): int32 { return x * 2 }
fun inc(x: int32): int32 { return x + 1 }
```

A method of the same name wins over the field.

A generic function's type parameters are inferred through a function type:

```kflat
fun double(x: int32): int32 { return x * 2 }

fun twice<T>(f: (T) -> T, x: T): T {
    return f(f(x))
}

fun main(): int32 {
    println(twice(double, 3))   // 12
    return 0
}
```

A generic function itself is not a value, since a value has one type:

```console
$ komp check .
src/main.kf:4:13: error: generic function `id` cannot be used as a value: a function value has one type
```

### Methods as values

`Type.method` names a method as a value. Its receiver becomes the first
parameter: `&Type`, or `&var Type` for a `mutating` method. A `static` method
has no receiver:

```kflat
struct Counter {
    var n: int32
}

impl Counter {
    static fun make(): Counter { return Counter { n: 1 } }
    fun get(): int32 { return self.n }
    mutating fun bump(by: int32): void { self.n = self.n + by }
}

fun main(): int32 {
    val make: () -> Counter = Counter.make
    val bump: (&var Counter, int32) -> void = Counter.bump
    val get: (&Counter) -> int32 = Counter.get
    var c = make()
    bump(&var c, 4)
    println(get(&c))   // 5
    return 0
}
```

A trait impl's method is named the same way, `Counter.show`, as long as only
one of the type's traits provides `show`. A method of a generic type, or one
with type parameters of its own, is not a value, since a value has one type.

An extension function or a lambda is not a value. A lambda is a struct of its
own, passed to a `Call` bound (see [Lambdas](lambdas.md)); to hand a function
value to one, wrap it: `xs.map(|x| double(x))`. Function values cannot be
compared with `==`.

## Extension functions

A function declared as `fun Type.name()` is called like a method of `Type`,
including types from other crates and built-in types such as `int32`. See
[Extension functions](extensions.md).
