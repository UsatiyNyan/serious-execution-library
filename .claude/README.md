# Claude Context: serious-execution-library

## Build Commands

```bash
# Build a specific target
cmake-build.sh . <target_name>

# Examples:
cmake-build.sh . serious-execution-library_parallel_test
cmake-build.sh . serious-execution-library_algo_test
cmake-build.sh . serious-execution-library_tests  # meta-target for all tests
```

## Running Tests

```bash
# Run all tests via ctest
ctest --test-dir ./build --output-on-failure

# Run a specific test executable
./build/test/serious-execution-library_<name>_test

# Available test executables:
# - serious-execution-library_algo_test
# - serious-execution-library_coro_test
# - serious-execution-library_exec_test
# - serious-execution-library_model_test
# - serious-execution-library_parallel_test
# - serious-execution-library_sim_test
# - serious-execution-library_thread_test
```

## Adding New Tests

1. Create test file in `test/src/<name>_test.cpp`
2. Add to `test/CMakeLists.txt`: `sl_add_gtest(${PROJECT_NAME} <name>)`

## Writing Async Tests with manual_executor

**Key distinction:**
- `continue_on(executor)` - affects *where* continuation runs, NOT when signal starts
- `schedule(executor, fn)` - defers entire execution until executor runs
- `start_on(executor)` - defers signal start until executor runs

### Correct async test pattern:

```cpp
TEST(example, asyncWithManualExecutor) {
    manual_executor executor;

    int result = 0;

    // Use schedule() to defer execution
    schedule(executor, [] { return meta::ok(42); })
        | map([&result](int x) { result = x; return meta::unit{}; })
        | detach();

    EXPECT_EQ(result, 0);           // Not executed yet
    executor.execute_batch();        // Run scheduled work
    EXPECT_EQ(result, 42);          // Now executed
}
```

### Alternative with start_on:

```cpp
start_on(executor)
    | map([&done](meta::unit) { done = true; return meta::unit{}; })
    | detach();

ASSERT_FALSE(done);
executor.execute_at_most(1);
ASSERT_TRUE(done);
```

### Using subscribe() for manual emission:

```cpp
subscribe_connection conn = value_as_signal(42)
    | continue_on(executor)
    | map([&value](int x) { value = x; return meta::unit{}; })
    | subscribe();

std::move(conn).emit();  // Start the signal

EXPECT_EQ(value, 0);
executor.execute_at_most(1);
EXPECT_EQ(value, 42);
```

## Common Signal Patterns

```cpp
// Immediate value signal
auto s = value_as_signal(42);

// Result signal (value or error)
using result_type = meta::result<int, std::string>;
auto s = as_signal(result_type{ 42 });                        // value
auto s = as_signal(result_type{ tl::unexpect, "error" });     // error

// Null signal (cancellation)
auto s = null_as_signal<int, meta::unit>();

// Get result synchronously (for sync signals)
auto maybe_result = std::move(signal) | get<nowait_event>();
```

## Test Framework

- Uses Google Test (gtest)
- Tests are in `test/src/` directory
- Each test file creates a separate executable
