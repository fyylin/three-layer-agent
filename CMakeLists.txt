cmake_minimum_required(VERSION 3.20)
project(ThreeLayerAgent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(AGENT_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# -- Platform configuration ----------------------------------------------------
if(WIN32)
    set(PLATFORM_LIBS winhttp ws2_32)
    if(MSVC)
        # /utf-8       : source and execution charset = UTF-8
        # /W4 /WX-     : high warnings, don't treat as errors
        # /wd4100      : unreferenced formal parameter
        # /wd4505      : unreferenced local function
        # /wd4702      : unreachable code
        # /wd4101      : unreferenced local variable
        # /wd4189      : local variable initialized but not referenced (tests)
        # NOTE: Do NOT add WIN32_LEAN_AND_MEAN here  --  tool_set.cpp needs
        #       the full windows.h to access <tlhelp32.h> symbols.
        add_compile_options(/utf-8 /W4 /WX- /wd4100 /wd4505 /wd4702 /wd4101 /wd4189)
        add_compile_definitions(
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _CRT_SECURE_NO_WARNINGS
            _WIN32_WINNT=0x0601
        )
    endif()
else()
    find_package(OpenSSL REQUIRED)
    set(PLATFORM_LIBS OpenSSL::SSL OpenSSL::Crypto)
    set(WARN_FLAGS -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
                   -Wno-format-truncation)
endif()

find_package(Threads REQUIRED)

# -- Source lists --------------------------------------------------------------
set(AGENT_SOURCES
    src/agent/models.cpp
    src/agent/tool_registry.cpp
    src/agent/worker_agent.cpp
    src/agent/manager_agent.cpp
    src/agent/director_agent.cpp
    src/agent/supervisor_agent.cpp
    src/agent/advisor_agent.cpp
    src/agent/reflection.cpp
    src/agent/skill_registry.cpp
    src/agent/manager_pool.cpp
    src/utils/logger.cpp
    src/utils/message_bus.cpp
    src/utils/memory_store.cpp
    src/utils/workspace.cpp
    src/utils/file_lock.cpp
    src/utils/structured_log.cpp
    src/utils/tool_set.cpp
    src/indexer/simple_indexer.cpp
    src/indexer/indexer_tools.cpp
)

set(AGENT_SOURCES_FULL
    ${AGENT_SOURCES}
    src/agent/api_client.cpp
)

# -- Core library (with real HTTP client) -------------------------------------
add_library(agent_core STATIC ${AGENT_SOURCES_FULL})
target_include_directories(agent_core PUBLIC ${AGENT_INCLUDES})
target_link_libraries(agent_core PUBLIC ${PLATFORM_LIBS} Threads::Threads)
if(WARN_FLAGS)
    target_compile_options(agent_core PRIVATE ${WARN_FLAGS}
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g -O0>)
endif()

# -- Stub library (tests  --  stub replaces api_client, no TLS needed) ------------
add_library(agent_stub STATIC
    ${AGENT_SOURCES}           # all agent + utils (including tool_set.cpp)
    tests/stub_api_client.cpp  # stub replaces api_client.cpp
)
target_include_directories(agent_stub PUBLIC ${AGENT_INCLUDES})
target_link_libraries(agent_stub PUBLIC Threads::Threads)
if(MSVC)
    # Tests don't link against winhttp/ws2_32, but workspace.cpp needs nothing extra
endif()
if(WARN_FLAGS)
    target_compile_options(agent_stub PRIVATE ${WARN_FLAGS} -g -O0)
endif()

# -- Main executable ------------------------------------------------------------
add_executable(agent_runner src/main.cpp src/setup.cpp)
target_include_directories(agent_runner PRIVATE ${AGENT_INCLUDES})
target_link_libraries(agent_runner PRIVATE agent_core)
if(WARN_FLAGS)
    target_compile_options(agent_runner PRIVATE ${WARN_FLAGS}
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g -O0>)
endif()

# -- Tests ----------------------------------------------------------------------
enable_testing()

macro(add_agent_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${AGENT_INCLUDES})
    target_link_libraries(${name} PRIVATE agent_stub)
    if(WARN_FLAGS)
        target_compile_options(${name} PRIVATE ${WARN_FLAGS} -g -O0)
    endif()
    if(MSVC)
        target_compile_options(${name} PRIVATE /wd4189)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endmacro()

add_agent_test(test_models      tests/test_models.cpp)
add_agent_test(test_thread_pool tests/test_thread_pool.cpp)
add_agent_test(test_worker      tests/test_worker.cpp)
add_agent_test(test_manager     tests/test_manager.cpp)
add_agent_test(test_director    tests/test_director.cpp)
add_agent_test(test_v2_infra       tests/test_v2_infra.cpp)
add_agent_test(test_skill_registry tests/test_skill_registry.cpp)  # links agent_stub (has tool_set)

install(TARGETS agent_runner DESTINATION bin)
install(DIRECTORY prompts/ DESTINATION share/three_layer_agent/prompts)
install(DIRECTORY config/  DESTINATION share/three_layer_agent/config)
