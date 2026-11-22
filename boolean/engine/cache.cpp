#include <vcl.h>
#pragma hdrstop
#include "cache.hpp"
#include "control.hpp"
#include "target.hpp"
#include "../verification/native.hpp"
#include <deque>
#include <mutex>
namespace b2f { namespace engine {
namespace {
constexpr unsigned cacheVersion=1;
constexpr size_t byteLimit=8*1024*1024,entryLimit=4;
struct Entry { std::string key,payload,digest;unsigned version=cacheVersion; };
std::deque<Entry> entries;
std::mutex gate;
size_t storedBytes=0;
std::string cache_key(const Problem &p,const Options &o) {
    Json cubes=Json::array();for(auto c:p.original_on)cubes.push_back({c.care,c.bits});
    return sha256(Json({{"cache_version",cacheVersion},{"ir_version",schema_version},
        {"search_version",2},{"stage",7},{"truth",p.truth.json()},{"original_on",cubes},
        {"options",{{"work_limit",o.work_limit},{"max_nodes",o.max_nodes},{"table_bytes",o.table_bytes},
                    {"max_candidates",o.max_candidates},{"allow_full_lut",o.allow_full_lut}}}}).dump());
}
}
Json cache_status() {
    std::lock_guard<std::mutex> lock(gate);
    return {{"version",cacheVersion},{"entries",entries.size()},{"stored_payload_bytes",storedBytes},
            {"payload_byte_limit",byteLimit},{"entry_limit",entryLimit},{"persistent",false}};
}
void clear_cache() { std::lock_guard<std::mutex> lock(gate);entries.clear();storedBytes=0; }
void damage_cache_for_check(const std::string &kind) {
    std::lock_guard<std::mutex> lock(gate);
    if(entries.empty())throw std::runtime_error("no cache entry to damage");
    if(kind=="hash")entries.back().payload+=" ";
    else if(kind=="version")++entries.back().version;
    else throw std::runtime_error("damage kind must be hash or version");
    storedBytes=0;for(auto &e:entries)storedBytes+=e.payload.size();
}
Json cached_search(const Problem &p,const Options &options,bool enabled,Json &diagnostic,const Json &profile) {
    checkpoint();std::string key=cache_key(p,options);Entry saved;bool found=false,rejected=false,hit=false;
    if(enabled) {
        std::lock_guard<std::mutex> lock(gate);
        for(auto i=entries.begin();i!=entries.end();++i)if(i->key==key) {
            saved=*i;found=true;break;
        }
    }
    Json logical;
    if(found) {
        if(saved.version==cacheVersion && saved.digest==sha256(saved.payload)) {
            try { logical=Json::parse(saved.payload);hit=true; }
            catch(const std::exception &) { rejected=true; }
        } else rejected=true;
        if(rejected) {
            std::lock_guard<std::mutex> lock(gate);
            for(auto i=entries.begin();i!=entries.end();++i)if(i->key==key) { storedBytes-=i->payload.size();entries.erase(i);break; }
        }
    }
    // Revalidation is mandatory even for a warm entry; metrics/target packages are never cached.
    auto rebuild=[&](const Json &doc) {
        Pool pool(p,options.max_candidates);
        for(auto &c:doc.at("candidates"))pool.add(Graph::read(c.at("ir")),c.at("method"),c.at("inverted_first"),c.at("evidence"));
        if(pool.candidates.size()!=doc.at("candidates").size())throw std::runtime_error("cache candidate count mismatch");
        return target_pool(pool, profile);
    };
    Json targets;
    if(hit) {
        try { targets=rebuild(logical); }
        catch(const Cancelled &) { throw; }
        catch(const std::exception &) { rejected=true;hit=false; }
    }
    if(!hit) {
        logical=search(p,7,options);targets=rebuild(logical);
        if(enabled) {
            Entry entry;entry.key=key;entry.payload=logical.dump();entry.digest=sha256(entry.payload);
            if(entry.payload.size()<=byteLimit) {
                std::lock_guard<std::mutex> lock(gate);
                for(auto i=entries.begin();i!=entries.end();++i)if(i->key==key) { storedBytes-=i->payload.size();entries.erase(i);break; }
                while(!entries.empty() && (entries.size()>=entryLimit || storedBytes+entry.payload.size()>byteLimit)) {
                    storedBytes-=entries.front().payload.size();entries.pop_front();
                }
                storedBytes+=entry.payload.size();entries.push_back(std::move(entry));
            }
        }
    }
    logical["stage"]=8;logical["targets"]=targets;
    diagnostic=cache_status();diagnostic["enabled"]=enabled;diagnostic["hit"]=hit;diagnostic["rejected"]=rejected;
    diagnostic["semantic_key"]=key;diagnostic["target_package_cached"]=false;
    checkpoint();return logical;
}
} }
