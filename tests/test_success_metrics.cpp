#include <gtest/gtest.h>
#include "indexer/simple_indexer.hpp"

TEST(SuccessMetrics, FindDefinition) {
    auto runner = [](const std::string& cmd) {
        return system((cmd + " 2>&1").c_str()) == 0 ? "src/agent/worker.cpp:10:class Worker {" : "";
    };
    indexer::SimpleIndexer idx(runner);
    auto info = idx.find_definition("Worker", ".");
    EXPECT_EQ(info.name, "Worker");
    EXPECT_FALSE(info.definition.file.empty());
}

TEST(SuccessMetrics, FindCallers) {
    auto runner = [](const std::string& cmd) {
        return "src/main.cpp:20:worker.execute();\nsrc/test.cpp:15:w.execute();";
    };
    indexer::SimpleIndexer idx(runner);
    auto refs = idx.find_references("execute", ".");
    EXPECT_GE(refs.size(), 2);
}
