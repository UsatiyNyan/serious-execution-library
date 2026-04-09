# Channel & Select Race/UAF Analysis

## Architecture Overview

### Key Components
- `channel_impl<V, Mutex, Atomic>` - MPMC channel with mutex-protected queues
- `select_connection` - coordinates multiple cases via `parallel_connection`
- `parallel_connection` - manages parallel emissions with serialized cancel/delete
- `serial_executor` - ensures FIFO task ordering for synchronization

### Synchronization Model
1. Channel operations protected by mutex (`m_`)
2. Select uses `kcas` for atomic done flag transition
3. `parallel_connection` uses `serial_executor` to serialize: emit, try_cancel_beside, delete_this

---

## Interleavings Table

### Channel Operations

| Operation A | Operation B | Interleaving | Result | Status |
|------------|-------------|--------------|--------|--------|
| send(s) queued | receive(r) | A first: s queued; B matches with s | Values exchanged | OK |
| send(s) queued | close() | A first: s queued; B closes | s stays in sendq, receivers get error | OK (by design) |
| receive(r) queued | close() | A first: r queued; B closes | r gets error via close() | OK |
| try_cancel(node) | close() | A first: node erased, gets null; B: node not in queue | Only null delivered | OK |
| try_cancel(node) | close() | B first: node.queued_in=null; A: no erase, no callback; B: delivers error | Only error delivered | OK |

### Select + Channel Interleavings

| Select Case A | Select Case B (on channel) | Channel Op C | Interleaving | Result | Status |
|--------------|---------------------------|--------------|--------------|--------|--------|
| completes first | recv queued | send arrives | A: done_=1; C: pops recv, kcas fails; C: checks done, doesn't requeue | **BUG: recv never gets callback** | **BUG** |
| completes first | send queued | recv arrives | A: done_=1; C: pops send, kcas fails; C: checks done, doesn't requeue | **BUG: send never gets callback** | **BUG** |

---

## Detailed Bug Analysis

### BUG: Missing callback when kcas fails and select is done

**Location**: `channel.hpp` lines 107-130 (send) and 158-181 (receive)

