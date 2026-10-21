// Simple tests for the array logic. Run with run-tests.bat (or see README).
#include "../backend/classroom_store.h"
#include <cstdio>
#include <iostream>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; failures++; } } while (0)

static Classroom make(const std::string& name, const std::string& building, int cap, bool avail) {
    Classroom r; r.name = name; r.building = building; r.floor = 1; r.capacity = cap;
    r.available = avail; r.facilities = "Whiteboard"; r.description = ""; return r;
}

int main() {
    static RoomStore s;
    s.loadSamples();
    CHECK(s.size() == 12);

    // linear search
    CHECK(s.findIndexById("ROOM-001") == 0);
    CHECK(s.findIndexById("ROOM-012") == 11);
    CHECK(s.findIndexById("NOPE") == -1);

    // stats
    Stats st = s.getStats();
    CHECK(st.total == 12 && st.available == 8 && st.unavailable == 4 && st.seats == 417);

    // filter: group of 25 needs an AVAILABLE room with capacity >= 25
    int idx[MAX_ROOMS];
    Filter f; f.groupSize = 25;
    int n = s.filterRooms(f, idx);
    CHECK(n == 3);
    for (int i = 0; i < n; i++) CHECK(s.at(idx[i]).available && s.at(idx[i]).capacity >= 25);

    // filter: search is case-insensitive and checks name, id and building
    Filter g; g.query = "LIBRARY";
    CHECK(s.filterRooms(g, idx) == 2);
    Filter h; h.query = "room-003";
    CHECK(s.filterRooms(h, idx) == 1);
    Filter u; u.availability = "unavailable";
    CHECK(s.filterRooms(u, idx) == 4);
    Filter none; none.query = "zzz";
    CHECK(s.filterRooms(none, idx) == 0);

    // sort
    Filter all;
    n = s.filterRooms(all, idx);
    s.sortIndexes(idx, n, "capacity-asc");
    for (int i = 1; i < n; i++) CHECK(s.at(idx[i - 1]).capacity <= s.at(idx[i]).capacity);
    s.sortIndexes(idx, n, "capacity-desc");
    CHECK(s.at(idx[0]).capacity == 120);
    s.sortIndexes(idx, n, "name");
    CHECK(s.at(idx[0]).name == "Computer Lab 2");

    // add: appended at the end with a fresh unique id
    std::string err;
    CHECK(s.addRoom(make("New Room", "Arts Block", 30, true), err));
    CHECK(s.size() == 13 && s.at(12).id == "ROOM-013");

    // add: validation
    CHECK(!s.addRoom(make("", "Arts Block", 30, true), err));
    CHECK(!s.addRoom(make("X", "", 30, true), err));
    CHECK(!s.addRoom(make("X", "Arts Block", 0, true), err));
    CHECK(!s.addRoom(make("X", "Arts Block", 10001, true), err));
    CHECK(s.size() == 13);

    // update keeps the id
    CHECK(s.updateRoom("ROOM-013", make("Renamed", "Arts Block", 31, false), err));
    CHECK(s.at(12).id == "ROOM-013" && s.at(12).name == "Renamed" && !s.at(12).available);
    CHECK(!s.updateRoom("NOPE", make("X", "Y", 5, true), err));

    // delete: later rooms shift left, ids stay unique and the freed id is reused
    CHECK(s.removeRoom("ROOM-002"));
    CHECK(s.size() == 12);
    CHECK(s.findIndexById("ROOM-002") == -1);
    CHECK(s.at(1).id == "ROOM-003");
    CHECK(!s.removeRoom("ROOM-002"));
    CHECK(s.addRoom(make("Again", "Arts Block", 10, true), err));
    CHECK(s.at(s.size() - 1).id == "ROOM-002");

    // buildings are unique and sorted
    std::string b[MAX_BUILDINGS];
    CHECK(s.getBuildings(b, MAX_BUILDINGS) == 6);
    CHECK(b[0] == "Administration Block" && b[5] == "Technology Centre");

    // facilities cleanup
    CHECK(RoomStore::normalizeFacilities(" Projector, projector ,,Whiteboard") == "Projector,Whiteboard");

    // file round trip
    CHECK(s.saveToFile("test_rooms.tmp"));
    static RoomStore t;
    CHECK(t.loadFromFile("test_rooms.tmp"));
    CHECK(t.size() == s.size());
    CHECK(t.at(5).name == s.at(5).name && t.at(5).capacity == s.at(5).capacity);
    std::remove("test_rooms.tmp");

    // the array has a hard limit
    static RoomStore full;
    full.loadSamples();
    while (full.size() < MAX_ROOMS) CHECK(full.addRoom(make("R", "B", 5, true), err));
    CHECK(!full.addRoom(make("One too many", "B", 5, true), err));

    std::cout << (failures == 0 ? "All tests passed.\n" : "Some tests failed.\n");
    return failures == 0 ? 0 : 1;
}
