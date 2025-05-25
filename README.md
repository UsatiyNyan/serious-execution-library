# serious-execution-library

For serious programmers.

# v1 API

## model

Concepts for creation of asynchrony:
- `signal` produces
- `slot` consumes
- `connection` encompasses fixed state where calculation happens
- `executor` describes how calculation is scheduled, low-level

In client code you are expected to use:
`Signal<value_type, error_type> auto` as a description of the source of asynchrony

# WONTDO

- [x] get_current_executor - `with_executor` should be enough, inline_executor case is ambiguous
- [x] repeatable_connection - `(args) -> signal` is basically a repeatable connection

# TODO

- [ ] events (atomics)
- [ ] require noexcept from all slot-s
- [ ] thread::pool::distributed
- [ ] TESTS
- [ ] pseudo-atomics, parametrize emits and executors with atomic

