#include "ConfigPatch.h"
#include <iostream>

int main() {
    Json::Value current;
    current["bind_address"] = "127.0.0.1";
    current["require_stream_auth"] = true;
    current["max_request_body_mb"] = 12;
    current["telegram_token"] = "kept-private";
    current["language"] = "en";
    current["streams"] = Json::Value(Json::arrayValue);
    current["streams"].append("channel");
    Json::Value patch;
    patch["language"] = "es";
    auto merged = tvs::mergeConfigPatch(current, patch);
    if (merged["bind_address"].asString() != "127.0.0.1" ||
        !merged["require_stream_auth"].asBool() ||
        merged["max_request_body_mb"].asInt() != 12 ||
        merged["telegram_token"].asString() != "kept-private" ||
        merged["language"].asString() != "es" || merged["streams"].size() != 1) {
        std::cerr << "Partial config save lost existing settings\n";
        return 1;
    }
    patch["require_stream_auth"] = false;
    patch["streams"] = Json::Value(Json::arrayValue);
    merged = tvs::mergeConfigPatch(current, patch);
    if (merged["require_stream_auth"].asBool() || !merged["streams"].empty() || current["streams"].size() != 1) {
        std::cerr << "Explicit settings/array replacement failed\n";
        return 1;
    }
    return 0;
}
