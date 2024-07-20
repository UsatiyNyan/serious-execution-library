# serious-execution-library
For serious programmers.

# st

Stands for single-threaded. An experiment in creating a single-threaded versions of following concepts in C++:
- `future`
- `coroutine`
- `event-queue`/`epoll`

Fully working single-threaded TCP echo-server on epoll is [here](examples/src/00_epoll_st.cpp).

# BUILD

```bash
cmake -DCMAKE_BUILD_TYPE=Release -S . -B ./build
cmake --build ./build --parallel "$(nproc)" --target serious-execution-library_00_epoll_st
```
