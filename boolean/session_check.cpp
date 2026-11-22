#include <vcl.h>
#pragma hdrstop
#include "session_check.hpp"
#include "engine/cache.hpp"
#include "engine/control.hpp"
#include "engine/export.hpp"
namespace b2f {
J session_check(const J &request) {
    require(request.is_object() && request.size()==1 && request.contains("jobs") && request.at("jobs").is_array(),"session request requires jobs");
    engine::clear_cache();J results=J::array();
    for(auto &job:request.at("jobs")) {
        for(auto i=job.begin();i!=job.end();++i)require(i.key()=="state" || i.key()=="output" || i.key()=="name" || i.key()=="fbd" || i.key()=="st" || i.key()=="policy" || i.key()=="cache" || i.key()=="cancel_phase" || i.key()=="damage" || i.key()=="clear","unknown session job field");
        if(job.value("clear",false))engine::clear_cache();
        if(job.contains("damage"))engine::damage_cache_for_check(job.at("damage"));
        engine::Control control;control.use_cache=job.value("cache",true);
        int cancel=job.value("cancel_phase",0);require(cancel>=0 && cancel<=7,"cancel_phase must be 0..7");
        bool accepted=false;control.observer=[&](int phase) { if(phase==cancel)accepted=control.cancel(); };
        engine::ControlScope scope(&control);J result;
        U target = wide(job.at("output"));
        try { result=generate_strategy(job.at("state"),target,job.value("name",S("Result")),job.value("fbd",true),job.value("st",true),job.value("policy",engine::default_policy()),ChangeFileExt(target,".report.json"));result["cancelled"]=false; }
        catch(const engine::Cancelled &) { result={{"cancelled",true}}; }
        result["cancel_accepted"]=accepted;result["cache_state"]=engine::cache_status();results.push_back(result);
    }
    return {{"jobs",results}};
}
}
