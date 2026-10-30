// The in-exe Archipelago bridge (the Crackdown recomp's design: crackdown/src/ap). client.cpp's network thread only
// queues packets; this file runs on the title's thread (hle_ap.c calls it once a frame Harry is updated) and owns
// all session state:
//   - catalog.json (port/ap, embedded at build time) maps location keys and item names to IDs; a slot whose
//     slot_data catalog differs is refused (disconnected, logged);
//   - the session sidecar <exe dir>\saves\archipelago\session_<seed>_<slot>.json keeps every item given (in the
//     server's order: its length is the received index) and every check made, so an item is given once and checks
//     are sent again on each connection (the server ignores repeats);
//   - checks made before a slot is connected this run go to pending_<slot>.json and join the session on connecting;
//   - received items are handed out one at a time (hpcos_ap_next_item / hpcos_ap_item_given), the session saved after
//     each, so a crash between giving and saving gives an item at most twice, never loses one.
#include "bridge.h"
#include "client.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace hpcos::ap {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;

const char kCatalogText[] =
#include "catalog_data.inc"
    ;

struct Catalog {
  std::string version, game;
  std::map<std::string, int64_t> location_by_key;
  std::map<int64_t, std::string> item_name;
  std::map<std::string, json> item;  // by name
};

const Catalog& Cat() {
  static Catalog c = [] {
    Catalog c;
    json j = json::parse(kCatalogText);
    c.version = j["version"];
    c.game = j["game"];
    for (auto& l : j["locations"]) c.location_by_key[l["key"].get<std::string>()] = l["id"].get<int64_t>();
    for (auto& [name, it] : j["items"].items()) {
      c.item_name[it["id"].get<int64_t>()] = name;
      c.item[name] = it;
    }
    return c;
  }();
  return c;
}

