# Design

This document describes the design decisions for `parallel_connection`, `channel`, and `select`.

## parallel_connection design

`parallel_connection` coordinates N independent connections running concurrently, providing:
- Completion tracking via atomic counter
- Cancellation of sibling connections when one completes
- Safe cleanup when all connections finish

```
+-----------------------------------------------------------------------------+
|                           parallel_connection<N>                            |
|                                                                             |
|  connections_: tuple<Connection...>     N independent signal subscriptions  |
|                                                                             |
|  +---------------------------------------------------------------------+   |
|  |                        serial_executor                              |   |
|  |  Serializes all state mutations to avoid races between:             |   |
|  |  - emit completing                                                  |   |
|  |  - try_cancel_beside being scheduled                                |   |
|  |  - delete_this being scheduled                                      |   |
|  |                                                                     |   |
|  |  tasks_:                                                            |   |
|  |    emit_task            -> stores cancel_handles after emit         |   |
|  |    try_cancel_beside    -> cancels all except winner                |   |
|  |    delete_this          -> invokes destructor callback              |   |
|  +---------------------------------------------------------------------+   |
|                                                                             |
|  state_:                       (accessed only via serial_executor)          |
|    cancel_handles[N]           pointers to each connection's cancel_handle  |
|    cancel_request              deferred cancel if emit not yet done         |
|    delete_requested            deferred delete if emit not yet done         |
|                                                                             |
|  counter_: Atomic<uint32>      tracks how many connections have completed   |
|                                                                             |
+-----------------------------------------------------------------------------+

Lifecycle
=========

1. emit() / emit_ordered()
   +------------------------------------------------------------------------+
   |  for each connection:                                                  |
   |      cancel_handles[i] = connection[i].emit()                          |
   |                                                                        |
   |  emit_ordered(): sorts connections by get_ordering() before emitting   |
   |                  (prevents deadlock when connections acquire locks)    |
   |                                                                        |
   |  schedule(emit_task) -> serializes storing of cancel_handles           |
   +------------------------------------------------------------------------+
                                       |
                                       v
2. One connection completes (calls slot.set_value/set_error/set_null)
   +------------------------------------------------------------------------+
   |  schedule_try_cancel_beside(winning_index)                             |
   |      \-> try_cancel_beside_task                                        |
   |              \-> for i != winning_index: cancel_handles[i]->try_cancel |
   |                                                                        |
   |  increment_and_check()                                                 |
   |      \-> counter_.fetch_add(1)                                         |
   |      \-> returns true if this was the last connection                  |
   +------------------------------------------------------------------------+
                                       |
                                       v
3. All N connections complete (success, error, or cancelled)
   +------------------------------------------------------------------------+
   |  schedule_delete_this()                                                |
   |      \-> delete_this_task                                              |
   |              \-> invokes DeleteThisT callback (usually destructor)     |
   +------------------------------------------------------------------------+

Why serial_executor?
====================
Connections complete on arbitrary threads. Without serialization:

  Thread A (connection 0 completes)     Thread B (connection 1 completes)
  ----------------------------------    ----------------------------------
  schedule_try_cancel_beside(0)         schedule_try_cancel_beside(1)
        |                                     |
        v                                     v
  read cancel_handles[1]  <--- RACE --->  read cancel_handles[0]
  call try_cancel()                       call try_cancel()

serial_executor ensures these operations execute one at a time.

Why emit_ordered()?
===================
When connections acquire locks (e.g., channel mutex), emitting in arbitrary
order can deadlock:

  parallel_connection A              parallel_connection B
  ---------------------              ---------------------
  case: ch1.send()                   case: ch2.send()
  case: ch2.recv()                   case: ch1.recv()

  emit ch1.send() -> lock(ch1)       emit ch2.send() -> lock(ch2)
  emit ch2.recv() -> lock(ch2) WAIT  emit ch1.recv() -> lock(ch1) WAIT
                     ^                                  ^
                     +---------- DEADLOCK -------------+

emit_ordered() sorts by channel address, ensuring consistent lock order:

  parallel_connection A              parallel_connection B
  ---------------------              ---------------------
  emit ch1.send() -> lock(ch1)       emit ch1.recv() -> lock(ch1) WAIT
  emit ch2.recv() -> lock(ch2)              |
         |                                  |
         v                                  v
      unlock(ch1)                    lock(ch1) -> proceed
      unlock(ch2)                    lock(ch2) -> proceed
```

## channel design

Unbuffered MPMC (Multi-Producer Multi-Consumer) rendezvous channel.
Zero capacity - sender blocks until receiver arrives (and vice versa).

