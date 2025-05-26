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

- `detail/atomic` is an "injectable" atomic for fuzz testing and platform-specific configurations
- `event`-s are different types of sync primitives for one-shot calculations (use `default_event` if confused)
- `sync` - thread-synchronization primitives
- `pool/monolithic` is a simple "queue under mutex" implementation of `executor`

## coro

- `coroutine` basic, needs manual `resume` (can be composed, lazily constructed, eagerly awaited)
- `generator` basic, needs manual `next`
- `async` is a coroutine that supports exec integration, `async<void>` can be scheduled in `executor`, can not be manually resumed
- `async_gen` is a generator that supports `co_await`, when awaited yields next value
- `await` gives ability to `co_await Signal`
- `as_signal` transforms `async<T>` into a producer (source of asynchrony)

> If you want to include a coroutine into pipeline of signals, 
there's a way to combine `as_signal` and `flatten` in order to achieve that:

```cpp
async<...> coro(T x);

... 
| and_then([&](T x) { return as_signal(coro(x)) | continue_on(some_executor); }) 
| flatten() 
| ...
```


# WONTDO

- [x] get_current_executor - `with_executor` should be enough, inline_executor case is ambiguous
- [x] repeatable_connection - `(args) -> signal` is basically a repeatable connection

# TODO

- [ ] require noexcept from all slot-s
- [ ] thread::pool::distributed
- [ ] TESTS

