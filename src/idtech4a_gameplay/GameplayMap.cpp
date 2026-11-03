// SPDX-License-Identifier: GPL-3.0-or-later
#include "GameplayMap.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace wolf2009 {
namespace {
void Need(bool b, const char *message) { if (!b) throw std::runtime_error(message); }
std::string Lower(std::string s) {
    for (char &c : s) if (c >= 'A' && c <= 'Z') c = char(c-'A'+'a');
    return s;
}
bool Name(const std::string &s) {
    if (s.empty() || s.size() > 255) return false;
    for (const unsigned char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
bool Resource(const std::string &s) {
    if (s.empty() || s.size() > 1023 || s[0] == '/' || s.back() == '/') return false;
    std::size_t p = 0;
    while (p < s.size()) {
        const auto end = s.find('/',p);
        const auto part = s.substr(p,end == std::string::npos ? end : end-p);
        if (part.empty() || part == "." || part == "..") return false;
        for (const unsigned char c : part)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        if (end == std::string::npos) break;
        p = end+1;
    }
    return true;
}
bool Finite(CollisionVec3 v, double bound) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::abs(v.x) <= bound && std::abs(v.y) <= bound && std::abs(v.z) <= bound;
}
void Axis(const double a[9]) {
    for (int row=0;row<3;++row) {
        for (int col=0;col<3;++col) {
            double d = 0;
            for (int k=0;k<3;++k) {
                Need(std::isfinite(a[row*3+k]),"Nonfinite entity rotation");
                d += a[row*3+k]*a[col*3+k];
            }
            Need(std::abs(d-(row == col ? 1 : 0)) < 1e-5,"Entity rotation is not rigid");
        }
    }
    const double det = a[0]*(a[4]*a[8]-a[5]*a[7])-
                       a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    Need(std::abs(det-1) < 1e-5,"Entity rotation reflects or scales collision");
}
const char *Material(std::uint32_t contents) {
    switch (contents) {
    case 1: return "wolf2009/collision/solid";
    case 8: return "wolf2009/collision/playerclip";
    case 16: return "wolf2009/collision/monsterclip";
    case 9: return "wolf2009/collision/solid_playerclip";
    case 17: return "wolf2009/collision/solid_monsterclip";
    case 24: return "wolf2009/collision/player_monsterclip";
    case 25: return "wolf2009/collision/solid_player_monsterclip";
    case 2: return "wolf2009/collision/opaque";
    case 3: return "wolf2009/collision/solid_opaque";
    case 10: return "wolf2009/collision/playerclip_opaque";
    case 18: return "wolf2009/collision/monsterclip_opaque";
    case 11: return "wolf2009/collision/solid_playerclip_opaque";
    case 19: return "wolf2009/collision/solid_monsterclip_opaque";
    case 26: return "wolf2009/collision/player_monsterclip_opaque";
    case 27: return "wolf2009/collision/solid_player_monsterclip_opaque";
    case 4096: return "wolf2009/collision/trigger";
    default: throw std::runtime_error("Unimplemented collision contents; no SOLID fallback");
    }
}
void Box(const StaticCollisionBox &box, const GameplayMapLimits &limits) {
    Need(Finite(box.minimum,limits.coordinate) && Finite(box.maximum,limits.coordinate),
         "Collision box exceeds coordinate limits");
    Need(box.maximum.x > box.minimum.x && box.maximum.y > box.minimum.y &&
         box.maximum.z > box.minimum.z,"Degenerate collision box");
    Need(box.contentsKnown,"Unknown collision contents");
    GameplayCollisionMaterial(box.contents);
}
void Pair(std::ostream &out, const char *key, const std::string &value) {
    out << '"' << key << "\" \"" << value << "\"\n";
}
std::string Number(double v) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << v;
    return out.str();
}
std::string Vector(CollisionVec3 v) { return Number(v.x)+" "+Number(v.y)+" "+Number(v.z); }
void Brush(std::ostream &out, const StaticCollisionBox &b) {
    out << "{\nbrushDef3\n{\n";
    const double n[6][4] = {{1,0,0,-b.maximum.x},{-1,0,0,b.minimum.x},
        {0,1,0,-b.maximum.y},{0,-1,0,b.minimum.y},
        {0,0,1,-b.maximum.z},{0,0,-1,b.minimum.z}};
    for (const auto &plane : n)
        out << "( " << plane[0] << ' ' << plane[1] << ' ' << plane[2] << ' ' << plane[3]
            << " ) ( ( 1 0 0 ) ( 0 1 0 ) ) \"" << GameplayCollisionMaterial(b.contents) << "\" 0 0 0\n";
    out << "}\n}\n";
}
const char *Class(GameplayEntityKind k) {
    switch (k) {
    case GameplayEntityKind::PlayerStart: return "info_player_start";
    case GameplayEntityKind::Static: return "wolf2009_static";
    case GameplayEntityKind::Mover: return "wolf2009_mover";
    case GameplayEntityKind::Trigger: return "wolf2009_trigger";
    case GameplayEntityKind::RemoveTargets: return "wolf2009_remove_targets";
    case GameplayEntityKind::Point: return "wolf2009_target";
    }
    throw std::runtime_error("Unknown gameplay entity role");
}
}
const char *GameplayCollisionMaterial(std::uint32_t contents) { return Material(contents); }
std::vector<StaticCollisionBox> ImportGameplayWorld(const StaticCollisionScene &s) {
    Need(s.coverageComplete && s.identityBodyPoseVerified && s.contentsComplete &&
         s.otherRigidBodies == 0 && s.unsupported.empty() && s.omittedTriangles == 0 &&
         s.omittedTransformedSubparts == 0,"World HKXS coverage is incomplete");
    Need(!s.boxes.empty(),"World has no decoded exact collision");
    for (const auto &box : s.boxes) {
        Box(box,GameplayMapLimits());
        Need(box.contents != 4096,"Trigger collision requires an explicitly bound entity");
    }
    return s.boxes;
}
std::vector<StaticCollisionBox> ImportGameplayModel(const ModelCollision &m) {
    Need(m.geometryComplete && m.contentsComplete && m.localPoseVerified &&
         m.unsupportedGeometry.empty(),"Model HKXS coverage is incomplete");
    Need(!m.localBoxes.empty(),"Model has no decoded exact collision");
    for (const auto &box : m.localBoxes) Box(box,GameplayMapLimits());
    return m.localBoxes;
}
GameplayEntity ImportGameplayPlayerStart(const PlayerStartCandidate &c) {
    const auto &p = c.placement;
    Need(p.hasOrigin,"Player start lacks an authored origin");
    GameplayEntity e;
    e.kind = GameplayEntityKind::PlayerStart;
    e.name = p.name;
    e.origin = {p.origin[0],p.origin[1],p.origin[2]};
    if (p.hasRotation) {
        std::copy(p.rotation,p.rotation+9,e.axis);
    } else if (p.hasAngle) {
        Need(std::isfinite(p.angle),"Nonfinite player yaw");
        const auto yaw = double(p.angle)*3.14159265358979323846/180;
        e.axis[0]=std::cos(yaw); e.axis[1]=std::sin(yaw);
        e.axis[3]=-std::sin(yaw); e.axis[4]=std::cos(yaw);
    }
    Axis(e.axis);
    return e;
}
GameplayMap BuildStaticGameplayMap(const StaticCollisionScene &s, const EntityDocument &entities,
                                  const std::string &requestedStart) {
    GameplayMap map;
    map.worldBoxes = ImportGameplayWorld(s);
    const auto candidates = CollectPlayerStarts(entities);
    const auto choice = SelectDiagnosticPlayerStart(candidates,requestedStart);
    Need(choice.selected,PlayerStartSelectionReasonText(choice.reason));
    map.entities.push_back(ImportGameplayPlayerStart(candidates.at(choice.candidateIndex)));
    map.sourceEntities = entities.entities.size();
    for (std::size_t i=0;i<entities.entities.size();++i) {
        if (i == candidates.at(choice.candidateIndex).entityIndex) continue;
        const auto *c = entities.entities[i].Find("classname");
        if (c && c->kind == EntityProperty::Text && Lower(c->text) == "worldspawn") continue;
        ++map.unimplementedEntities;
    }
    return map;
}
GameplayMapFiles EncodeGameplayMap(const GameplayMap &map, const GameplayMapLimits &limits) {
    Need(limits.coordinate > 0 && std::isfinite(limits.coordinate),"Invalid gameplay coordinate limit");
    Need(!map.worldBoxes.empty() && map.entities.size() <= limits.entities,"Invalid gameplay map size");
    Need(std::isfinite(map.gravity) && map.gravity > 0 && map.gravity <= 100000,"Invalid map gravity");
    GameplayMapFiles result;
    result.worldBoxes = map.worldBoxes.size();
    result.entities = map.entities.size()+1;
    std::map<std::string,std::size_t> names;
    names["world"] = static_cast<std::size_t>(-1);
    std::size_t starts = 0;
    for (std::size_t i=0;i<map.entities.size();++i) {
        const auto &e = map.entities[i];
        Need(Name(e.name) && Lower(e.name) != "player1" &&
             names.insert(std::make_pair(Lower(e.name),i)).second,"Invalid or duplicate entity name");
        Class(e.kind);
        Need(Finite(e.origin,limits.coordinate),"Invalid entity origin");
        Axis(e.axis);
        Need(e.visualModel.empty() || Resource(e.visualModel),"Invalid visual model resource");
        const bool physical = e.kind == GameplayEntityKind::Static || e.kind == GameplayEntityKind::Mover ||
                              e.kind == GameplayEntityKind::Trigger;
        Need(physical || e.localBoxes.empty(),"Point entity unexpectedly has collision");
        Need(!physical || !e.localBoxes.empty(),"Physical entity lacks exact collision");
        Need(e.kind != GameplayEntityKind::PlayerStart || (e.bind.empty() && !e.hidden),
             "Player start cannot be hidden or bound by this adapter");
        starts += e.kind == GameplayEntityKind::PlayerStart;
        Need(e.localBoxes.size() <= limits.boxes && result.entityBoxes <= limits.boxes-e.localBoxes.size(),
             "Entity collision budget exceeded");
        result.entityBoxes += e.localBoxes.size();
        for (const auto &box : e.localBoxes) {
            Box(box,limits);
            Need((box.contents == 4096) == (e.kind == GameplayEntityKind::Trigger),
                 "Trigger contents and runtime entity role disagree");
        }
        if (e.kind == GameplayEntityKind::Trigger)
            Need(std::isfinite(e.triggerWait) && e.triggerWait >= -1 && e.triggerWait <= 86400 &&
                 std::isfinite(e.triggerDelay) && e.triggerDelay >= 0 && e.triggerDelay <= 86400,
                 "Invalid trigger timing");
        if (e.moveOnSpawn)
            Need(e.kind == GameplayEntityKind::Mover && Finite(e.moveDestination,limits.coordinate) &&
                 std::isfinite(e.moveSpeed) && e.moveSpeed > 0 && e.moveSpeed <= 100000,
                 "Invalid native mover command");
    }
    Need(starts == 1,"Exactly one diagnostic player start is required");
    Need(result.worldBoxes <= limits.boxes && result.entityBoxes <= limits.boxes-result.worldBoxes,
         "Map collision budget exceeded");
    for (const auto &box : map.worldBoxes) {
        Box(box,limits);
        Need(box.contents != 4096,"Unbound world trigger is unsupported");
    }
    for (std::size_t i=0;i<map.entities.size();++i) {
        const auto &e = map.entities[i];
        std::set<std::string> seen;
        for (const auto &target : e.targets)
            Need(Name(target) && names.count(Lower(target)) && Lower(target) != Lower(e.name) &&
                 Lower(target) != "world" && seen.insert(Lower(target)).second,
                 "Invalid, missing or duplicate entity target");
        if (!e.bind.empty()) {
            auto j = i;
            std::set<std::size_t> chain;
            while (!map.entities[j].bind.empty()) {
                Need(chain.insert(j).second,"Entity bind cycle");
                const auto found = names.find(Lower(map.entities[j].bind));
                Need(found != names.end() && found->second < map.entities.size(),"Missing entity bind parent");
                j = found->second;
            }
        }
    }
    std::ostringstream out, script;
    out.imbue(std::locale::classic()); script.imbue(std::locale::classic());
    out << std::setprecision(17);
    out << "// Generated from explicit collision/entity data, not render bounds.\nVersion 2\n{\n";
    Pair(out,"classname","worldspawn"); Pair(out,"name","world");
    Pair(out,"gravity",Number(map.gravity));
    for (const auto &box : map.worldBoxes) Brush(out,box);
    out << "}\n";
    bool commands = false;
    for (const auto &e : map.entities) {
        out << "{\n";
        Pair(out,"classname",Class(e.kind)); Pair(out,"name",e.name); Pair(out,"origin",Vector(e.origin));
        std::string rotation;
        for (int i=0;i<9;++i) { if (i) rotation += ' '; rotation += Number(e.axis[i]); }
        Pair(out,"rotation",rotation);
        if (!e.localBoxes.empty()) {
            Pair(out,"clipmodel",e.name);
            // The opt-in engine patch gives inline collision its own key so
            // an independent visual model cannot rename/reuse this clipmodel.
            Pair(out,"wolf2009_collisionmodel",e.name);
        }
        else Pair(out,"noclipmodel","1");
        if (!e.visualModel.empty()) Pair(out,"model",e.visualModel);
        if (!e.bind.empty()) Pair(out,"bind",e.bind);
        if (e.hidden) Pair(out,"hide","1");
        for (std::size_t i=0;i<e.targets.size();++i)
            Pair(out,("target"+std::to_string(i+1)).c_str(),e.targets[i]);
        if (e.kind == GameplayEntityKind::Trigger) {
            Pair(out,"wait",Number(e.triggerWait)); Pair(out,"delay",Number(e.triggerDelay));
        }
        for (const auto &box : e.localBoxes) Brush(out,box);
        out << "}\n";
        if (e.moveOnSpawn) {
            if (!commands) { script << "void main() {\n    entity mover;\n"; commands = true; }
            script << "    mover = sys.getEntity(\"" << e.name << "\");\n"
                   << "    mover.speed(" << Number(e.moveSpeed) << ");\n"
                   << "    mover.moveToPos('" << Vector(e.moveDestination) << "');\n";
        }
        Need(static_cast<std::size_t>(out.tellp()) <= limits.textBytes,"Generated map text budget exceeded");
    }
    if (commands) script << "}\n";
    result.mapText = out.str(); result.scriptText = script.str();
    Need(result.mapText.size() <= limits.textBytes && result.scriptText.size() <= limits.textBytes,
         "Generated gameplay text budget exceeded");
    return result;
}
bool ResolveGameplayMapPath(const std::string &path, GameplayMapSourcePaths &out) {
    const auto p = Lower(path);
    if (!Resource(p) || p.compare(0,9,"wolf2009/") != 0) return false;
    const auto end = p.find('/',9);
    if (end == std::string::npos || !Name(p.substr(9,end-9)) ||
        p.compare(end,11,"/proc/maps/") != 0 || p.size() < end+16 ||
        p.compare(p.size()-4,4,".map") != 0) return false;
    const auto resource = p.substr(end+6,p.size()-4-(end+6));
    // Reject .map.map and file-extension ambiguity at the typed boundary.
    const auto leaf = resource.substr(resource.find_last_of('/')+1);
    if (leaf.empty() || leaf.find('.') != std::string::npos) return false;
    GameplayMapSourcePaths next;
    next.entities = p.substr(0,end)+"/ENTS/"+resource+".ents";
    next.collision = p.substr(0,end)+"/HKXS/"+resource+".hkxs";
    out = next;
    return true;
}
} // namespace wolf2009