**Scenario**:
1. select has 2 cases: case A (any signal), case B (channel recv)
2. case B's recv_node is queued in channel
3. case A completes: `done_ = 1`, schedules try_cancel_beside(A)
4. Meanwhile, a send arrives on channel
5. send pops recv from queue, sets `recv.queued_in = nullptr`
6. send attempts kcas with recv - **fails** (select's done_ = 1)
7. send checks `kcas_read(recv.select_done)` - sees value > 0
8. send does NOT requeue recv (correct - select will cancel it)
9. **But**: send does NOT set `recv.requested_cancel = true`
10. send returns
11. serial_executor runs try_cancel_beside -> calls try_cancel on recv
12. try_cancel -> un_impl:
    - `queued_in == nullptr` -> skip erase
    - `exchange(requested_cancel, true)` returns **false**
    - **No callback delivered!**

**Impact**:
- recv's slot never gets set_null
- `increment_and_check()` never called for this case
- counter never reaches N
- `delete_this` never scheduled
- **Memory leak of select_connection**

**Code Path** (send, line 122-128):
```cpp
// if recv's select is done, then it will be try_cancel-ed by select
// and we don't need to requeue it
if (a_recv_node->select_done == nullptr || kcas_read(*a_recv_node->select_done) == 0) {
    // put recv back at front
    a_recv_node->queued_in = this;
    recvq_.push_front(a_recv_node);
    a_send_node.requested_cancel = true;
    break;
}
// BUG: recv.requested_cancel not set, recv never gets callback
```

**Fix**: After determining not to requeue (because select is done), set `requested_cancel = true`:
```cpp
if (a_recv_node->select_done == nullptr || kcas_read(*a_recv_node->select_done) == 0) {
    a_recv_node->queued_in = this;
    recvq_.push_front(a_recv_node);
    a_send_node.requested_cancel = true;
    break;
}
// recv's select is done - mark as cancelled so try_cancel will notify it
a_recv_node->requested_cancel = true;
```

Same fix needed in receive() for the symmetric case (lines 173-178).

---

## Other Findings

### Design Constraint: Channel must outlive connections

**Not a bug, but important constraint**.

Connections hold `impl_&` reference to channel_impl. If channel is destroyed while connections are active:
- Dangling reference
- UAF on any subsequent operation

**Mitigation**: Always call `channel->close()` before destroying channel to clean up pending operations.

### Correct Behaviors Verified

1. **close() + try_cancel() race**: Properly handles both orderings
   - close first: sets queued_in=null, try_cancel sees it and skips
   - cancel first: removes from queue, close doesn't see it

2. **parallel_connection serialization**: serial_executor ensures:
   - emit runs before try_cancel_beside/delete_this can access cancel_handles
   - delete_this defers if cancel_handles not yet set

3. **kcas for select done flag**: Ensures exactly one case wins

4. **Memory ordering**: relaxed increment_and_check is OK because:
   - Only used to detect "is_last"
   - Actual work (cancel, delete) goes through serial_executor with proper barriers

---

## Test Cases Needed

### For the kcas failure bug:

```cpp
TEST(channel, selectCaseFailsKcasWhenOtherCaseWins) {
    manual_executor executor;
    serial_executor serial{ executor };

    auto channel = make_channel<int>();

    int result = -1;

    // Create select with: immediate value signal + channel receive
    select_(serial)
        .case_(value_as_signal(42), [&](int x) { result = x; return 0; })
        .case_(channel->receive(), [&](int x) { result = x; return 1; })
        | detach();

    // At this point:
    // - value signal completes immediately (done_ = 1)
    // - recv is queued on channel
    // - try_cancel_beside scheduled

    // Now send to channel - this will pop recv but kcas will fail
    bool send_cancelled = false;
    channel->send(100)
        | map([](meta::unit) { return meta::unit{}; })
        | map_error([&](meta::unit) { send_cancelled = true; return meta::unit{}; })
        | detach();

    // Execute all scheduled tasks
    while (executor.execute_batch() > 0) {}

    // Verify:
    // 1. result should be 42 (immediate signal won)
    // 2. send should be cancelled
    // 3. No memory leak (select_connection properly deleted)
    EXPECT_EQ(result, 42);
    EXPECT_TRUE(send_cancelled);
}
```

---

## Files to Modify

1. `include/sl/exec/algo/sync/channel.hpp`
   - Line ~128: Add `a_recv_node->requested_cancel = true;` after the if-block
   - Line ~178: Add `a_send_node->requested_cancel = true;` after the if-block

---

## BUG FOUND AND FIXED: emit_ordered cancel_handles index mismatch

**Location**: `include/sl/exec/algo/sync/detail/parallel.hpp` lines 91-122

**Problem**: In `emit_ordered()`, connections are sorted by their ordering value and emitted in sorted order. However, the `cancel_handles` array was populated in sorted order while `schedule_try_cancel_beside()` uses the ORIGINAL case index (stored in `case_slot_type`).

This caused a mismatch: when trying to cancel "all except the winning case", the wrong cancel handles were called. Specifically, when a non-ordered signal (like `result_signal`) won against an ordered signal (like channel operations), the cancellation would skip the wrong index and call `try_cancel()` on the dummy cancel handle instead of the actual channel cancel handle.

**Impact**:
- The channel operation was never cancelled
- `set_null_impl` was never called for that case
- `increment_and_check()` never reached N (number of cases)
- `schedule_delete_this()` was never called
- **Memory leak of select_connection**

**Fix Applied**: Modified `emit_ordered()` to populate `cancel_handles` in ORIGINAL order while still emitting connections in sorted order:

```cpp
// Store: connection pointer, ordering value, original index
std::array<std::tuple<connection*, std::uintptr_t, std::size_t>, N> ordered_connections{};
// ... populate with original index ...

// Emit in sorted order, but store cancel_handles in ORIGINAL order
std::array<cancel_handle*, N> cancel_handles{};
for (std::size_t i = 0; i < ordered_connections.size(); ++i) {
    std::size_t original_index = std::get<2>(ordered_connections[i]);
    cancel_handles[original_index] = &std::move(*std::get<0>(ordered_connections[i])).emit();
}
```

---

## Summary

| Issue | Severity | Location | Fix Required |
|-------|----------|----------|--------------|
| emit_ordered cancel_handles index mismatch | **Critical** (memory leak) | parallel.hpp:91-122 | **FIXED** |
| Missing requested_cancel on kcas failure | **High** (memory leak) | channel.hpp:128, 178 | Yes |
| Channel must outlive connections | Design constraint | N/A | Document only |
