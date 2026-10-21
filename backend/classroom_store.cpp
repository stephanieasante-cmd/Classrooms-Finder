#include "classroom_store.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <cstdlib>

RoomStore::RoomStore() : count(0) {}

std::string RoomStore::lower(const std::string& s) {
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++) r[i] = (char)std::tolower((unsigned char)r[i]);
    return r;
}

static bool containsText(const std::string& text, const std::string& term) {
    return RoomStore::lower(text).find(term) != std::string::npos;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// ---------- LINEAR SEARCH ----------
// Walk the array from index 0 and stop at the first room whose id matches.
int RoomStore::findIndexById(const std::string& id) const {
    for (int i = 0; i < count; i++) {
        if (rooms[i].id == id) return i;
    }
    return -1;   // not found
}

std::string RoomStore::generateId() const {
    for (int n = 1; n <= MAX_ROOMS + 1; n++) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "ROOM-%03d", n);
        if (findIndexById(buf) == -1) return buf;
    }
    return "ROOM-999";
}

// Clean the comma-separated facilities text: trim, drop empty items and duplicates.
std::string RoomStore::normalizeFacilities(const std::string& raw) {
    std::string items[50];
    int n = 0;
    std::stringstream ss(raw);
    std::string piece;
    while (n < 50 && std::getline(ss, piece, ',')) {
        piece = trim(piece);
        if (piece.empty()) continue;
        bool duplicate = false;
        for (int i = 0; i < n; i++) {
            if (lower(items[i]) == lower(piece)) { duplicate = true; break; }
        }
        if (!duplicate) items[n++] = piece;
    }
    std::string result;
    for (int i = 0; i < n; i++) {
        if (i > 0) result += ",";
        result += items[i];
    }
    return result;
}

bool RoomStore::validate(const Classroom& r, std::string& error) {
    if (trim(r.name).empty())       { error = "Enter a room name."; return false; }
    if (r.name.size() > 80)         { error = "Room name is too long (max 80 characters)."; return false; }
    if (trim(r.building).empty())   { error = "Enter a building name."; return false; }
    if (r.building.size() > 80)     { error = "Building name is too long (max 80 characters)."; return false; }
    if (r.floor < -1 || r.floor > 100) { error = "Floor must be a whole number from 0 to 100."; return false; }
    if (r.capacity < 1 || r.capacity > 10000) { error = "Capacity must be a whole number from 1 to 10,000."; return false; }
    if (r.facilities.size() > 250)  { error = "Facilities text is too long (max 250 characters)."; return false; }
    if (r.description.size() > 300) { error = "Description is too long (max 300 characters)."; return false; }
    return true;
}

// ---------- ADD (append to the end of the array) ----------
bool RoomStore::addRoom(Classroom room, std::string& error) {
    if (!validate(room, error)) return false;
    if (count >= MAX_ROOMS) { error = "The room array is full."; return false; }
    room.facilities = normalizeFacilities(room.facilities);
    room.id = generateId();
    rooms[count] = room;     // put the new room in the first free slot
    count++;
    return true;
}

bool RoomStore::updateRoom(const std::string& id, Classroom room, std::string& error) {
    int index = findIndexById(id);
    if (index == -1) { error = "Classroom not found."; return false; }
    if (!validate(room, error)) return false;
    room.facilities = normalizeFacilities(room.facilities);
    room.id = id;            // the id never changes
    rooms[index] = room;
    return true;
}

// ---------- DELETE (shift everything after it one step left) ----------
bool RoomStore::removeRoom(const std::string& id) {
    int index = findIndexById(id);
    if (index == -1) return false;
    for (int i = index; i < count - 1; i++) {
        rooms[i] = rooms[i + 1];
    }
    count--;                 // the last slot is now unused
    return true;
}

// ---------- FILTER ----------
// Fills outIndexes[] with the positions of matching rooms and returns how many matched.
int RoomStore::filterRooms(const Filter& f, int outIndexes[]) const {
    std::string term = lower(trim(f.query));
    int found = 0;
    for (int i = 0; i < count; i++) {
        const Classroom& r = rooms[i];
        bool matchesSearch = term.empty() || containsText(r.name, term) ||
                             containsText(r.id, term) || containsText(r.building, term);
        bool matchesAvailability = f.availability == "all" ||
                                   (f.availability == "available" && r.available) ||
                                   (f.availability == "unavailable" && !r.available);
        bool matchesBuilding = f.building == "all" || r.building == f.building;
        bool matchesGroup = f.groupSize <= 0 || (r.available && r.capacity >= f.groupSize);
        if (matchesSearch && matchesAvailability && matchesBuilding && matchesGroup) {
            outIndexes[found++] = i;
        }
    }
    return found;
}

// ---------- SORT (insertion sort on the list of indexes) ----------
// Returns true if room a should come BEFORE room b.
static bool comesBefore(const Classroom& a, const Classroom& b, const std::string& sortBy) {
    if (sortBy == "capacity-asc"  && a.capacity != b.capacity) return a.capacity < b.capacity;
    if (sortBy == "capacity-desc" && a.capacity != b.capacity) return a.capacity > b.capacity;
    return RoomStore::lower(a.name) < RoomStore::lower(b.name);   // default and tie-breaker
}

