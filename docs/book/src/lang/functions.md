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

Only free functions are values; a method, an extension function or a lambda
is not. A lambda is a struct of its own, passed to a `Call` bound (see
[Lambdas](lambdas.md)); to hand a function value to one, wrap it:
`xs.map(|x| double(x))`. Function values cannot be compared with `==`.

## Extension functions

A function declared as `fun Type.name()` is called like a method of `Type`,
including types from other crates and built-in types such as `int32`. See
[Extension functions](extensions.md).
