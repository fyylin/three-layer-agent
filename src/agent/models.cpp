#include "agent/models.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace agent {

// -- helpers -------------------------------------------------------------------
static nlohmann::json mo() { return nlohmann::json::object(); }
static void ss(nlohmann::json& j, const char* k, const std::string& v) { j[k]=nlohmann::json(v); }
static void sb(nlohmann::json& j, const char* k, bool v)  { j[k]=nlohmann::json::from_bool(v); }
static void si(nlohmann::json& j, const char* k, int v)   { j[k]=nlohmann::json::from_int((int64_t)v); }
static void sd(nlohmann::json& j, const char* k, double v){ j[k]=nlohmann::json::from_float(v); }

static std::string now_iso8601() {
    using namespace std::chrono;
    auto now=system_clock::now();
    auto tt =system_clock::to_time_t(now);
    auto ms =duration_cast<milliseconds>(now.time_since_epoch())%1000;
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&tt);
#else
    gmtime_r(&tt,&tm);
#endif
    char buf[40];
    std::snprintf(buf,sizeof(buf),"%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
        tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,
        tm.tm_hour,tm.tm_min,tm.tm_sec,static_cast<long>(ms.count()));
    return buf;
}

// -- TaskStatus ----------------------------------------------------------------
std::string status_to_string(TaskStatus s) noexcept {
    switch(s){
        case TaskStatus::Pending:  return "pending";
        case TaskStatus::Running:  return "running";
        case TaskStatus::Done:     return "done";
        case TaskStatus::Failed:   return "failed";
        case TaskStatus::Rejected:  return "rejected";
        case TaskStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}
TaskStatus status_from_string(const std::string& s) {
    if(s=="pending")  return TaskStatus::Pending;
    if(s=="running")  return TaskStatus::Running;
    if(s=="done")     return TaskStatus::Done;
    if(s=="failed")   return TaskStatus::Failed;
    if(s=="rejected")  return TaskStatus::Rejected;
    if(s=="cancelled") return TaskStatus::Cancelled;
    throw std::runtime_error("unknown TaskStatus: "+s);
}

// -- Provider ------------------------------------------------------------------
std::string provider_to_string(Provider p) noexcept {
    switch(p){
        case Provider::Anthropic: return "anthropic";
        case Provider::OpenAI:    return "openai";
        case Provider::Azure:     return "azure";
        case Provider::Ollama:    return "ollama";
        case Provider::Custom:    return "custom";
    }
    return "anthropic";
}
Provider provider_from_string(const std::string& s) {
    if(s=="anthropic") return Provider::Anthropic;
    if(s=="openai")    return Provider::OpenAI;
    if(s=="azure")     return Provider::Azure;
    if(s=="ollama")    return Provider::Ollama;
    if(s=="custom")    return Provider::Custom;
    throw std::runtime_error("unknown provider: "+s);
}

// -- ModelSpec -----------------------------------------------------------------
void to_json(nlohmann::json& j, const ModelSpec& s){
    j=mo(); ss(j,"model",s.model);
    si(j,"max_tokens",s.max_tokens);
    sd(j,"temperature",s.temperature);
    sd(j,"top_p",s.top_p);
}
void from_json(const nlohmann::json& j, ModelSpec& s){
    if(j.contains("model"))       j.at("model").get_to(s.model);
    if(j.contains("max_tokens"))  j.at("max_tokens").get_to(s.max_tokens);
    if(j.contains("temperature")) j.at("temperature").get_to(s.temperature);
    if(j.contains("top_p"))       j.at("top_p").get_to(s.top_p);
}

// -- AtomicTask / AtomicResult -------------------------------------------------
void to_json(nlohmann::json& j,const AtomicTask& t){
    j=mo();ss(j,"id",t.id);ss(j,"parent_id",t.parent_id);
    ss(j,"description",t.description);ss(j,"tool",t.tool);ss(j,"input",t.input);
    ss(j,"input_from",t.input_from);
    // depends_on array
    nlohmann::json dep_arr=nlohmann::json::array();
    for(auto& d:t.depends_on) dep_arr.push_back(d);
    j["depends_on"]=dep_arr;
}
void from_json(const nlohmann::json& j,AtomicTask& t){
    if(j.contains("id"))          j.at("id").get_to(t.id);
    if(j.contains("description")) j.at("description").get_to(t.description);
    if(j.contains("parent_id"))   j.at("parent_id").get_to(t.parent_id);
    if(j.contains("tool"))        j.at("tool").get_to(t.tool);
    if(j.contains("input"))       j.at("input").get_to(t.input);
    if(j.contains("input_from"))  j.at("input_from").get_to(t.input_from);
    if(j.contains("depends_on")&&j["depends_on"].is_array())
        for(auto& d:j["depends_on"]) t.depends_on.push_back(d.get<std::string>());
}
void to_json(nlohmann::json& j,const AtomicResult& r){
    j=mo();ss(j,"task_id",r.task_id);ss(j,"status",status_to_string(r.status));
    ss(j,"output",r.output);ss(j,"error",r.error);
}
void from_json(const nlohmann::json& j,AtomicResult& r){
    j.at("task_id").get_to(r.task_id);
    r.status=status_from_string(j.at("status").get<std::string>());
    if(j.contains("output")) j.at("output").get_to(r.output);
    if(j.contains("error"))  j.at("error").get_to(r.error);
}

// -- SubTask / SubTaskReport ---------------------------------------------------
void to_json(nlohmann::json& j,const SubTask& t){
    j=mo();ss(j,"id",t.id);ss(j,"description",t.description);
    ss(j,"expected_output",t.expected_output);ss(j,"retry_feedback",t.retry_feedback);
}
void from_json(const nlohmann::json& j,SubTask& t){
    if(j.contains("id"))              j.at("id").get_to(t.id);
    if(j.contains("description"))     j.at("description").get_to(t.description);
    if(j.contains("expected_output")) j.at("expected_output").get_to(t.expected_output);
    if(j.contains("retry_feedback"))  j.at("retry_feedback").get_to(t.retry_feedback);
}
void to_json(nlohmann::json& j,const SubTaskReport& r){
    j=mo();ss(j,"subtask_id",r.subtask_id);ss(j,"status",status_to_string(r.status));
    ss(j,"summary",r.summary);ss(j,"issues",r.issues);
    nlohmann::json arr=nlohmann::json::array();
    for(auto& ar:r.results){nlohmann::json rj;to_json(rj,ar);arr.push_back(std::move(rj));}
    j["results"]=std::move(arr);
}
void from_json(const nlohmann::json& j,SubTaskReport& r){
    j.at("subtask_id").get_to(r.subtask_id);
    r.status=status_from_string(j.at("status").get<std::string>());
    if(j.contains("summary")) j.at("summary").get_to(r.summary);
    if(j.contains("issues"))  j.at("issues").get_to(r.issues);
    if(j.contains("results"))
        for(size_t i=0;i<j.at("results").size();++i){
            AtomicResult ar;from_json(j.at("results").at(i),ar);r.results.push_back(std::move(ar));
        }
}

// -- UserGoal / ReviewFeedback / FinalResult -----------------------------------
void to_json(nlohmann::json& j,const UserGoal& g){j=mo();ss(j,"description",g.description);}
void from_json(const nlohmann::json& j,UserGoal& g){j.at("description").get_to(g.description);}
void to_json(nlohmann::json& j,const ReviewFeedback& f){
    j=mo();ss(j,"subtask_id",f.subtask_id);sb(j,"approved",f.approved);ss(j,"feedback",f.feedback);
}
void from_json(const nlohmann::json& j,ReviewFeedback& f){
    j.at("subtask_id").get_to(f.subtask_id);
    j.at("approved").get_to(f.approved);
    if(j.contains("feedback")) j.at("feedback").get_to(f.feedback);
}
void to_json(nlohmann::json& j,const FinalResult& r){
    j=mo();ss(j,"status",status_to_string(r.status));ss(j,"answer",r.answer);
    ss(j,"error",r.error);ss(j,"started_at",r.started_at);ss(j,"finished_at",r.finished_at);
    nlohmann::json arr=nlohmann::json::array();
    for(auto& sr:r.sub_reports){nlohmann::json sj;to_json(sj,sr);arr.push_back(std::move(sj));}
    j["sub_reports"]=std::move(arr);
}

// -- AgentConfig serialisation -------------------------------------------------
void to_json(nlohmann::json& j, const AgentConfig& c){
    j=mo();
    ss(j,"provider",provider_to_string(c.provider));
    ss(j,"api_key",c.api_key);
    ss(j,"base_url",c.base_url);
    ss(j,"api_version",c.api_version);
    ss(j,"organization",c.organization);
    ss(j,"default_model",c.default_model);
    {nlohmann::json dm;to_json(dm,c.director_model);j["director_model"]=dm;}
    {nlohmann::json mm;to_json(mm,c.manager_model); j["manager_model"]=mm;}
    {nlohmann::json wm;to_json(wm,c.worker_model);  j["worker_model"]=wm;}
    si(j,"max_tokens",c.max_tokens);
    sd(j,"temperature",c.temperature);
    sd(j,"top_p",c.top_p);
    si(j,"request_timeout",c.request_timeout);
    si(j,"connect_timeout",c.connect_timeout);
    si(j,"max_network_retries",c.max_network_retries);
    si(j,"max_subtask_retries",c.max_subtask_retries);
    si(j,"max_atomic_retries",c.max_atomic_retries);
    si(j,"worker_threads",c.worker_threads);
    ss(j,"log_level",c.log_level);
    ss(j,"prompt_dir",c.prompt_dir);
    ss(j,"workspace_dir",c.workspace_dir);
    si(j,"memory_short_term_window",c.memory_short_term_window);
    j["memory_session_enabled"]=nlohmann::json::from_bool(c.memory_session_enabled);
    j["memory_long_term_enabled"]=nlohmann::json::from_bool(c.memory_long_term_enabled);
    si(j,"supervisor_poll_interval_ms",c.supervisor_poll_interval_ms);
    si(j,"supervisor_stuck_timeout_ms",c.supervisor_stuck_timeout_ms);
    si(j,"supervisor_max_fail_count",  c.supervisor_max_fail_count);
    j["supervisor_advisor_enabled"]=nlohmann::json::from_bool(c.supervisor_advisor_enabled);
    si(j,"supervisor_max_retries",     c.supervisor_max_retries);
    if(c.max_cost_per_run_usd>0)  ss(j,"max_cost_per_run_usd",std::to_string(c.max_cost_per_run_usd));
    if(c.max_tokens_per_run>0)    si(j,"max_tokens_per_run",(int64_t)c.max_tokens_per_run);
}

void from_json(const nlohmann::json& j, AgentConfig& c){
    if(j.contains("provider"))     c.provider=provider_from_string(j.at("provider").get<std::string>());
    if(j.contains("api_key"))      j.at("api_key").get_to(c.api_key);
    if(j.contains("base_url"))     j.at("base_url").get_to(c.base_url);
    if(j.contains("api_version"))  j.at("api_version").get_to(c.api_version);
    if(j.contains("organization")) j.at("organization").get_to(c.organization);
    if(j.contains("default_model"))j.at("default_model").get_to(c.default_model);
    // legacy compat: if old "model" field exists, treat as default_model
    if(j.contains("model") && !j.contains("default_model"))
        j.at("model").get_to(c.default_model);
    if(j.contains("director_model")) from_json(j.at("director_model"),c.director_model);
    if(j.contains("manager_model"))  from_json(j.at("manager_model"), c.manager_model);
    if(j.contains("worker_model"))   from_json(j.at("worker_model"),  c.worker_model);
    if(j.contains("max_tokens"))          j.at("max_tokens").get_to(c.max_tokens);
    if(j.contains("temperature"))         j.at("temperature").get_to(c.temperature);
    if(j.contains("top_p"))               j.at("top_p").get_to(c.top_p);
    if(j.contains("request_timeout"))     j.at("request_timeout").get_to(c.request_timeout);
    if(j.contains("connect_timeout"))     j.at("connect_timeout").get_to(c.connect_timeout);
    if(j.contains("max_network_retries")) j.at("max_network_retries").get_to(c.max_network_retries);
    if(j.contains("max_subtask_retries")) j.at("max_subtask_retries").get_to(c.max_subtask_retries);
    if(j.contains("max_atomic_retries"))  j.at("max_atomic_retries").get_to(c.max_atomic_retries);
    if(j.contains("worker_threads"))      j.at("worker_threads").get_to(c.worker_threads);
    if(j.contains("log_level"))           j.at("log_level").get_to(c.log_level);
    if(j.contains("prompt_dir"))          j.at("prompt_dir").get_to(c.prompt_dir);
    if(j.contains("workspace_dir"))       j.at("workspace_dir").get_to(c.workspace_dir);
    if(j.contains("memory_short_term_window")) j.at("memory_short_term_window").get_to(c.memory_short_term_window);
    if(j.contains("memory_session_enabled"))   j.at("memory_session_enabled").get_to(c.memory_session_enabled);
    if(j.contains("memory_long_term_enabled"))  j.at("memory_long_term_enabled").get_to(c.memory_long_term_enabled);
    if(j.contains("supervisor_poll_interval_ms")) j.at("supervisor_poll_interval_ms").get_to(c.supervisor_poll_interval_ms);
    if(j.contains("supervisor_stuck_timeout_ms")) j.at("supervisor_stuck_timeout_ms").get_to(c.supervisor_stuck_timeout_ms);
    if(j.contains("supervisor_max_fail_count"))   j.at("supervisor_max_fail_count").get_to(c.supervisor_max_fail_count);
    if(j.contains("supervisor_advisor_enabled"))  j.at("supervisor_advisor_enabled").get_to(c.supervisor_advisor_enabled);
    if(j.contains("supervisor_max_retries"))      j.at("supervisor_max_retries").get_to(c.supervisor_max_retries);
    if(j.contains("max_cost_per_run_usd")) j.at("max_cost_per_run_usd").get_to(c.max_cost_per_run_usd);
    if(j.contains("max_tokens_per_run"))    j.at("max_tokens_per_run").get_to(c.max_tokens_per_run);
}

AgentConfig AgentConfig::load(const std::string& path){
    std::ifstream f(path);
    if(!f.is_open()) throw std::runtime_error("AgentConfig: cannot open: "+path);
    std::ostringstream ss; ss<<f.rdbuf();
    auto j=nlohmann::json::parse(ss.str());
    AgentConfig cfg; from_json(j,cfg); return cfg;
}

void AgentConfig::save(const std::string& path) const {
    nlohmann::json j; to_json(j, *this);
    std::ofstream f(path);
    if(!f.is_open()) throw std::runtime_error("AgentConfig::save: cannot open: "+path);
    f << j.dump(2) << "\n";
}

} // namespace agent

// -- AgentConfig::validate() --------------------------------------------------
void agent::AgentConfig::validate() const {
    // API key required for cloud providers
    if (provider != Provider::Ollama && provider != Provider::Custom) {
        if (api_key.empty() || api_key == "YOUR_API_KEY_HERE")
            throw std::runtime_error(
                "AgentConfig: api_key is not set. "
                "Run with --setup or set api_key in config.json.");
    }
    // Model must not be empty
    if (default_model.empty())
        throw std::runtime_error("AgentConfig: default_model is empty.");
    // Reasonable bounds
    if (max_tokens < 64 || max_tokens > 32768)
        throw std::runtime_error(
            "AgentConfig: max_tokens out of range [64, 32768]: " +
            std::to_string(max_tokens));
    if (worker_threads < 1 || worker_threads > 32)
        throw std::runtime_error(
            "AgentConfig: worker_threads out of range [1, 32]: " +
            std::to_string(worker_threads));
    if (request_timeout < 5 || request_timeout > 600)
        throw std::runtime_error(
            "AgentConfig: request_timeout out of range [5, 600]: " +
            std::to_string(request_timeout));
}