```
+-----------------------------------------------------------------------------+
|                              channel_impl                                   |
|                                                                             |
|  +-----------------------------------+  +--------------------------------+  |
|  |            sendq_                 |  |            recvq_              |  |
|  |   intrusive_list<send_node>       |  |   intrusive_list<recv_node>    |  |
|  |                                   |  |                                |  |
|  |  +----------+  +----------+       |  |  +----------+  +----------+    |  |
|  |  |send_node |->|send_node |-> ... |  |  |recv_node |->|recv_node |->  |  |
|  |  |  value   |  |  value   |       |  |  |  slot    |  |  slot    |    |  |
|  |  |  slot    |  |  slot    |       |  |  +----------+  +----------+    |  |
|  |  +----------+  +----------+       |  |                                |  |
|  +-----------------------------------+  +--------------------------------+  |
|                                                                             |
|  is_closed_: bool                                                           |
|  m_: Mutex                           protects all state above               |
+-----------------------------------------------------------------------------+

Core operations (without select)
================================

send(node):
  +----------------------------------------------------------------------+
  |  lock(m_)                                                            |
  |  if is_closed_ -> unlock, slot.set_error(), return                   |
  |                                                                      |
  |  if recvq_.empty():                                                  |
  |      sendq_.push_back(node)        # wait for receiver               |
  |      return (stays locked in queue until matched or cancelled)       |
  |                                                                      |
  |  recv = recvq_.pop_front()         # found waiting receiver          |
  |  unlock(m_)                                                          |
  |  recv.slot.set_value(node.value)   # transfer value                  |
  |  node.slot.set_value(unit)         # notify sender of success        |
  +----------------------------------------------------------------------+

receive(node):
  +----------------------------------------------------------------------+
  |  lock(m_)                                                            |
  |  if sendq_.empty():                                                  |
  |      if is_closed_ -> unlock, slot.set_error(), return               |
  |      recvq_.push_back(node)        # wait for sender                 |
  |      return                                                          |
  |                                                                      |
  |  send = sendq_.pop_front()         # found waiting sender            |
  |  unlock(m_)                                                          |
  |  node.slot.set_value(send.value)   # receive value                   |
  |  send.slot.set_value(unit)         # notify sender of success        |
  +----------------------------------------------------------------------+

Intrusive node design
=====================
Nodes are NOT heap-allocated by channel. They live inside connection objects:

  channel_send_signal::connection_type
  +----------------------------------+
  |  send_node node_     <----------- embedded, not heap-allocated
  |  impl_type& impl_                |
  +----------------------------------+

This means:
- Zero allocations for channel operations
- Node lifetime tied to connection lifetime
- Channel just links/unlinks pointers

Node state tracking
===================
Each node tracks its queue membership:

  channel_node:
    queued_in: channel_impl*    # which channel this node is queued in (or nullptr)
    requested_cancel: bool      # cancellation requested but not yet processed

  queued_in != nullptr  -->  node is in some channel's queue
  queued_in == nullptr  -->  node is not queued (matched, cancelled, or never queued)

Cancellation (unsend/unreceive)
===============================

  +----------------------------------------------------------------------+
  |  lock(m_)                                                            |
  |                                                                      |
  |  if node.queued_in != nullptr:     # still in queue                  |
  |      queue.erase(node)                                               |
  |      node.queued_in = nullptr                                        |
  |      node.requested_cancel = true                                    |
  |                                                                      |
  |  if node.requested_cancel was already true:                          |
  |      unlock(m_)                                                      |
  |      node.slot.set_null()          # notify of cancellation          |
  +----------------------------------------------------------------------+

The double-check on requested_cancel handles the race:
- Thread A: calls unsend(), sets requested_cancel = true
- Thread B: matches the node before A can remove it from queue
- Thread B: sees requested_cancel, doesn't transfer value
- Thread A: sees requested_cancel was already true, calls set_null()

Close semantics
===============

  +----------------------------------------------------------------------+
  |  lock(m_)                                                            |
  |  if already closed -> unlock, slot.set_error(), return               |
  |                                                                      |
  |  is_closed_ = true                                                   |
  |  pending_recvs = move(recvq_)      # take all waiting receivers      |
  |  unlock(m_)                                                          |
  |                                                                      |
  |  for each recv in pending_recvs:                                     |
  |      recv.slot.set_error()         # notify receivers channel closed |
  |                                                                      |
  |  slot.set_value()                  # close succeeded                 |
  +----------------------------------------------------------------------+

After close:
- New send() calls get immediate error (is_closed_ check)
- New receive() calls get immediate error
- Pending senders in sendq_ remain (will be cancelled by their owners)
- Pending receivers are notified with error

Signal integration
==================
Channel operations are Signals (lazy, composable):

  channel.send(value)   -> channel_send_signal    -> Signal<unit, unit>
  channel.receive()     -> channel_receive_signal -> Signal<Value, unit>
  channel.close()       -> channel_close_signal   -> Signal<unit, unit>

Each signal's connection is an ordered_connection (provides get_ordering())
using channel address - enables emit_ordered() in parallel_connection.
```

