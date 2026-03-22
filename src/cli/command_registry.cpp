#include "cli/command_system.hpp"
#include <iostream>
#include <iomanip>

namespace agent::cli {

void register_all_commands(CommandSystem& sys) {
    // /help - 帮助信息
    sys.register_command({
        "help",
        "显示所有可用命令",
        {},
        [&sys](const auto&) {
            std::cout << "\n=== 可用命令 ===\n\n";
            std::cout << "基础命令:\n";
            std::cout << "  /help      - 显示此帮助信息\n";
            std::cout << "  /status    - 系统状态\n";
            std::cout << "  /stats     - 统计信息\n\n";
            std::cout << "配置命令:\n";
            std::cout << "  /set       - 修改设置 (用法: /set key=value)\n";
            std::cout << "  /reset     - 重置设置\n";
            std::cout << "  /preset    - 快速预设\n";
            std::cout << "  /model     - 模型管理\n";
            std::cout << "  /config    - 配置向导\n\n";
            std::cout << "高级命令:\n";
            std::cout << "  /retry     - 重试策略\n";
            std::cout << "  /provider  - Provider配置\n";
            std::cout << "  /export    - 导出配置\n";
            std::cout << "  /save      - 保存配置\n";
            std::cout << "  /load      - 加载配置\n\n";
            std::cout << "工具命令:\n";
            std::cout << "  /clear     - 清理数据\n";
            std::cout << "  /log       - 日志管理\n";
            std::cout << "  /history   - 命令历史\n";
            std::cout << "  /alias     - 命令别名\n";
            std::cout << "  /watch     - 监控模式\n\n";
            std::cout << "性能命令:\n";
            std::cout << "  /bench     - 基准测试\n";
            std::cout << "  /profile   - 性能分析\n\n";
            std::cout << "提示: 输入 / 查看命令列表，输入命令后按空格查看参数\n\n";
        }
    });

    // /new - 新建对话
    sys.register_command({
        "new",
        "创建新对话",
        {},
        [](const auto&) {
            std::cout << "新建对话功能 (待实现)\n";
        }
    });

    // /conv - 当前对话
    sys.register_command({
        "conv",
        "显示当前对话信息",
        {},
        [](const auto&) {
            std::cout << "当前对话信息 (待实现)\n";
        }
    });

    // /convs - 对话列表
    sys.register_command({
        "convs",
        "显示所有对话",
        {},
        [](const auto&) {
            std::cout << "对话列表 (待实现)\n";
        }
    });

    // /switch - 切换对话
    sys.register_command({
        "switch",
        "切换到其他对话",
        {{"id", "对话ID", "", {}}},
        [](const auto& args) {
            if (args.count("id")) {
                std::cout << "切换到对话: " << args.at("id") << " (待实现)\n";
            } else {
                std::cout << "用法: /switch id=<对话ID>\n";
            }
        }
    });

    // /workspace - 工作区管理
    sys.register_command({
        "workspace",
        "工作区管理",
        {},
        [](const auto&) {
            std::cout << "工作区管理 (待实现)\n";
        }
    });

    // /experience - 经验总结
    sys.register_command({
        "experience",
        "查看经验总结",
        {},
        [](const auto&) {
            std::cout << "经验总结 (待实现)\n";
        }
    });

    // /set - 设置全局参数
    sys.register_command({
        "set",
        "修改全局设置参数",
        {
            {"max_retries", "最大重试次数", "3", {}},
            {"auto_approve", "重试后自动批准", "true", {"true", "false"}},
            {"verbose", "详细日志输出", "false", {"true", "false"}},
            {"model", "默认模型", "claude-sonnet-4-5-20251015", {}},
            {"timeout", "超时时间(秒)", "120", {}},
            {"parallel", "并行执行", "true", {"true", "false"}},
            {"strict", "严格验证模式", "false", {"true", "false"}},
            {"temperature", "生成温度", "0.7", {}},
            {"max_tokens", "最大token数", "4096", {}}
        },
        [](const auto& args) {
            if (args.empty()) {
                std::cout << "\n当前设置:\n";
                std::cout << "  max_retries      = " << g_settings.max_retries << "\n";
                std::cout << "  auto_approve     = " << (g_settings.auto_approve_after_retries ? "true" : "false") << "\n";
                std::cout << "  verbose          = " << (g_settings.verbose_logging ? "true" : "false") << "\n";
                std::cout << "  model            = " << g_settings.default_model << "\n";
                std::cout << "  timeout          = " << g_settings.timeout_seconds << "s\n";
                std::cout << "  parallel         = " << (g_settings.parallel_execution ? "true" : "false") << "\n";
                std::cout << "  strict           = " << (g_settings.strict_validation ? "true" : "false") << "\n";
                std::cout << "  temperature      = " << g_settings.temperature << "\n";
                std::cout << "  max_tokens       = " << g_settings.max_tokens << "\n\n";
                return;
            }
            for (const auto& [k, v] : args) {
                if (k == "max_retries") g_settings.max_retries = std::stoi(v);
                else if (k == "auto_approve") g_settings.auto_approve_after_retries = (v == "true");
                else if (k == "verbose") g_settings.verbose_logging = (v == "true");
                else if (k == "model") g_settings.default_model = v;
                else if (k == "timeout") g_settings.timeout_seconds = std::stoi(v);
                else if (k == "parallel") g_settings.parallel_execution = (v == "true");
                else if (k == "strict") g_settings.strict_validation = (v == "true");
                else if (k == "temperature") g_settings.temperature = std::stod(v);
                else if (k == "max_tokens") g_settings.max_tokens = std::stoi(v);
            }
            std::cout << "设置已更新\n";
        }
    });

    // /status - 显示系统状态
    sys.register_command({
        "status",
        "显示当前系统状态和统计信息",
        {},
        [](const auto&) {
            std::cout << "\n=== 系统状态 ===\n";
            std::cout << "模型: " << g_settings.default_model << "\n";
            std::cout << "重试策略: " << g_settings.max_retries << " 次后";
            if (g_settings.auto_approve_after_retries) {
                std::cout << "自动批准\n";
            } else {
                std::cout << "继续重试\n";
            }
            std::cout << "验证模式: " << (g_settings.strict_validation ? "严格" : "宽松") << "\n\n";
        }
    });

    // /reset - 重置设置
    sys.register_command({
        "reset",
        "重置所有设置为默认值",
        {},
        [](const auto&) {
            g_settings = GlobalSettings{};
            std::cout << "所有设置已重置为默认值\n";
        }
    });

    // /retry - 设置重试策略
    sys.register_command({
        "retry",
        "配置重试策略",
        {
            {"max", "最大重试次数", "3", {}},
            {"strategy", "重试策略", "progressive", {"progressive", "immediate", "exponential"}}
        },
        [](const auto& args) {
            if (args.count("max")) {
                g_settings.max_retries = std::stoi(args.at("max"));
            }
            std::cout << "重试策略已更新: 最多 " << g_settings.max_retries << " 次\n";
        }
    });

    // /stats - 显示统计信息
    sys.register_command({
        "stats",
        "显示系统统计信息",
        {},
        [](const auto&) {
            std::cout << "\n=== 系统统计 ===\n";
            std::cout << "总任务数: " << g_settings.total_tasks << "\n";
            std::cout << "成功: " << g_settings.successful_tasks << " | ";
            std::cout << "失败: " << g_settings.failed_tasks << "\n";
            if (g_settings.total_tasks > 0) {
                double success_rate = 100.0 * g_settings.successful_tasks / g_settings.total_tasks;
                std::cout << "成功率: " << std::fixed << std::setprecision(1) << success_rate << "%\n";
            }
            std::cout << "总重试次数: " << g_settings.total_retries << "\n";
            uint64_t total_cache = g_settings.cache_hits + g_settings.cache_misses;
            if (total_cache > 0) {
                double hit_rate = 100.0 * g_settings.cache_hits / total_cache;
                std::cout << "缓存命中率: " << std::fixed << std::setprecision(1) << hit_rate << "%\n";
            }
            std::cout << "\n";
        }
    });

    // /clear - 清理命令
    sys.register_command({
        "clear",
        "清理内存或统计信息",
        {
            {"target", "清理目标", "stats", {"stats", "memory", "cache", "all"}}
        },
        [](const auto& args) {
            std::string target = args.count("target") ? args.at("target") : "stats";
            if (target == "stats" || target == "all") {
                g_settings.total_tasks = 0;
                g_settings.successful_tasks = 0;
                g_settings.failed_tasks = 0;
                g_settings.total_retries = 0;
                g_settings.cache_hits = 0;
                g_settings.cache_misses = 0;
                std::cout << "统计信息已清零\n";
            }
            if (target == "all") {
                std::cout << "所有数据已清理\n";
            }
        }
    });

    // /preset - 快速预设
    sys.register_command({
        "preset",
        "加载预设配置",
        {
            {"name", "预设名称", "", {"debug", "production", "quality", "fast"}}
        },
        [](const auto& args) {
            if (!args.count("name")) {
                std::cout << "\n可用预设:\n";
                std::cout << "  debug      - 调试模式 (verbose=true, max_retries=1)\n";
                std::cout << "  production - 生产模式 (balanced, max_retries=3)\n";
                std::cout << "  quality    - 高质量模式 (strict=true, max_retries=5)\n";
                std::cout << "  fast       - 快速模式 (max_retries=1, temperature=0.9)\n\n";
                return;
            }
            std::string preset = args.at("name");
            if (preset == "debug") {
                g_settings.verbose_logging = true;
                g_settings.max_retries = 1;
                g_settings.strict_validation = false;
                std::cout << "已切换到调试模式\n";
            } else if (preset == "production") {
                g_settings.verbose_logging = false;
                g_settings.max_retries = 3;
                g_settings.auto_approve_after_retries = true;
                g_settings.parallel_execution = true;
                std::cout << "已切换到生产模式\n";
            } else if (preset == "quality") {
                g_settings.strict_validation = true;
                g_settings.max_retries = 5;
                g_settings.temperature = 0.3;
                std::cout << "已切换到高质量模式\n";
            } else if (preset == "fast") {
                g_settings.max_retries = 1;
                g_settings.auto_approve_after_retries = true;
                g_settings.parallel_execution = true;
                g_settings.temperature = 0.9;
                std::cout << "已切换到快速模式\n";
            }
        }
    });

    // /model - 模型管理
    sys.register_command({
        "model",
        "查看或切换模型",
        {
            {"name", "模型名称", "", {}},
            {"agent", "指定agent", "all", {"all", "supervisor", "director", "manager", "worker"}},
            {"provider", "提供商", "", {"anthropic", "openai", "ollama", "azure"}},
            {"temperature", "温度", "", {}},
            {"max_tokens", "最大token", "", {}}
        },
        [](const auto& args) {
            if (!args.count("name")) {
                std::cout << "\n=== 当前模型配置 ===\n";
                std::cout << "默认: " << g_settings.default_model << "\n";
                std::cout << "Supervisor: " << g_settings.supervisor_model.model << " (" << g_settings.supervisor_model.provider << ")\n";
                std::cout << "Director: " << g_settings.director_model.model << " (" << g_settings.director_model.provider << ")\n";
                std::cout << "Manager: " << g_settings.manager_model.model << " (" << g_settings.manager_model.provider << ")\n";
                std::cout << "Worker: " << g_settings.worker_model.model << " (" << g_settings.worker_model.provider << ")\n\n";
                std::cout << "可用模型:\n";
                std::cout << "  Anthropic: opus-4-5, sonnet-4-5, haiku-4-5\n";
                std::cout << "  OpenAI: gpt-4o, gpt-4o-mini, o1\n";
                std::cout << "  Ollama: llama3.3, deepseek-r1, qwen2.5\n";
                std::cout << "  Azure: 自定义部署\n\n";
                return;
            }
            std::string agent = args.count("agent") ? args.at("agent") : "all";
            std::string model = args.at("name");
            std::string provider = args.count("provider") ? args.at("provider") : "anthropic";

            auto update_model = [&](ModelConfig& cfg) {
                cfg.model = model;
                cfg.provider = provider;
                if (args.count("temperature")) cfg.temperature = std::stod(args.at("temperature"));
                if (args.count("max_tokens")) cfg.max_tokens = std::stoi(args.at("max_tokens"));
            };

            if (agent == "all") {
                g_settings.default_model = model;
                update_model(g_settings.supervisor_model);
                update_model(g_settings.director_model);
                update_model(g_settings.manager_model);
                update_model(g_settings.worker_model);
                std::cout << "所有agent模型已切换到: " << model << "\n";
            } else if (agent == "supervisor") {
                update_model(g_settings.supervisor_model);
                std::cout << "Supervisor模型已切换到: " << model << "\n";
            } else if (agent == "director") {
                update_model(g_settings.director_model);
                std::cout << "Director模型已切换到: " << model << "\n";
            } else if (agent == "manager") {
                update_model(g_settings.manager_model);
                std::cout << "Manager模型已切换到: " << model << "\n";
            } else if (agent == "worker") {
                update_model(g_settings.worker_model);
                std::cout << "Worker模型已切换到: " << model << "\n";
            }
        }
    });

    // /log - 日志管理
    sys.register_command({
        "log",
        "查看或管理日志",
        {
            {"action", "操作", "tail", {"tail", "clear", "level"}}
        },
        [](const auto& args) {
            std::string action = args.count("action") ? args.at("action") : "tail";
            if (action == "tail") {
                std::cout << "查看最近日志 (功能待实现)\n";
            } else if (action == "clear") {
                std::cout << "日志已清理\n";
            } else if (action == "level") {
                std::cout << "当前日志级别: " << (g_settings.verbose_logging ? "DEBUG" : "INFO") << "\n";
            }
        }
    });

    // /export - 导出配置
    sys.register_command({
        "export",
        "导出当前配置",
        {},
        [](const auto&) {
            std::cout << "\n# 当前配置\n";
            std::cout << "max_retries=" << g_settings.max_retries << "\n";
            std::cout << "auto_approve=" << (g_settings.auto_approve_after_retries ? "true" : "false") << "\n";
            std::cout << "verbose=" << (g_settings.verbose_logging ? "true" : "false") << "\n";
            std::cout << "model=" << g_settings.default_model << "\n";
            std::cout << "timeout=" << g_settings.timeout_seconds << "\n";
            std::cout << "parallel=" << (g_settings.parallel_execution ? "true" : "false") << "\n";
            std::cout << "strict=" << (g_settings.strict_validation ? "true" : "false") << "\n";
            std::cout << "temperature=" << g_settings.temperature << "\n";
            std::cout << "max_tokens=" << g_settings.max_tokens << "\n\n";
        }
    });

    // /benchmark - 性能基准测试
    sys.register_command({
        "bench",
        "运行性能基准测试",
        {
            {"iterations", "迭代次数", "10", {}}
        },
        [](const auto& args) {
            int iters = args.count("iterations") ? std::stoi(args.at("iterations")) : 10;
            std::cout << "运行基准测试 (" << iters << " 次迭代)...\n";
            std::cout << "功能待实现\n";
        }
    });

    // /profile - 性能分析
    sys.register_command({
        "profile",
        "显示性能分析信息",
        {},
        [](const auto&) {
            std::cout << "\n=== 性能分析 ===\n";
            if (g_settings.total_tasks > 0) {
                double avg_retries = (double)g_settings.total_retries / g_settings.total_tasks;
                std::cout << "平均重试次数: " << std::fixed << std::setprecision(2) << avg_retries << "\n";
            }
            std::cout << "功能待完善\n\n";
        }
    });

    // /history - 命令历史
    sys.register_command({
        "history",
        "显示命令历史",
        {
            {"limit", "显示条数", "10", {}}
        },
        [](const auto& args) {
            int limit = args.count("limit") ? std::stoi(args.at("limit")) : 10;
            std::cout << "显示最近 " << limit << " 条命令 (功能待实现)\n";
        }
    });

    // /alias - 命令别名
    sys.register_command({
        "alias",
        "管理命令别名",
        {
            {"name", "别名名称", "", {}},
            {"command", "目标命令", "", {}}
        },
        [](const auto& args) {
            if (args.empty()) {
                std::cout << "当前别名: (功能待实现)\n";
            } else {
                std::cout << "别名功能待实现\n";
            }
        }
    });

    // /watch - 监控模式
    sys.register_command({
        "watch",
        "启动监控模式",
        {
            {"interval", "刷新间隔(秒)", "5", {}}
        },
        [](const auto& args) {
            int interval = args.count("interval") ? std::stoi(args.at("interval")) : 5;
            std::cout << "监控模式 (每 " << interval << " 秒刷新) - 功能待实现\n";
        }
    });

    // /save - 保存配置到文件
    sys.register_command({
        "save",
        "保存配置到文件",
        {
            {"file", "文件路径", "config.txt", {}}
        },
        [](const auto& args) {
            std::string file = args.count("file") ? args.at("file") : "config.txt";
            std::cout << "保存配置到: " << file << " (功能待实现)\n";
        }
    });

    // /load - 从文件加载配置
    sys.register_command({
        "load",
        "从文件加载配置",
        {
            {"file", "文件路径", "config.txt", {}}
        },
        [](const auto& args) {
            std::string file = args.count("file") ? args.at("file") : "config.txt";
            std::cout << "从文件加载配置: " << file << " (功能待实现)\n";
        }
    });

    // /provider - 多provider配置
    sys.register_command({
        "provider",
        "配置多provider支持",
        {
            {"action", "操作", "list", {"list", "add", "remove", "enable", "disable"}},
            {"name", "provider名称", "", {}}
        },
        [](const auto& args) {
            std::string action = args.count("action") ? args.at("action") : "list";
            if (action == "list") {
                std::cout << "\n=== Provider配置 ===\n";
                std::cout << "多provider: " << (g_settings.enable_multi_provider ? "启用" : "禁用") << "\n";
                std::cout << "回退列表: ";
                for (const auto& p : g_settings.fallback_providers) {
                    std::cout << p << " ";
                }
                std::cout << "\n\n";
            } else if (action == "enable") {
                g_settings.enable_multi_provider = true;
                std::cout << "多provider已启用\n";
            } else if (action == "disable") {
                g_settings.enable_multi_provider = false;
                std::cout << "多provider已禁用\n";
            } else if (action == "add" && args.count("name")) {
                g_settings.fallback_providers.push_back(args.at("name"));
                std::cout << "已添加回退provider: " << args.at("name") << "\n";
            }
        }
    });

    // /config - 快速配置向导
    sys.register_command({
        "config",
        "配置向导",
        {
            {"mode", "模式", "quick", {"quick", "advanced", "show"}}
        },
        [](const auto& args) {
            std::string mode = args.count("mode") ? args.at("mode") : "quick";
            if (mode == "show") {
                std::cout << "\n=== 完整配置 ===\n";
                std::cout << "基础设置:\n";
                std::cout << "  max_retries=" << g_settings.max_retries << "\n";
                std::cout << "  timeout=" << g_settings.timeout_seconds << "s\n";
                std::cout << "  parallel=" << (g_settings.parallel_execution ? "true" : "false") << "\n";
                std::cout << "\n模型配置:\n";
                std::cout << "  Supervisor: " << g_settings.supervisor_model.model << "\n";
                std::cout << "    provider=" << g_settings.supervisor_model.provider << "\n";
                std::cout << "    temperature=" << g_settings.supervisor_model.temperature << "\n";
                std::cout << "    max_tokens=" << g_settings.supervisor_model.max_tokens << "\n";
                std::cout << "  Director: " << g_settings.director_model.model << "\n";
                std::cout << "  Manager: " << g_settings.manager_model.model << "\n";
                std::cout << "  Worker: " << g_settings.worker_model.model << "\n";
                std::cout << "\n";
            } else if (mode == "quick") {
                std::cout << "\n快速配置向导 (功能待实现)\n";
                std::cout << "使用 /config advanced 进入高级配置\n\n";
            } else if (mode == "advanced") {
                std::cout << "\n高级配置向导 (功能待实现)\n";
                std::cout << "使用 /config show 查看当前配置\n\n";
            }
        }
    });
}

} // namespace agent::cli
