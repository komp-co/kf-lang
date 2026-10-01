# Arrays

An array holds a fixed number of elements of one type, inline: in the local,
the field or the argument that holds it, with no allocation. The length is
part of the type, so `int32[3]` and `int32[4]` are different types.

```kflat
val xs: int32[3] = [1, 2, 3]
val ys: Array<int32, 3> = [4, 5, 6]   // the same type, spelled out
val zs: float64[] = [1.5, 2.5]        // float64[2]: the literal gives the length
```

`T[N]` is sugar for core's [`Array<T, N>`](../libs/core.md#array), the way
`T?` is sugar for `Option<T>`. `N` is the array's length, a
[value parameter](generics.md#value-parameters) of type `uint64`. `T[]`
leaves the length to the initializer, so it is written only on a `val` or
`var` initialized with an array literal or another array. A parameter that
takes any length is a [slice](#any-length-slices), `&T[]`; anywhere else the
length must be written:

```console
$ komp check .
src/main.kf:1:20: error: the array's length is left out here; only a binding initialized by a literal or another array can leave it out, and any length is taken as a slice, `&T[]`
    fun first(xs: int32[]): int32 {
                       ^
```

Suffixes read left to right: `int32[2][3]` is three `int32[2]`, `int32?[2]`
holds two options, and `int32[2]?` is an optional array.

## Literals

A list literal written where an array is expected builds the array in place,
with no allocation and no `push`. Its elements take the array's element type,
and there must be exactly as many as the array holds:

```console
$ komp check .
src/main.kf:2:24: error: this literal has 2 elements, but `int32[3]` holds 3
        val xs: int32[3] = [1, 2]
                           ^~~~~~
```

With no array expected, `[1, 2, 3]` is still a `List<int32>`; see
[alloc](../libs/alloc.md#list-literals). A crate without alloc has no list to
build, so there the same literal is an `int32[3]`, typed by its first element.

A string literal written where a `uint8` or `char` array is expected builds
that array: its UTF-8 bytes, or its characters, one per element. A `&uint8[]`
or `&char[]` argument takes one the same way. With no such array expected, a
string literal stays text.

```kflat
val magic: uint8[] = "PNG"      // uint8[3]: 80, 78, 71
val letters: char[] = "héllo"   // char[5]; as bytes it would be uint8[6]
```

The count must match a written length, as for a list literal:

```console
$ komp check .
src/main.kf:2:25: error: this string has 3 bytes, but `uint8[4]` holds 4
        val tag: uint8[4] = "PNG"
                            ^~~~~
```

## Elements

`xs[i]` borrows element `i`, like a list's. The index is a `uint64`, checked
against the length on every access: an index past the end panics rather than
reading past the array. `size()` is the length, a constant.

```kflat
var grid: int32[2][2] = [[1, 2], [3, 4]]
grid[1][0] = 7
val corner = grid[1][0] + grid[0][0]     // 8
val count = grid.size()                  // 2
```

Writing through `xs[i]` needs `xs` to be a `var`, or a `&var` borrow of one.

## Any length: slices

`&T[]` is a slice: a borrowed view of elements laid out in a row, whose
length is known only when the program runs. A function taking one takes an
array of any length, a list, or a literal:

```kflat
fun total(xs: &int32[]): int32 {
    var sum = 0
    while x in xs { sum = sum + x }
    return sum
}

fun fill(xs: &var int32[], value: int32): void {
    while i in 0..xs.size() { xs[i] = value }
}

fun main(): int32 {
    var four: int32[4] = [0, 0, 0, 0]
    fill(four, 2)
    val three: int32[3] = [1, 2, 3]
    return total(four) + total(&three) + total([5, 5])   // 8 + 6 + 10
}
```

Where a slice is expected, an array or a list, or a borrow of either, lends
itself as one, and a literal builds an array in place and lends that, with
no allocation. `&var T[]` writes through to what it views, so its source
must be a `var` or a `&var` borrow. A slice's element is inferred like any
other type argument: `fun count<T>(xs: &T[])` takes an `int64[4]` as
`&int64[]`.

`&T[]` is sugar for core's [`Slice<T>`](../libs/core.md#slice) and
`&var T[]` for `SliceMut<T>`. Each is a [view](memory.md#view-types): it
follows the rules of the borrow it stands for. It cannot be returned past
the array it views, cannot be stored in a struct, and while it is still used
its source cannot change:

```console
$ komp build .
src/main.kf:7:5: error: `list` cannot be changed here: `view` borrows it and is still used afterwards
        list.push(2)
        ^~~~~~~~~~~~
```

`slice(range)` narrows a slice to part of the same elements, and an array or
list names its own slices with `as_slice()` and `as_slice_mut()`:

```kflat
fun main(): int32 {
    val scores: int32[5] = [7, 3, 9, 4, 8]
    val middle = scores.as_slice().slice(1..4)     // 3, 9, 4
    return middle[1] + middle.size() as int32      // 9 + 3
}
```

## Any length, known when compiling

A function that needs the length as part of the type is generic over it. It
takes an array of any length, one instance per length used:

```kflat
fun total<N: uint64>(xs: &int32[N]): int32 {
    var sum = 0
    while x in xs { sum = sum + x }
    return sum
}

fun fill<N: uint64>(xs: &var int32[N], value: int32): void {
    while i in 0..xs.size() { xs[i] = value }
}

fun main(): int32 {
    var four: int32[4] = [0, 0, 0, 0]
    fill(&var four, 2)
    val three: int32[3] = [1, 2, 3]
    return total(&four) + total(&three)     // 8 + 6
}
```

`N` is inferred from the argument. `while x in xs` hands out each element in
turn; `xs.iter()` is the cursor it uses, core's `ArrayIter<T>`, which points
into the array and is valid only while the array is alive and unmoved.

## Copying and moving

An array is `Copy` when its element is: assigning an `int32[3]` copies twelve
bytes, and a struct holding one can still be `@derive(Copy)`. An array of an
owning type, such as `String[2]`, moves like any owning value, and dropping it
drops each element.

Arrays go wherever a type does: in struct fields and enum payloads, as
generic arguments (`List<int32[2]>`, `Option<float64[3]>`), and across crates
in public signatures.

## What arrays do not do yet

An array implements no traits but `Clone` and `Copy`, so `==`, `Hash` and
`Display` do not apply to it, and `@derive(Equal)`, `Hash` and `Default` fail
on a struct holding one. [Limitations](../limitations.md#arrays) tracks it.
