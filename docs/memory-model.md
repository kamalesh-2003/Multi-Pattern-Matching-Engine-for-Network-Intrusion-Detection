# Memory model of the SPSC ring

`include/mpm/spsc_ring.hpp` is a single-producer / single-consumer lock-free
queue. Correctness rests on four atomic accesses; this document states, for each,
what reordering it prevents and what breaks without it.

## Roles

| Variable | Written by | Read by |
|----------|-----------|---------|
| `tail_`  | producer (`push`) | producer + consumer |
| `head_`  | consumer (`pop`)  | consumer + producer |
| `buffer_[i]` | producer writes, consumer reads (never concurrently on the same slot) |

Because each index has exactly one writer, a thread reading *its own* index needs
no synchronization — hence the `relaxed` self-loads. All cross-thread ordering is
carried by the release/acquire pairs on `tail_` and `head_`.

## The two synchronizes-with edges

### 1. Producer publishes data → consumer reads it (`tail_`)

```
push (producer):                  pop (consumer):
  buffer_[t & mask] = value;        if (h == tail_.load(acquire)) empty;
  tail_.store(t+1, RELEASE);        out = buffer_[h & mask];
```

The `release` store and the `acquire` load form a synchronizes-with edge. Every
write sequenced before the release (here, the element store into `buffer_`) is
guaranteed to be visible to the consumer after its acquire load observes the new
`tail_`.

**Without `release` on the store** (e.g. `relaxed`): the compiler or CPU may move
the `buffer_[t]=value` write *after* the `tail_` update. The consumer then sees
`tail_` advanced, indexes into the slot, and reads an uninitialized or
half-written element. This is the classic publish bug and it manifests on weakly
ordered hardware (ARM/POWER) and under compiler reordering even on x86.

**Without `acquire` on the load**: the consumer's read of `buffer_[h]` could be
hoisted before it confirms the slot is populated, again reading stale data.

### 2. Consumer frees a slot → producer reuses it (`head_`)

```
pop (consumer):                   push (producer):
  out = buffer_[h & mask];          if (next - head_.load(acquire) > cap) full;
  head_.store(h+1, RELEASE);        buffer_[t & mask] = value;
```

The consumer's `release` store of `head_` and the producer's `acquire` load form
the second edge. It guarantees the consumer's *read* of `buffer_[h]` completes
(in the happens-before sense) before the producer is allowed to observe the freed
space and overwrite that slot.

**Without `release` on the consumer store / `acquire` on the producer load**: the
producer may observe freed space early and overwrite `buffer_[h]` while the
consumer's read of it is still in flight — a read/write data race on the slot,
and lost/corrupted data.

## Why x86 "seems" to work anyway — and why we still annotate

x86-TSO does not reorder store→store or load→load in the ways above, so on x86
the hazards mostly do not fire at the hardware level. But the memory orderings
are equally a contract with the **compiler**: `relaxed` would license the
compiler to reorder or fold these accesses regardless of the target ISA. The
annotations make the code correct on every C++-supported platform and document
intent. They are verified empirically by `tests/test_spsc.cpp`, which streams
millions of items through a deliberately small ring (constant full/empty
transitions) and checks that every value arrives exactly once, in order.

## Full vs. empty, and the reserved slot

Capacity is rounded to a power of two and one slot is reserved, so the ring holds
`capacity_ - 1` usable elements. `empty` is `head_ == tail_`; `full` is
`tail_ - head_ == capacity_ - 1`. Using unsigned index arithmetic that wraps
mod 2^64, the subtraction `next - head_` yields the correct occupancy even across
integer wraparound, so no separate size counter (which would need its own
synchronization) is required.