fs::path SavesDir() {
  wchar_t exe[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  return fs::path(std::wstring(exe, n)).parent_path() / L"saves" / L"archipelago";
}

std::string ReadPassword() {
  std::ifstream f(SavesDir() / L"connection.toml");
  std::string line;
  while (std::getline(f, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos || line.compare(0, 8, "password") != 0) continue;
    std::string v = line.substr(eq + 1);
    auto a = v.find_first_of("\"'"), b = v.find_last_of("\"'");
    if (a != std::string::npos && b > a) return v.substr(a + 1, b - a - 1);
  }
  return {};
}

struct Session {
  std::string seed, slot;
  std::vector<int64_t> given;  // every item given in the game, in the server's order
  std::set<int64_t> checked;
  fs::path path;
  void Load() {
    std::ifstream f(path);
    if (!f) return;
    try {
      json j = json::parse(f);
      for (auto& id : j.value("given", json::array())) given.push_back(id.get<int64_t>());
      for (auto& id : j.value("checked", json::array())) checked.insert(id.get<int64_t>());
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[AP] session %s unreadable (%s): starting it over\n", path.string().c_str(), e.what());
    }
  }
  bool Save() const {
    fs::create_directories(path.parent_path());
    fs::path tmp = path;
    tmp += L".tmp";
    {
      std::ofstream f(tmp, std::ios::trunc);
      json j{{"format", 1}, {"seed", seed}, {"slot", slot}, {"received_index", given.size()}, {"given", given},
             {"checked", std::vector<int64_t>(checked.begin(), checked.end())}};
      f << j.dump(1);
      if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
};

struct Bridge {
  bool configured = false, started = false, connected = false;
  std::string server, slot, password;
  Client client;
  uint64_t generation = 0;
  std::string seed;
  json slot_data;
  Session session;
  bool bound = false;                 // a session is loaded (a slot connected this run)
  std::vector<int64_t> received;      // the server's whole item list this connection
  std::deque<int64_t> queued_checks;  // made before a slot connected
  bool goal_done = false, goal_sent = false;

  void Init() {
    const char* s = std::getenv("RECOMP_AP_SERVER");
    const char* n = std::getenv("RECOMP_AP_SLOT");
    configured = s && *s && n && *n;
    if (!configured) return;
    server = s;
    slot = n;
    password = ReadPassword();
    LoadPending();
    std::fprintf(stderr, "[AP] %s, slot %s, catalog %s (%zu locations), %zu checks pending\n", server.c_str(),
                 slot.c_str(), Cat().version.c_str(), Cat().location_by_key.size(), queued_checks.size());
    client.Start(server);
    started = true;
  }

  fs::path PendingPath() const { return SavesDir() / fs::path("pending_" + slot + ".json"); }
  void LoadPending() {
    std::ifstream f(PendingPath());
    if (!f) return;
    try {
      for (auto& id : json::parse(f)) queued_checks.push_back(id.get<int64_t>());
    } catch (...) {
    }
  }
  void SavePending() {
    fs::create_directories(PendingPath().parent_path());
    std::ofstream f(PendingPath(), std::ios::trunc);
    f << json(std::vector<int64_t>(queued_checks.begin(), queued_checks.end())).dump();
  }

  void Send(json cmds) {
    if (connected) client.Send(generation, std::move(cmds));
  }

  void SendChecks() {
    if (!connected || !bound || session.checked.empty()) return;
    Send(json::array({{{"cmd", "LocationChecks"},
                       {"locations", std::vector<int64_t>(session.checked.begin(), session.checked.end())}}}));
  }

  void SendGoal() {
    Send(json::array({{{"cmd", "StatusUpdate"}, {"status", 30}}}));
    goal_sent = connected;
  }

  void OnPacket(const NetEvent& e) {
    const json& p = e.body;
    if (e.cmd == "RoomInfo") {
      seed = p.value("seed_name", "");
      json connect{{"cmd", "Connect"}, {"game", Cat().game}, {"name", slot}, {"password", password},
                   {"uuid", "hpcos-" + slot}, {"items_handling", 7}, {"tags", json::array()}, {"slot_data", true},
                   {"version", {{"major", 0}, {"minor", 6}, {"build", 8}, {"class", "Version"}}}};
      client.Send(generation, json::array({connect}));
    } else if (e.cmd == "Connected") {
      slot_data = p.value("slot_data", json::object());
      std::string cat = slot_data.value("catalog", "");
      if (cat != Cat().version) {
        std::fprintf(stderr, "[AP] slot built from catalog %s, this game has %s: not playing it\n", cat.c_str(),
                     Cat().version.c_str());
        client.RequestStop();
        return;
      }
      connected = true;
      if (!bound || session.seed != seed) {
        session = Session{};
        session.seed = seed;
        session.slot = slot;
        session.path = SavesDir() / fs::path("session_" + seed + "_" + slot + ".json");
        session.Load();
        bound = true;
      }
      for (int64_t id : queued_checks) session.checked.insert(id);
      queued_checks.clear();
      session.Save();
      SavePending();
      received.clear();
      std::fprintf(stderr, "[AP] connected: seed %s, %zu checks made, %zu items given\n", seed.c_str(),
                   session.checked.size(), session.given.size());
      SendChecks();
      if (goal_done && !goal_sent) SendGoal();
    } else if (e.cmd == "ConnectionRefused") {
      std::fprintf(stderr, "[AP] connection refused: %s\n", p.value("errors", json::array()).dump().c_str());
      client.RequestStop();
    } else if (e.cmd == "ReceivedItems") {
      size_t index = p.value("index", size_t(0));
      if (index == 0) received.clear();
      if (index != received.size()) {  // out of step: ask for the whole list again
        Send(json::array({{{"cmd", "Sync"}}}));
        return;
      }
      for (auto& it : p.value("items", json::array())) received.push_back(it.value("item", int64_t(0)));
    }
  }

  void EnsureInit() {
    static bool once = false;
    if (!once) {
      once = true;
      Init();
    }
  }

  void Frame() {
    EnsureInit();
    if (!started) return;
    for (NetEvent& e : client.Drain()) {
      if (e.kind == NetEvent::Kind::connected_socket) {
        generation = e.generation;
      } else if (e.kind == NetEvent::Kind::disconnected) {
        if (connected)
          std::fprintf(stderr, "[AP] disconnected: %s (checks are kept and sent later)\n", e.detail.c_str());
        connected = false;
      } else if (e.generation == generation) {
        OnPacket(e);
      }
    }
  }

  void Location(const std::string& key) {
    EnsureInit();
    if (!configured) return;
    auto it = Cat().location_by_key.find(key);
    if (it == Cat().location_by_key.end()) return;
    int64_t id = it->second;
    if (!bound) {
      for (int64_t q : queued_checks)
        if (q == id) return;
      queued_checks.push_back(id);
      SavePending();
      std::fprintf(stderr, "[AP] check %s (%lld), kept until a slot is connected\n", key.c_str(), (long long)id);
      return;
    }
    if (!session.checked.insert(id).second) return;
    session.Save();
    std::fprintf(stderr, "[AP] check %s (%lld)\n", key.c_str(), (long long)id);
    if (connected) Send(json::array({{{"cmd", "LocationChecks"}, {"locations", {id}}}}));
  }
};

Bridge& B() {
  static Bridge b;
  return b;
}
std::string g_kind;  // hpcos_ap_next_item's string, valid until the next call
}  // namespace
}  // namespace hpcos::ap

using namespace hpcos::ap;

extern "C" void hpcos_ap_frame(void) { B().Frame(); }
extern "C" int hpcos_ap_enabled(void) {
  static int on = -1;
  if (on < 0) {
    const char* s = std::getenv("RECOMP_AP_SERVER");
    const char* n = std::getenv("RECOMP_AP_SLOT");
    on = s && *s && n && *n;
  }
  return on;
}
extern "C" int hpcos_ap_known(const char* key) {
  return key && hpcos_ap_enabled() && Cat().location_by_key.count(key) != 0;
}
extern "C" void hpcos_ap_location(const char* key) {
  if (key && hpcos_ap_enabled()) B().Location(key);
}
extern "C" void hpcos_ap_goal(void) {
  Bridge& b = B();
  if (!hpcos_ap_enabled() || b.goal_done) return;
  b.goal_done = true;
  std::fprintf(stderr, "[AP] goal done\n");
  b.SendGoal();
}
extern "C" int hpcos_ap_next_item(const char** kind, int* value) {
  Bridge& b = B();
  while (b.bound && b.connected && b.session.given.size() < b.received.size()) {
    int64_t id = b.received[b.session.given.size()];
    auto name = Cat().item_name.find(id);
    if (name == Cat().item_name.end()) {  // not in this catalog: counted as given, nothing to do
      b.session.given.push_back(id);
      b.session.Save();
      continue;
    }
    const json& it = Cat().item.at(name->second);
    g_kind = it.value("kind", "");
    *kind = g_kind.c_str();
    *value = it.value("card", it.value("spell", it.value("amount", it.value("item", 0))));
    return 1;
  }
  return 0;
}
extern "C" void hpcos_ap_item_given(void) {
  Bridge& b = B();
  if (b.session.given.size() < b.received.size()) {
    int64_t id = b.received[b.session.given.size()];
    b.session.given.push_back(id);
    b.session.Save();
    std::fprintf(stderr, "[AP] item %s given\n", Cat().item_name.at(id).c_str());
  }
}
extern "C" int hpcos_ap_active(void) { return B().bound; }
// a whole-number option of the slot (slot_data), or def without a bound slot or the key
extern "C" int hpcos_ap_option(const char* key, int def) {
  auto& b = B();
  if (!b.bound || !key || !b.slot_data.contains(key)) return def;
  const json& v = b.slot_data[key];
  return v.is_boolean() ? int(v.get<bool>()) : v.is_number_integer() ? v.get<int>() : def;
}
extern "C" int hpcos_ap_cards_goal(void) {
  Bridge& b = B();
  return b.bound && b.slot_data.value("goal", "") == "wizard_cards" ? b.slot_data.value("cards_goal", 0) : 0;
}
extern "C" int hpcos_ap_has(const char* item_name) {
  Bridge& b = B();
  auto it = Cat().item.find(item_name ? item_name : "");
  if (!b.bound || it == Cat().item.end()) return 0;
  int64_t id = it->second.value("id", int64_t(0));
  for (int64_t g : b.session.given)
    if (g == id) return 1;
  return 0;
}

namespace {
struct TravelEntry {
  std::string name, needs;
  int level = 0, start = -1;
};
// CoS has no days: every pass item names one place (a level of the title's table and the start point to use there)
const std::vector<TravelEntry>& Travel() {
  static std::vector<TravelEntry> list = [] {
    std::vector<TravelEntry> out;
    for (auto& [name, it] : Cat().item) {
      if (it.value("kind", "") != "pass" || !it.contains("level")) continue;
      TravelEntry e;
      e.level = it["level"].get<int>();
      e.start = it.value("start", -1);
      e.name = name.size() > 5 && name.compare(name.size() - 5, 5, " Pass") == 0 ? name.substr(0, name.size() - 5) : name;
      e.needs = name;
      out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const TravelEntry& a, const TravelEntry& b) { return a.level < b.level; });
    return out;
  }();
  return list;
}
}  // namespace

extern "C" int hpcos_ap_travel_entry(int i, const char** name, int* level, int* start, int* unlocked) {
  const auto& list = Travel();
  if (i < 0 || size_t(i) >= list.size()) return 0;
  const TravelEntry& e = list[size_t(i)];
  *name = e.name.c_str();
  *level = e.level;
  *start = e.start;
  // The Burrow is where a game starts: open without its pass
  *unlocked = e.level == 0 || hpcos_ap_has(e.needs.c_str());
  return 1;
}
