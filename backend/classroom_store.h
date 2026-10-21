// classroom_store.h
// All classroom data lives in ONE fixed-size array: Classroom rooms[MAX_ROOMS].
// Every operation (search, filter, sort, add, update, delete) is written by hand
// with loops so the array logic is easy to explain and defend.
#pragma once
#include <string>

const int MAX_ROOMS = 300;       // size of the array
const int MAX_BUILDINGS = 100;   // size of the "unique buildings" array

struct Classroom {
    std::string id;
    std::string name;
    std::string building;
    int  floor;                  // -1 means "not set"
    int  capacity;
    bool available;
    std::string facilities;      // comma-separated, e.g. "Projector,Whiteboard"
    std::string description;
};

struct Filter {
    std::string query;           // matches name, id or building (case-insensitive)
    std::string availability;    // "all", "available" or "unavailable"
    std::string building;        // "all" or an exact building name
    int groupSize;               // 0 = not used
    Filter() : availability("all"), building("all"), groupSize(0) {}
};

struct Stats {
    int total, available, unavailable, seats;
};

class RoomStore {
public:
    RoomStore();

    int  size() const { return count; }
    const Classroom& at(int index) const { return rooms[index]; }

    // --- core array operations ---
    int  findIndexById(const std::string& id) const;                // linear search (loop)
    bool addRoom(Classroom room, std::string& error);               // append at rooms[count]
    bool updateRoom(const std::string& id, Classroom room, std::string& error);
    bool removeRoom(const std::string& id);                         // shift elements left
    int  filterRooms(const Filter& f, int outIndexes[]) const;      // returns how many matched
    void sortIndexes(int indexes[], int n, const std::string& sortBy) const; // insertion sort
    Stats getStats() const;                                         // loop totals
    int  getBuildings(std::string out[], int maxOut) const;         // unique + sorted

    // --- data management ---
    void loadSamples();
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    // --- helpers (public so tests can use them) ---
    static bool validate(const Classroom& room, std::string& error);
    static std::string normalizeFacilities(const std::string& raw);
    static std::string lower(const std::string& s);

private:
    Classroom rooms[MAX_ROOMS];
    int count;
    std::string generateId() const;
};
