# Associated types

A trait can declare a type member alongside its method members. Each
implementation chooses a concrete type for that member. This lets the trait
express relationships between types — "every collection has an associated
iterator type" — without turning every generic signature into a zoo of type
parameters.

## Declaring and implementing

```kflat
trait Producer {
    type Out
    fun make(): Self.Out
}

struct Widget { pub val v: int32 }

impl Producer for Widget {
    type Out = int32
    fun make(): int32 { return self.v }
}
```

The trait declares `type Out`; the impl sets `type Out = int32`. The method
`make` uses `Self.Out` in its return type. When the impl is resolved,
`Self.Out` becomes `int32` and the caller sees a concrete type.

## Projections

A projection spells out `Type.AssocName` — for example, `Widget.Out`
resolves to the associated type chosen in `Widget`'s impl. You can use a
projection in a function signature:

```kflat
fun produce(w: Widget): Widget.Out {
    return w.v
}
```

The compiler substitutes the projection away, so `Widget.Out` becomes `int32`
and code generation sees an ordinary function returning `int32`. There is no
runtime cost for projections.

## Bounds on an associated type

An impl can restrict its associated type with a bound:

```kflat
trait Container {
    type Item: Equal
    fun get(): Self.Item
}
```

Any type that implements `Container` must set `type Item` to a type that
itself implements `Equal`. The compiler checks this when the impl is written.

## The Iterable / Iterator protocol

The `for` loop is built on associated types. `Iterable` declares the iterator
type, and `Iterator` declares the element type:

```kflat
// (simplified from libs/core/src/iter.kf)

trait Iterator {
    type Item
    fun next(): Option<Self.Item>
}

trait Iterable {
    type Iter: Iterator
    fun iter(): Self.Iter
}
```

A type that impls `Iterable` picks its iterator, and the iterator's `next`
returns an `Option` of the element type. The `for` desugaring calls `iter()`,
then `next()` until `None`. Because the element type comes from the associated
`Item`, you write `while x in container` without naming it.
