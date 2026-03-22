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
# SQLite3 disabled on Windows to avoid MSYS2 header conflicts
if(NOT WIN32)
    find_package(SQLite3 QUIET)
endif()

# -- Source lists --------------------------------------------------------------
set(AGENT_SOURCES
    src/agent/models.cpp
    src/agent/tool_registry.cpp
    src/agent/tool_cache.cpp
    src/agent/parallel_executor.cpp
    src/agent/async_message_bus.cpp
    src/agent/worker_agent.cpp
    src/agent/manager_agent.cpp
    src/agent/director_agent.cpp
    src/agent/supervisor_agent.cpp
    src/agent/advisor_agent.cpp
    src/agent/reflection.cpp
    src/agent/skill_registry.cpp
    src/agent/manager_pool.cpp
    src/agent/task_router.cpp
    src/agent/task_context.cpp
    src/agent/preflight_checker.cpp
    src/agent/dependency_analyzer.cpp
    src/agent/experience_db.cpp
    src/agent/retry_policy.cpp
    src/agent/circuit_breaker.cpp
    src/agent/request_deduplicator.cpp
    src/agent/event_bus.cpp
    src/agent/worker_executor.cpp
    src/agent/manager_dispatcher.cpp
    src/agent/director_decomposer.cpp
    src/agent/file_type_handler.cpp
    src/agent/conversation_context.cpp
    src/agent/tool_composer.cpp
    src/agent/global_summary.cpp
    src/cli/command_system.cpp
    src/cli/command_registry.cpp
    src/cli/readline_helper.cpp
    src/utils/logger.cpp
    src/utils/message_bus.cpp
    src/utils/memory_store.cpp
    src/utils/workspace.cpp
    src/utils/file_lock.cpp
    src/utils/structured_log.cpp
    src/utils/tool_set.cpp
    src/indexer/simple_indexer.cpp
    src/indexer/indexer_tools.cpp
    src/indexer/sqlite_indexer.cpp
    src/indexer/treesitter_indexer.cpp
    src/api/batch_api_client.cpp
    src/utils/token_budget_allocator.cpp
    src/utils/experience_replay_engine.cpp
)

set(AGENT_SOURCES_FULL
    ${AGENT_SOURCES}
    src/agent/api_client.cpp
)

# -- Core library (with real HTTP client) -------------------------------------
add_library(agent_core STATIC ${AGENT_SOURCES_FULL})
target_include_directories(agent_core PUBLIC ${AGENT_INCLUDES})
target_link_libraries(agent_core PUBLIC ${PLATFORM_LIBS} Threads::Threads)
if(SQLite3_FOUND)
    target_link_libraries(agent_core PUBLIC SQLite::SQLite3)
    target_compile_definitions(agent_core PUBLIC HAVE_SQLITE3)
endif()
if(WARN_FLAGS)
    target_compile_options(agent_core PRIVATE ${WARN_FLAGS}
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g -O0>)
endif()

# -- Stub library (tests  --  stub replaces api_client, no TLS needed) ------------
add_library(agent_stub STATIC
    ${AGENT_SOURCES}           # all agent + utils (including tool_set.cpp)
    tests/stub_api_client.cpp  # stub replaces api_client.cpp
    src/utils/multimodal_handler.cpp
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
add_custom_command(TARGET agent_runner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:agent_runner>/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:agent_runner>/prompts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/agent_config.json"
            "$<TARGET_FILE_DIR:agent_runner>/config/agent_config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/prompts"
            "$<TARGET_FILE_DIR:agent_runner>/prompts"
)

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
add_agent_test(test_phase_integration tests/test_phase_integration.cpp)
add_agent_test(test_chinese_support tests/test_chinese_support.cpp)
add_agent_test(test_getcwd_chinese tests/test_getcwd_chinese.cpp)
add_agent_test(test_all_tools_chinese tests/test_all_tools_chinese.cpp)
add_agent_test(test_workspace_chinese tests/test_workspace_chinese.cpp)
add_agent_test(test_improvements tests/test_improvements.cpp)
add_agent_test(test_phase2 tests/test_phase2.cpp)
add_agent_test(test_integration tests/test_integration.cpp)

install(TARGETS agent_runner DESTINATION bin)
install(DIRECTORY prompts/ DESTINATION share/three_layer_agent/prompts)
install(DIRECTORY config/  DESTINATION share/three_layer_agent/config)
if(EXISTS "${CMAKE_SOURCE_DIR}/test_indexer_demo.cpp")
    add_executable(test_indexer_demo test_indexer_demo.cpp)
    target_include_directories(test_indexer_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(test_indexer_demo PRIVATE agent_stub)
endif()

add_executable(test_real_indexer tests/test_real_indexer.cpp)
target_include_directories(test_real_indexer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_real_indexer PRIVATE agent_stub)


