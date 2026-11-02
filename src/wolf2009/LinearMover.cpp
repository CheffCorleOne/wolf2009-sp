// SPDX-License-Identifier: GPL-3.0-or-later
#include "LinearMover.h"
#include <algorithm>
#include <cmath>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
namespace wolf2009 {
namespace {
void Need(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
bool Finite(LinearMoverVec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 10000000 &&
           std::abs(p.y) <= 10000000 && std::abs(p.z) <= 10000000;
}
LinearMoverVec3 Sub(LinearMoverVec3 a, LinearMoverVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double Number(const std::string &text) {
    Need(!text.empty(), "Empty BRAI numeric field");
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double value;
    input >> std::noskipws >> value;
    Need(!input.fail() && input.eof() && std::isfinite(value), "Invalid/nonfinite BRAI number");
    return value;
}
void Integer(const std::string &text) {
    std::size_t i = (!text.empty() && text[0] == '-') ? 1 : 0;
    Need(i < text.size(), "Invalid BRAI integer");
    for (; i < text.size(); i++)
        Need(text[i] >= '0' && text[i] <= '9', "Invalid BRAI integer");
    Need(std::abs(Number(text)) <= 2147483647.0, "BRAI integer outside observed 32-bit range");
}
std::string Fold(std::string value) {
    for (char &c : value)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return value;
}
const EntityProperty *Unique(const EntityRecord &entity, const std::string &key) {
    const EntityProperty *result = NULL;
    for (const auto &property : entity.properties)
        if (Fold(property.key) == key) {
            Need(!result, "Duplicate ENTS placement property");
            result = &property;
        }
    return result;
}
} // namespace
SelfLinearMove DecodeSelfLinearMoveH1023(const std::vector<unsigned char> &data,
                                         const LinearMoverLimits &limits) {
    Need(!data.empty() && data.size() <= limits.sourceBytes, "BRAI source size limit");
    std::size_t cursor = 0, record = 0, totalTokens = 0, candidates = 0;
    SelfLinearMove result;
    while (cursor < data.size()) {
        Need(record < limits.records, "BRAI record limit");
        std::size_t depth = 0;
        while (cursor < data.size() && data[cursor] == '\t') {
            ++depth;
            ++cursor;
        }
        Need(depth <= limits.nesting, "BRAI nesting limit");
        std::vector<std::string> tokens;
        while (cursor < data.size() && data[cursor] != '\n') {
            Need(++totalTokens <= limits.tokens, "BRAI token count limit");
            std::string token;
            while (cursor < data.size() && data[cursor] != 0) {
                const unsigned char c = data[cursor++];
                Need(c >= 32 && c <= 126, "Unsupported BRAI token byte");
                Need(token.size() < limits.tokenBytes, "BRAI token byte limit");
                token += char(c);
            }
            Need(cursor < data.size(), "Unterminated BRAI token");
            ++cursor;
            Need(!token.empty(), "Empty BRAI token");
            tokens.push_back(token);
        }
        if (cursor < data.size())
            ++cursor;
        Need(!tokens.empty(), "Empty BRAI record");
        if (record == 0) {
            Need(depth == 0 && tokens == std::vector<std::string>({"h1023", "32", "32", "32", "0"}),
                 "Unsupported BRAI h1023 header profile");
        } else if (tokens[0] == "132") {
            Need(depth == 0, "Nested linear mover is unsupported");
            Need(++candidates == 1, "Ambiguous BRAI: multiple linear movers");
            Need(tokens.size() >= 9 && tokens[5] == "^132" && tokens[6] == "16" && tokens[7] == "^131" &&
                     tokens[8] == "1024",
                 "Unsupported BRAI linear-node field profile");
            for (unsigned i = 1; i <= 4; i++)
                Integer(tokens[i]);
            Need((tokens.size() - 9) % 2 == 0, "BRAI linear properties are not key/value pairs");
            std::map<std::string, std::string> properties;
            for (std::size_t i = 9; i < tokens.size(); i += 2)
                Need(properties.insert(std::make_pair(tokens[i], tokens[i + 1])).second,
                     "Duplicate BRAI linear property");
            const char *names[] = {"mMover",     "mDestination", "mSpeed",   "mTime",           "mAccelTime",
                                   "mDecelTime", "mMaxSpeed",    "mMaxTime", "mUseCustomSounds"};
            Need(properties.size() == 9, "Unsupported/missing BRAI linear property");
            for (const char *name : names)
                Need(properties.count(name) == 1, "Unsupported/missing BRAI linear property");
            Need(properties["mMover"] == "Me", "Only BRAI self-movement is supported");
            Need(properties["mUseCustomSounds"] == "false", "Custom mover sounds unsupported");
            result.destinationName = properties["mDestination"];
            Need(!result.destinationName.empty() && result.destinationName.size() <= 256,
                 "Unsupported BRAI destination name");
            result.speed = Number(properties["mSpeed"]);
            Need(result.speed > 0 && result.speed <= 100000, "Unsupported BRAI mover speed");
            for (unsigned i = 3; i <= 7; i++)
                Need(Number(properties[names[i]]) == 0, "Timed/accelerated BRAI mover unsupported");
            result.sourceRecord = record;
        }
        ++record;
    }
    Need(candidates == 1, "No supported top-level BRAI linear mover");
    return result;
}
LinearMoveDefinition ResolveSelfLinearMove(const SelfLinearMove &configuration,
                                           const EntityDocument &entities,
                                           LinearMoverVec3 currentWorldOrigin) {
    Need(Finite(currentWorldOrigin), "Invalid current mover world origin");
    Need(std::isfinite(configuration.speed) && configuration.speed > 0 && configuration.speed <= 100000,
         "Invalid linear speed");
    Need(!configuration.destinationName.empty() && configuration.destinationName.size() <= 256,
         "Invalid linear destination name");
    const EntityRecord *target = NULL;
    for (const auto &entity : entities.entities) {
        const auto *name = Unique(entity, "name");
        if (name && name->kind == EntityProperty::Text && name->text == configuration.destinationName) {
            Need(!target, "Ambiguous ENTS mover destination");
            target = &entity;
        }
    }
    Need(target != NULL, "ENTS mover destination not found");
    for (const char *key : {"bind", "bindtojoint", "bindtobody"}) {
        const auto *binding = Unique(*target, key);
        Need(!binding || (binding->kind == EntityProperty::Text && binding->text.empty()),
             "Bound ENTS destination has no established world origin");
    }
    const auto *origin = Unique(*target, "origin");
    Need(origin && origin->kind == EntityProperty::Floats && origin->numbers.size() == 3,
         "ENTS destination needs explicit origin[3]");
    LinearMoveDefinition definition;
    definition.start = currentWorldOrigin;
    definition.destination = {origin->numbers[0], origin->numbers[1], origin->numbers[2]};
    definition.speed = configuration.speed;
    definition.destinationName = configuration.destinationName;
    Need(Finite(definition.destination), "Invalid ENTS destination origin");
    return definition;
}
LinearMover::LinearMover(const LinearMoveDefinition &definition)
    : definition_(definition), position_(definition.start) {
    Need(Finite(definition.start) && Finite(definition.destination), "Invalid linear mover endpoints");
    Need(std::isfinite(definition.speed) && definition.speed > 0 && definition.speed <= 100000,
         "Invalid linear mover speed");
    const auto delta = Sub(definition.destination, definition.start);
    length_ = std::hypot(std::hypot(delta.x, delta.y), delta.z);
    arrived_ = length_ == 0;
}
void LinearMover::Activate() {
    if (!arrived_)
        active_ = true;
}
LinearMoverStep LinearMover::Step(double seconds) {
    Need(std::isfinite(seconds) && seconds >= 0 && seconds <= 1,
         "Linear mover step requires finite 0..1 seconds");
    const auto before = position_;
    if (active_ && seconds > 0) {
        travelled_ = std::min(length_, travelled_ + definition_.speed * seconds);
        if (travelled_ >= length_) {
            position_ = definition_.destination;
            arrived_ = true;
            active_ = false;
        } else {
            const double fraction = travelled_ / length_;
            const auto path = Sub(definition_.destination, definition_.start);
            position_ = {definition_.start.x + path.x * fraction, definition_.start.y + path.y * fraction,
                         definition_.start.z + path.z * fraction};
        }
    }
    LinearMoverStep result;
    result.position = position_;
    result.delta = Sub(position_, before);
    result.active = active_;
    result.arrived = arrived_;
    return result;
}
LinearMoverVec3 TranslateConfirmedRider(LinearMoverVec3 rider, const LinearMoverStep &step) {
    Need(Finite(rider) && Finite(step.delta), "Invalid confirmed-rider translation");
    LinearMoverVec3 result(rider.x + step.delta.x, rider.y + step.delta.y, rider.z + step.delta.z);
    Need(Finite(result), "Confirmed rider moved outside coordinate limit");
    return result;
}
} // namespace wolf2009