## select design

Select waits on multiple signals concurrently - exactly one case wins.
Built on top of parallel_connection with an atomic consensus flag.

```
+-----------------------------------------------------------------------------+
|                            select_connection                                |
|                                                                             |
|  +-----------------------------------------------------------------------+  |
|  |                       parallel_connection                             |  |
|  |  (provides: emit, cancel siblings, completion counting, cleanup)      |  |
|  |                                                                       |  |
|  |  case_slot[0]         case_slot[1]         case_slot[2]               |  |
|  |  (ch1.send)           (ch2.recv)           (timeout)                  |  |
|  |       |                    |                    |                     |  |
|  |       v                    v                    v                     |  |
|  |  connection[0]        connection[1]        connection[2]              |  |
|  +-----------------------------------------------------------------------+  |
|                                                                             |
|  done_: Atomic<size_t> = 0      consensus flag - first to CAS 0->1 wins     |
|                                 (word-size for KCAS2 compatibility)         |
|                                                                             |
|  slot_: slot&                   where to deliver the winning case's result  |
+-----------------------------------------------------------------------------+

select_connection composes parallel_connection, adding the consensus layer.
parallel_connection doesn't know about "winning" - it just coordinates.

Completion and cancellation
===========================
When a case completes, there are two paths depending on who claims done_:

Path 1: Case completes independently (e.g., timeout fires)
----------------------------------------------------------
  case_slot.set_value(value)
      |
      v
  check_done(): kcas({done_: 0->1})     # try to claim consensus
      |
      +-- success: I won
      |       result = functor(value)
      |       slot_.set_value(result)           # deliver to user
      |       parallel.schedule_try_cancel_beside(index)
      |                  |
      |                  v  (via serial_executor)
      |           for i != index:
      |               cancel_handles[i]->try_cancel()  # e.g. channel.unsend()
      |
      +-- failure: another case already won, do nothing
      |
      v
  parallel.increment_and_check()
      +-- if last: parallel.schedule_delete_this()

Path 2: Channel claims done_ via KCAS (select-aware signal)
-----------------------------------------------------------
  channel.try_fulfill_impl():
      kcas({send.select_done: 0->1}, {recv.select_done: 0->1})  # KCAS2
      |
      v  (success - channel already claimed done_)
  case_slot.set_value_skip_done(value)
      |
      +-- skip check_done(), proceed directly to:
              result = functor(value)
              slot_.set_value(result)
              parallel.schedule_try_cancel_beside(index)
      |
      v
  parallel.increment_and_check()
      +-- if last: parallel.schedule_delete_this()

In both paths, the winner triggers sibling cancellation via parallel_connection.
Cancelled siblings call set_null(), which increments the counter.
Last completion (win or cancel) triggers cleanup.

select_slot interface
=====================
Extends slot to expose done_ for external claiming:

  select_slot<Atomic, ValueT>:
      get_done() -> Atomic<size_t>&     # channel reads this for KCAS
      set_value_skip_done(value)        # channel already claimed done_

Simple signals (timer, default) just call set_value() - case_slot handles KCAS.
Select-aware signals (channel) can claim done_ externally, then call set_value_skip_done().

Why KCAS2 for two selects?
==========================
When channel matches two nodes that are BOTH in selects, it must claim
both done_ flags atomically. The done_ flags are external to channel's mutex:

  Select A                    channel (mutex held)           Select B
  --------                    ------------------             --------
  case: ch.send()             matching A.send with B.recv    case: ch.recv()
  case: timeout               ...                            case: timeout

  Without KCAS2:
  1. channel: CAS(A.done_: 0->1) succeeds
  2. channel: unlocks mutex
  3. B.timeout fires on another thread, CAS(B.done_: 0->1) succeeds
  4. BOTH selects think they won!

  With KCAS2:
  - Either BOTH done_ flags transition 0->1 atomically
  - Or neither does, and channel retries with next node in queue

Same-select guard
=================
A select with both send and recv on same channel shares one done_ pointer.
Channel detects this (select_done pointers equal) and skips the pairing.
A select cannot match with itself.
```
