# Templates

A template is code an annotation adds beside each struct it marks. It is
written in KFlat, in the library that declares the annotation, and read over
the struct's fields while the program compiles:

```kflat
annotation fields_equal on struct

template fields_equal on struct T {
    impl Equal for T {
        fun equals(other: &T): bool {
            while field in T.fields {
                if self.$field != other.$field { return false }
            }
            return true
        }
    }
}

@fields_equal
struct Point {
    val x: int32
    val y: int32
}

fun main(): int32 {
    val a = Point { x: 1, y: 2 }
    val b = Point { x: 1, y: 2 }
    return if a == b { 0 } else { 1 }
}
```

`Point` gets an `impl Equal` comparing `x`, then `y`, as if it were written by
hand beside the struct. It is then checked like any other code.

## Declaring one

`template NAME on struct T { declarations }` belongs to the annotation `NAME`,
which must be declared in the same crate and mark structs. An annotation has at
most one template on `struct`. `T` names the marked struct inside the template.

The template holds ordinary declarations: trait impls, `impl T` blocks, and
extension functions such as `fun T.describe()`. `template`, like `annotation`,
is a keyword only at the head of the declaration.

A template only adds. It never changes the struct it marks, and a trait impl
or method it adds that the struct already has by hand is an error naming both.

## Member loops and facts

`while field in T.fields` runs while the program compiles, not when it runs.
It becomes one block per field, in order, with `field` standing for that field.
In each block:

| Written | Becomes |
|---|---|
| `self.$field` | the field, `self.x` |
| `field.name` | its name as a string, `"x"` |
| `field.index` | its position, `0` |
| `field.type` (in a type) | its type, `int32` |
| `field.has<A>()` | whether the field carries `@A` |
| `field.get<A>()` | `@A`'s arguments on the field, as `A`'s argument struct |

Outside a loop, `T.name` is the struct's name, `T.fields.size()` the number of
fields, and `args` the arguments the annotation's use was written with.

An `if` or `when` whose condition is made of these facts alone is decided while
the program compiles: only the branch taken is kept, so the other need not make
sense for that field. `get<A>()` on a field without `@A` is an error, so guard it
with `has<A>()`:

```kflat
annotation described(prefix: String) on struct
annotation hidden on field
annotation caption(text: String) on field

template described on struct T {
    fun T.describe(): String {
        var out = "${args.prefix}${T.name} with ${T.fields.size()} fields:"
        while field in T.fields {
            if !field.has<hidden>() {
                val shown = if field.has<caption>() { field.get<caption>().text } else { field.name }
                out.append(" ${field.index}=${shown}")
            }
        }
        return out
    }
}

@described("user ")
struct User {
    val id: int64
    @caption("login")
    val name: String
    @hidden
    val password: String
}

fun main(): int32 {
    val user = User { id: 7, name: "ann", password: "hunter2" }
    println(user.describe())
    return 0
}
```

```console
user User with 3 fields: 0=id 1=login
```

Any other variable in a template is an ordinary one, living while the program
runs. `field` itself is not a value: use one of its facts, or `$field`.

## Where the code lives

What a template adds belongs to the marked struct's module: an extension
function it adds is called there without an import. Names in the template
resolve where the template is written, so it can call its own module's
private helpers, and a user of the annotation imports only the annotation.

## Errors

An error in code a loop added for one field is reported at that field, with a
note pointing at the template:

```console
src/main.kf:20:9: error: operator requires `impl Equal for Handle`
        val handle: Handle
            ^~~~~~
  = note: in what `@fields_equal`'s template adds for field `Holder.handle` (at src/main.kf:3:10)
      template fields_equal on struct T {
               ^~~~~~~~~~~~
```

Any other error in a template's code is reported in the template, with a note
naming the struct it was adding to.

## Limits

Templates add to structs of their own crate only, and not to a generic struct.
A name a template adds cannot be spliced (`with_$field`), so a free function or
a type in a template is added once per marked struct under one name, and
collides when two structs are marked. A `pub` extension function a template
adds cannot be imported by another module. [Limitations](../limitations.md#templates)
lists each with its issue.
