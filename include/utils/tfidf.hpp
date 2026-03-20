#pragma once
// =============================================================================
// include/utils/tfidf.hpp  --  Lightweight TF-IDF semantic retrieval
// Pure C++17, no external dependencies.
// Used by ReflectionEngine to find relevant past experiences by semantic
// similarity rather than exact keyword matching.
// =============================================================================
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <mutex>

namespace agent {

// ---------------------------------------------------------------------------
// Simple tokenizer: split on whitespace + punctuation, lowercase
// ---------------------------------------------------------------------------
inline std::vector<std::string> tfidf_tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string tok;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c > 127) {  // ASCII alnum OR any UTF-8 byte
            tok += (char)std::tolower(c);
        } else if (!tok.empty()) {
            if (tok.size() >= 2) tokens.push_back(tok);
            tok.clear();
        }
    }
    if (tok.size() >= 2) tokens.push_back(tok);
    return tokens;
}

// ---------------------------------------------------------------------------
// TFIDFIndex: maintains a corpus of (key, text) documents and supports
// cosine-similarity retrieval.
// ---------------------------------------------------------------------------
class TFIDFIndex {
public:
    struct Document {
        std::string key;
        std::string text;
        std::unordered_map<std::string, double> tf;  // term frequency
    };

    // Add or update a document
    void upsert(const std::string& key, const std::string& text) {
        std::lock_guard<std::mutex> lk(mu_);
        // Remove old version if present
        for (auto it = docs_.begin(); it != docs_.end(); ++it) {
            if (it->key == key) { docs_.erase(it); break; }
        }
        Document doc;
        doc.key  = key;
        doc.text = text;
        auto tokens = tfidf_tokenize(text);
        if (tokens.empty()) return;
        // Compute TF
        std::unordered_map<std::string, int> counts;
        for (auto& t : tokens) counts[t]++;
        double total = (double)tokens.size();
        for (auto& [t, c] : counts) {
            doc.tf[t] = (double)c / total;
            df_[t]++;   // document frequency
        }
        docs_.push_back(std::move(doc));
        dirty_ = true;
    }

    // Remove a document
    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = docs_.begin(); it != docs_.end(); ++it) {
            if (it->key == key) {
                for (auto& [t,_] : it->tf) { if (df_[t]>0) df_[t]--; }
                docs_.erase(it);
                dirty_ = true;
                break;
            }
        }
    }

    // Return top-k most similar documents to query (similarity > threshold)
    struct Hit { std::string key; std::string text; double score; };
    std::vector<Hit> query(const std::string& q, int topk=3,
                           double threshold=0.05) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (docs_.empty()) return {};

        auto q_tokens = tfidf_tokenize(q);
        if (q_tokens.empty()) return {};

        // Query TF
        std::unordered_map<std::string, double> q_tf;
        for (auto& t : q_tokens) q_tf[t] += 1.0 / (double)q_tokens.size();

        double n = (double)docs_.size();
        std::vector<Hit> hits;

        for (const auto& doc : docs_) {
            double dot=0, q_norm=0, d_norm=0;
            // Compute TF-IDF cosine similarity
            for (auto& [t, qtf] : q_tf) {
                auto dfit = df_.find(t);
                double idf = (dfit!=df_.end() && dfit->second>0)
                    ? std::log(n / (double)dfit->second) + 1.0
                    : 0.0;
                double q_tfidf = qtf * idf;
                q_norm += q_tfidf * q_tfidf;
                auto dtfit = doc.tf.find(t);
                if (dtfit != doc.tf.end()) {
                    double d_tfidf = dtfit->second * idf;
                    dot += q_tfidf * d_tfidf;
                    d_norm += d_tfidf * d_tfidf;
                }
            }
            // Also accumulate doc norm over all its terms
            for (auto& [t, dtf] : doc.tf) {
                auto dfit = df_.find(t);
                if (dfit==df_.end()) continue;
                double idf = std::log(n / (double)dfit->second) + 1.0;
                d_norm += dtf * idf * dtf * idf;
            }
            double denom = std::sqrt(q_norm) * std::sqrt(d_norm);
            double sim   = (denom > 1e-9) ? dot / denom : 0.0;
            if (sim >= threshold) hits.push_back({doc.key, doc.text, sim});
        }

        std::sort(hits.begin(), hits.end(),
                  [](const Hit& a, const Hit& b){ return a.score > b.score; });
        if ((int)hits.size() > topk) hits.resize(topk);
        return hits;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return docs_.size();
    }

private:
    mutable std::mutex mu_;
    std::vector<Document>                    docs_;
    std::unordered_map<std::string, int>     df_;   // document frequency per term
    bool                                     dirty_ = false;
};

}  // namespace agent
