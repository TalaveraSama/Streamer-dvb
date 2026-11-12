#pragma once

#include <json/json.h>

namespace tvs {
// Merge only top-level members supplied by the caller; arrays and objects sent
// explicitly retain their normal replacement semantics. The caller validates
// both values as objects and handles any domain-specific empty-value rules.
inline Json::Value mergeConfigPatch(const Json::Value& current, const Json::Value& patch) {
    Json::Value merged = current;
    for (const auto& name : patch.getMemberNames()) merged[name] = patch[name];
    return merged;
}
}