void RoomStore::sortIndexes(int indexes[], int n, const std::string& sortBy) const {
    for (int i = 1; i < n; i++) {
        int key = indexes[i];
        int j = i - 1;
        while (j >= 0 && comesBefore(rooms[key], rooms[indexes[j]], sortBy)) {
            indexes[j + 1] = indexes[j];   // shift bigger items to the right
            j--;
        }
        indexes[j + 1] = key;
    }
}

// ---------- STATS (one pass through the array) ----------
Stats RoomStore::getStats() const {
    Stats s = {count, 0, 0, 0};
    for (int i = 0; i < count; i++) {
        if (rooms[i].available) s.available++; else s.unavailable++;
        s.seats += rooms[i].capacity;
    }
    return s;
}

// ---------- UNIQUE BUILDINGS ----------
int RoomStore::getBuildings(std::string out[], int maxOut) const {
    int n = 0;
    for (int i = 0; i < count; i++) {
        bool seen = false;
        for (int j = 0; j < n; j++) {
            if (out[j] == rooms[i].building) { seen = true; break; }
        }
        if (!seen && n < maxOut) out[n++] = rooms[i].building;
    }
    for (int i = 1; i < n; i++) {              // insertion sort A-Z
        std::string key = out[i];
        int j = i - 1;
        while (j >= 0 && lower(out[j]) > lower(key)) { out[j + 1] = out[j]; j--; }
        out[j + 1] = key;
    }
    return n;
}

// ---------- SAMPLE DATA ----------
// Fictional records for demonstration. This is not live university data.
void RoomStore::loadSamples() {
    struct Row { const char *name, *building; int floor, capacity; bool available; const char *facilities, *description; };
    static const Row samples[] = {
        {"Lecture Room A", "Science Block", 1, 40, true, "Projector,Whiteboard,Power outlets", "A medium-sized room suited to tutorials and focused group study."},
        {"Lecture Room B", "Main Block", 2, 60, false, "Whiteboard,Air conditioning", "A large teaching room currently marked unavailable in this sample dataset."},
        {"Seminar Room 104", "Library Annex", 1, 18, true, "Whiteboard,Power outlets", "A compact space for collaborative work and seminar discussions."},
        {"Computer Lab 2", "Technology Centre", 2, 32, true, "Projector,Power outlets,Air conditioning", "A computer lab-style room for technology-related group activities."},
        {"Tutorial Room 3", "Science Block", 0, 12, false, "Whiteboard", "A small room suitable for tutorials and short meetings."},
        {"Conference Room East", "Administration Block", 1, 24, true, "Projector,Whiteboard,Air conditioning", "A meeting space for student project discussions and presentations."},
        {"Lecture Theatre 1", "Main Block", 0, 120, true, "Projector,Whiteboard,Air conditioning", "A high-capacity lecture theatre for larger study groups."},
        {"Study Room 12", "Library Annex", 2, 8, true, "Whiteboard,Power outlets", "A small quiet room for a few students working together."},
        {"Workshop Room C", "Technology Centre", 1, 28, false, "Projector,Power outlets", "A flexible room intended for practical workshops and team activities."},
        {"Tutorial Room 205", "Arts Block", 2, 20, true, "Whiteboard,Air conditioning", "A general-purpose room for tutorials, reading groups, and meetings."},
        {"Discussion Room North", "Administration Block", 0, 10, true, "Whiteboard", "A simple discussion space for small student teams."},
        {"Lecture Room C", "Arts Block", 1, 45, false, "Projector,Whiteboard", "A classroom with presentation equipment; currently marked unavailable in the sample data."},
    };
    count = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        Classroom r;
        r.name = samples[i].name; r.building = samples[i].building;
        r.floor = samples[i].floor; r.capacity = samples[i].capacity;
        r.available = samples[i].available; r.facilities = samples[i].facilities;
        r.description = samples[i].description;
        r.id = generateId();
        rooms[count++] = r;
    }
}

// ---------- FILE STORAGE (tab-separated text, one room per line) ----------
bool RoomStore::saveToFile(const std::string& path) const {
    std::ofstream out(path.c_str(), std::ios::trunc);
    if (!out) return false;
    for (int i = 0; i < count; i++) {
        const Classroom& r = rooms[i];
        out << r.id << '\t' << r.name << '\t' << r.building << '\t' << r.floor << '\t'
            << r.capacity << '\t' << (r.available ? 1 : 0) << '\t' << r.facilities << '\t'
            << r.description << '\n';
    }
    return (bool)out;
}

bool RoomStore::loadFromFile(const std::string& path) {
    std::ifstream in(path.c_str());
    if (!in) return false;
    int n = 0;
    std::string line;
    while (n < MAX_ROOMS && std::getline(in, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        std::string f[8];
        std::stringstream ss(line);
        int k = 0;
        while (k < 8 && std::getline(ss, f[k], '\t')) k++;
        if (k < 6) continue;                         // skip damaged lines
        Classroom r;
        r.id = f[0]; r.name = f[1]; r.building = f[2];
        r.floor = std::atoi(f[3].c_str());
        r.capacity = std::atoi(f[4].c_str());
        r.available = (f[5] == "1");
        r.facilities = f[6]; r.description = f[7];
        std::string err;
        if (r.id.empty() || !validate(r, err)) continue;
        bool duplicate = false;
        for (int i = 0; i < n; i++) if (rooms[i].id == r.id) { duplicate = true; break; }
        if (!duplicate) rooms[n++] = r;
    }
    count = n;
    return true;
}
