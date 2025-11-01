cmake_minimum_required(VERSION 3.21)

include(FetchContent)
FetchContent_Declare(
        serious-cmake-library
        GIT_REPOSITORY git@github.com:UsatiyNyan/serious-cmake-library.git
        GIT_TAG main
)
FetchContent_MakeAvailable(serious-cmake-library)

sl_project_prologue(
    C_STANDARD 17
    CXX_STANDARD 23)
project(
        serious-execution-library
        VERSION 0.0.1
        DESCRIPTION "coroutines, futures, async, parallelism, exclusively for linux for now"
        HOMEPAGE_URL "TODO"
        LANGUAGES CXX)

add_library(${PROJECT_NAME} STATIC
    src/algo/sched/manual.cpp
    src/coro/async.cpp
    src/thread/detail/multiword_dcss.cpp
    src/thread/detail/multiword_kcas.cpp
    src/thread/pool/config.cpp
    )

add_library(sl::exec ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC include)

add_subdirectory(dependencies)

# multiword configuration
set(SL_EXEC_MW_MAX_THREADS_DEFAULT 256 CACHE STRING "Max count of threads for multiword operations")
target_compile_definitions(${PROJECT_NAME} PUBLIC "SL_EXEC_MW_MAX_THREADS_DEFAULT=${SL_EXEC_MW_MAX_THREADS_DEFAULT}")

set(SL_EXEC_KCAS_MAX 16 CACHE STRING "Max amount of arguments passed to kcas")
target_compile_definitions(${PROJECT_NAME} PUBLIC "SL_EXEC_KCAS_MAX=${SL_EXEC_KCAS_MAX}")

set(SL_EXEC_INTERFERENCE_SIZE 64 CACHE STRING "Interference size, set to 0 to use stdlib")
target_compile_definitions(${PROJECT_NAME} PUBLIC "SL_EXEC_INTERFERENCE_SIZE=${SL_EXEC_INTERFERENCE_SIZE}")

if (NOT PROJECT_IS_TOP_LEVEL)
    return()
endif ()

sl_compiler_warnings(${PROJECT_NAME} WARNINGS_AS_ERRORS)

add_subdirectory(examples)

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(test)
endif ()
