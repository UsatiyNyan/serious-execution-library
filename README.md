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

## algo

- `make` - the beginning of execution pipeline
- `sched` - interactions with `executor`
- `sync` - execution strategies for synchronization
- `tf/seq` - sequential transforms of `signal`-s
- `tf/par` - enabling parallel execution and races
- `channel` - is a collection of "repeatable" asynchrony, all of them may call `set_value|error` many times:
  - `pipe` - SPSC
  - `broadcast` - SPMC
  - `sink` - MPSC
  - `topic` - MPMC
- `emit` - evaluation points, where `connection` is formed or calculation is eagerly executed

> _NOTE_: Even though most of the design is built around "one-shot" connections (e.g. `get`, `detach`, `co_await`, `all`, `any`),
repeatable connections are still possible. Thanks to `subscribe_connection`, state itself may exist longer than just one `set_value|error`, e.g.:

```cpp
auto [signal, slot] = make_pipe();
const auto connection = std::move(signal) 
    | map([](std::string_view x) { std::cout << x; }) 
    | subscribe();

// both should work
slot.set_value("Hello, ");
slot.set_value("World!\n");
// output should be "Hello, World!\n"
```

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

# TODO

- [ ] require noexcept from all slot-s
- [ ] thread::pool::distributed
- [ ] TESTS

