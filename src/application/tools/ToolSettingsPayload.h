#pragma once

#include <string>

// One tool instance's settings, as stored in a workspace.
//
// The shell never interprets `data` -- the tool that produced it is the only
// thing that understands it. `version` is checked against the tool's declared
// settingsVersion in its ToolDefinition, and a mismatch discards the payload
// rather than migrating it, so a tool can change its format freely at the cost
// of one restore falling back to defaults.
//
// Keyed by instance id, not tool id: two instances of the same tool each get
// their own payload, which is what would let them be configured differently.
struct ToolSettingsPayload
{
    int version = 1;
    std::string data;

    bool operator==(const ToolSettingsPayload&) const = default;
};
