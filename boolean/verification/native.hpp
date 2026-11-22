#pragma once
#include "../logic.hpp"
#include "../engine/package.hpp"
namespace b2f {
S sha256(const S &);
// Reconstruct semantics from native storage/source, never from the synthesis package.
J read_st_artifact(const J &documents,const J &metadata,const S &source);
J read_fbd_artifact(const J &documents,const J &metadata);
J verify_export(const Spec &,const S &language,const U &archive,const S &sourceBytes);
J verify_export_request(const J &);
}
