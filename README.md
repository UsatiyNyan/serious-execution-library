# serious-execution-library

For serious programmers.

# v1 API

## model

Concepts for creation of asynchrony:
- `signal` produces
- `slot` consumes
- `connection` encompasses fixed state where calculation happens
- `executor` describes how calculation is scheduled, low-level

In client code you are expected to use 

```cpp
Signal<value_type, error_type> auto f();
```

as a description of the source of asynchrony.

## thread

- `detail`
  - `atomic`, `mutex`, `condition_variable` - injections for fuzz testing
  - `tagged_ptr` - tag pointers in lower bits
  - `multiword` - primitives for multiword atomic operations
- `event`-s are different types of sync primitives for one-shot calculations (use `default_event` if confused)
- `sync` - thread-synchronization primitives
- `pool/monolithic` is a simple "queue under mutex" implementation of `executor`

## algo

- `make` - the beginning of execution pipeline
  - `result` - starts asynchrony from *just* a value, the simplest entrance into `signal` monad
  - `contract` - classic pair of `future ~ eager signal` and `promise ~ slot`, is one-shot
  - `pipe` - SPSC pseudo-blocking channel, restrictive, but fast for 1-to-1 communication
  - `channel` - MPMC non-blocking channel, similar to Golang's `chan`
- `sched` - interactions with `executor`
  - `start_on`, `continue_on` - scheduling signals
  - `inline` - immediate executor
  - `manual` - manual executor, allows to replicate races in a single thread, mainly used in tests
- `sync` - execution strategies for synchronization
  - `serial` - serial executor, wraps any other executor into single-threaded pipeline
  - `mutex` - wrapper around serial executor, has better unlock strategy (w/o thundering herd)
- `tf/seq` - sequential transforms of `signal`-s
  - `and_then`, `or_else`, `map`, `map_error`, `flatten` - classic monadic operations
- `tf/par` - enabling parallel execution and races
  - `all`, `any` - classic monadic operations, support cancellation of abandoned `signals`
  - `fork` - replicate signal for multiple pipelines
- `tf/type` - type transformations for signals
  - `box` - type erasure, would put `signal` and `connection` state on heap
  - `query_executor` - populate pipeline context with previous `signal-s` executor, may differ from actual executor at the point of `emit`
  - `cancellable` - injects a cancellation point into pipeline, stops slot fulfillment propagation further if cancelled
- `emit` - evaluation points, where `connection` is formed or calculation is eagerly executed
  - `get` - explicitly blocks until `signal` is evaluated, should be used in synchronous code
  - `detach` - begins evaluation, but does not return value
  - `subscribe` - manual `connection` storage, needs to be manually `emit`-ted
  - `force` - is not a termination point, but begins execution eagerly
  - `share` - is a termination point, can share it's result, is one-shot

## coro

- `coroutine` basic, needs manual `resume` (can be composed, lazily constructed, eagerly awaited)
- `generator` basic, needs manual `next`
- `async` is a coroutine that supports exec integration, `async<void>` can be scheduled in `executor`, can not be manually resumed
- `async_gen` is a generator that supports `co_await`, when awaited yields next value
- `await` gives ability to `co_await Signal`
- `as_signal` transforms `async<T>` into a producer (source of asynchrony)

> _NOTE_: If you want to include a coroutine into pipeline of signals, 
there's a way to combine `as_signal`, `continue_on` and `flatten` in order to achieve that:

```cpp
async<...> coro(T x);

... 
| and_then([&](T x) { return as_signal(coro(x)) | continue_on(some_executor); }) 
| flatten() 
| ...
```


# WONTDO

- [x] get_current_executor - `with_executor` should be enough, inline_executor case is ambiguous
- [x] repeatable connection - hard to manage memory, signals should always be one-shot

# TODO

- [ ] lock-free channel
  - [ ] lock-free list
- [ ] slots:
  - [ ] noexcept
  - [ ] set_result(...) -> bool
- [ ] thread::pool::distributed
- [ ] injectable thread-local storage for "pseudo-threads"
- [ ] TESTS

