// main.cpp - a tiny HTTP server that sits in front of the RoomStore array.
// The browser (HTML/CSS/JS) asks for data at /api/... and this program answers with JSON.
// It also serves the files in the "frontend" folder, so everything runs on one address.
#include "classroom_store.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <filesystem>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSE_SOCKET closesocket
  #define SEND_FLAGS 0
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #define CLOSE_SOCKET close
  #define SEND_FLAGS MSG_NOSIGNAL
#endif

static const std::string FRONTEND_DIR = "frontend";
static const std::string DATA_FILE = "data/classrooms.txt";
static RoomStore store;               // the one and only room array

// FIFO queue of classroom-search requests. Each request contains search criteria.
struct SearchRequest {
    std::string query;
    std::string availability;
    std::string building;
    int groupSize;                 // 0 means no minimum group-size filter
    std::string sortBy;
};
static std::queue<SearchRequest> searchQueue;

struct Response { int status; std::string type; std::string body; };

// ---------------- small helpers ----------------
static std::string jsonEscape(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c == '\t') r += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
        else r += (char)c;
    }
    return r;
}

static std::string urlDecode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') r += ' ';
        else if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            r += (char)std::strtol(s.substr(i + 1, 2).c_str(), NULL, 16);
            i += 2;
        } else r += s[i];
    }
    return r;
}

static std::map<std::string, std::string> parseForm(const std::string& text) {
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t amp = text.find('&', pos);
        if (amp == std::string::npos) amp = text.size();
        std::string pair = text.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        if (!pair.empty()) {
            if (eq == std::string::npos) m[urlDecode(pair)] = "";
            else m[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
        pos = amp + 1;
    }
    return m;
}

static std::string cleanText(std::string s) {      // tabs/newlines would break the data file
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r') s[i] = ' ';
    return s;
}

static bool parseWholeNumber(const std::string& raw, int& out) {
    if (raw.empty()) return false;
    char* end = NULL;
    long v = std::strtol(raw.c_str(), &end, 10);
    if (*end != '\0' || v < -1000000 || v > 1000000) return false;
    out = (int)v;
    return true;
}

static std::string roomToJson(const Classroom& r) {
    std::string j = "{";
    j += "\"id\":\"" + jsonEscape(r.id) + "\",";
    j += "\"name\":\"" + jsonEscape(r.name) + "\",";
    j += "\"building\":\"" + jsonEscape(r.building) + "\",";
    j += "\"floor\":" + (r.floor < 0 ? std::string("null") : std::to_string(r.floor)) + ",";
    j += "\"capacity\":" + std::to_string(r.capacity) + ",";
    j += std::string("\"available\":") + (r.available ? "true" : "false") + ",";
    j += "\"facilities\":[";
    std::stringstream ss(r.facilities);
    std::string item;
    bool first = true;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        if (!first) j += ",";
        j += "\"" + jsonEscape(item) + "\"";
        first = false;
    }
    j += "],";
    j += "\"description\":\"" + jsonEscape(r.description) + "\"}";
    return j;
}

static Response jsonResponse(int status, const std::string& body) {
    Response r = {status, "application/json; charset=utf-8", body};
    return r;
}
static Response errorResponse(int status, const std::string& message) {
    return jsonResponse(status, "{\"error\":\"" + jsonEscape(message) + "\"}");
}

static bool persist() { return store.saveToFile(DATA_FILE); }

// Build a Classroom from the form fields the browser sent.
static bool roomFromForm(const std::map<std::string, std::string>& f, Classroom& room, std::string& error) {
    std::map<std::string, std::string>::const_iterator it;
    room.id = "";
    it = f.find("name");        room.name = it == f.end() ? "" : cleanText(it->second);
    it = f.find("building");    room.building = it == f.end() ? "" : cleanText(it->second);
    it = f.find("facilities");  room.facilities = it == f.end() ? "" : cleanText(it->second);
    it = f.find("description"); room.description = it == f.end() ? "" : cleanText(it->second);
    it = f.find("available");   room.available = (it == f.end() || it->second != "false");
    it = f.find("floor");
    room.floor = -1;
    if (it != f.end() && !it->second.empty()) {
        int v;
        if (!parseWholeNumber(it->second, v) || v < 0 || v > 100) { error = "Floor must be a whole number from 0 to 100."; return false; }
        room.floor = v;
    }
    it = f.find("capacity");
    int cap;
    if (it == f.end() || !parseWholeNumber(it->second, cap)) { error = "Capacity must be a whole number from 1 to 10,000."; return false; }
    room.capacity = cap;
    return true;
}

// ---------------- API routes ----------------
static Response handleList(const std::map<std::string, std::string>& q) {
    Filter f;
    std::map<std::string, std::string>::const_iterator it;
    it = q.find("query");        if (it != q.end()) f.query = it->second;
    it = q.find("availability"); if (it != q.end() && !it->second.empty()) f.availability = it->second;
    it = q.find("building");     if (it != q.end() && !it->second.empty()) f.building = it->second;
    it = q.find("groupSize");
    if (it != q.end() && !it->second.empty()) {
        int g;
        if (!parseWholeNumber(it->second, g) || g < 1 || g > 10000) return errorResponse(400, "Group size must be a whole number from 1 to 10,000.");
        f.groupSize = g;
    }
    std::string sortBy = "name";
    it = q.find("sort"); if (it != q.end() && !it->second.empty()) sortBy = it->second;

    int matches[MAX_ROOMS];                       // positions of the rooms that matched
    int n = store.filterRooms(f, matches);
    store.sortIndexes(matches, n, sortBy);

    std::string body = "{\"total\":" + std::to_string(store.size()) + ",\"rooms\":[";
    for (int i = 0; i < n; i++) {
        if (i > 0) body += ",";
        body += roomToJson(store.at(matches[i]));
    }
    body += "]}";
    return jsonResponse(200, body);
}

static Response handleSummary() {
    Stats s = store.getStats();
    std::string names[MAX_BUILDINGS];
    int n = store.getBuildings(names, MAX_BUILDINGS);
    std::string body = "{\"total\":" + std::to_string(s.total) + ",\"available\":" + std::to_string(s.available) +
                       ",\"unavailable\":" + std::to_string(s.unavailable) + ",\"seats\":" + std::to_string(s.seats) + ",\"buildings\":[";
    for (int i = 0; i < n; i++) {
        if (i > 0) body += ",";
        body += "\"" + jsonEscape(names[i]) + "\"";
    }
    body += "]}";
    return jsonResponse(200, body);
}

static std::string searchRequestToJson(const SearchRequest& item) {
    return "{\"query\":\"" + jsonEscape(item.query) +
           "\",\"availability\":\"" + jsonEscape(item.availability) +
           "\",\"building\":\"" + jsonEscape(item.building) +
           "\",\"groupSize\":" + std::to_string(item.groupSize) +
           ",\"sortBy\":\"" + jsonEscape(item.sortBy) + "\"}";
}

static Response handleSearchQueue() {
    // std::queue has no iterators; inspect a copy so the live queue stays unchanged.
    std::queue<SearchRequest> copy = searchQueue;
    std::string body = "{\"size\":" + std::to_string(copy.size()) + ",\"requests\":[";
    bool first = true;
    while (!copy.empty()) {
        if (!first) body += ",";
        first = false;
        body += searchRequestToJson(copy.front());
        copy.pop();
    }
    body += "]}";
    return jsonResponse(200, body);
}

static Response processNextSearch() {
    if (searchQueue.empty()) return errorResponse(409, "The search queue is empty.");
    SearchRequest current = searchQueue.front();
    searchQueue.pop();

    Filter f;
    f.query = current.query;
    f.availability = current.availability.empty() ? "all" : current.availability;
    f.building = current.building.empty() ? "all" : current.building;
    f.groupSize = current.groupSize;

    int matches[MAX_ROOMS];
    int n = store.filterRooms(f, matches);
    store.sortIndexes(matches, n, current.sortBy.empty() ? "name" : current.sortBy);

    std::string body = "{\"processed\":" + searchRequestToJson(current) +
                       ",\"remaining\":" + std::to_string(searchQueue.size()) +
                       ",\"matchCount\":" + std::to_string(n) + ",\"rooms\":[";
    for (int i = 0; i < n; i++) {
        if (i) body += ",";
        body += roomToJson(store.at(matches[i]));
    }
    body += "]}";
    return jsonResponse(200, body);
}

static Response handleApi(const std::string& method, const std::string& path,
                          const std::map<std::string, std::string>& query, const std::string& body) {
    const std::string base = "/api/classrooms";

    // Queue API: add classroom-search criteria, inspect FIFO order, and process the next search.
    if (path == "/api/search-queue") {
        if (method == "GET") return handleSearchQueue();
        if (method == "POST") {
            std::map<std::string, std::string> form = parseForm(body);
            SearchRequest request;
            request.query = cleanText(form["query"]);
            request.availability = form["availability"].empty() ? "all" : form["availability"];
            request.building = form["building"].empty() ? "all" : cleanText(form["building"]);
            request.sortBy = form["sort"].empty() ? "name" : form["sort"];
            request.groupSize = 0;
            if (!form["groupSize"].empty()) {
                int value;
                if (!parseWholeNumber(form["groupSize"], value) || value < 1 || value > 10000)
                    return errorResponse(400, "Group size must be a whole number from 1 to 10,000.");
                request.groupSize = value;
                request.availability = "available";
            }
            if (request.availability != "all" && request.availability != "available" && request.availability != "unavailable")
                return errorResponse(400, "Choose a valid availability filter.");
            if (request.sortBy != "name" && request.sortBy != "capacity-asc" && request.sortBy != "capacity-desc")
                request.sortBy = "name";
            searchQueue.push(request);
            return jsonResponse(201, "{\"ok\":true,\"position\":" + std::to_string(searchQueue.size()) + "}");
        }
        if (method == "DELETE") return processNextSearch();
        return errorResponse(405, "Method not allowed.");
    }
    if (path == "/api/summary" && method == "GET") return handleSummary();
    if (path == "/api/reset" && method == "POST") {
        store.loadSamples();
        searchQueue = std::queue<SearchRequest>();
        if (!persist()) return errorResponse(500, "Could not write the data file.");
        return jsonResponse(200, "{\"ok\":true}");
    }
    if (path == base) {
        if (method == "GET") return handleList(query);
        if (method == "POST") {
            Classroom room; std::string err;
            if (!roomFromForm(parseForm(body), room, err) || !store.addRoom(room, err)) return errorResponse(400, err);
            if (!persist()) return errorResponse(500, "Room added but the data file could not be written.");
            return jsonResponse(201, roomToJson(store.at(store.size() - 1)));
        }
        return errorResponse(405, "Method not allowed.");
    }
    if (path.compare(0, base.size() + 1, base + "/") == 0) {
        std::string id = urlDecode(path.substr(base.size() + 1));
        int index = store.findIndexById(id);
        if (method == "GET") {
            if (index == -1) return errorResponse(404, "Classroom not found.");
            return jsonResponse(200, roomToJson(store.at(index)));
        }
        if (method == "PUT") {
            if (index == -1) return errorResponse(404, "Classroom not found.");
            Classroom room; std::string err;
            if (!roomFromForm(parseForm(body), room, err) || !store.updateRoom(id, room, err)) return errorResponse(400, err);
            if (!persist()) return errorResponse(500, "Room updated but the data file could not be written.");
            return jsonResponse(200, roomToJson(store.at(store.findIndexById(id))));
        }
        if (method == "DELETE") {
            if (!store.removeRoom(id)) return errorResponse(404, "Classroom not found.");
            if (!persist()) return errorResponse(500, "Room deleted but the data file could not be written.");
            return jsonResponse(200, "{\"ok\":true}");
        }
        return errorResponse(405, "Method not allowed.");
    }
    return errorResponse(404, "Unknown API route.");
}

// ---------------- static files ----------------
static std::string mimeFor(const std::string& path) {
    size_t dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : RoomStore::lower(path.substr(dot));
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

static Response handleStatic(std::string path) {
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) return errorResponse(400, "Bad path.");
    std::ifstream file((FRONTEND_DIR + path).c_str(), std::ios::binary);
    if (!file || std::filesystem::is_directory(FRONTEND_DIR + path)) {
        Response r = {404, "text/plain; charset=utf-8", "404 Not Found"};
        return r;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    Response r = {200, mimeFor(path), buf.str()};
    return r;
}

// ---------------- HTTP plumbing ----------------
static const char* statusText(int code) {
    switch (code) {
        case 200: return "OK"; case 201: return "Created"; case 400: return "Bad Request";
        case 404: return "Not Found"; case 405: return "Method Not Allowed"; default: return "Internal Server Error";
    }
}

static void sendAll(sock_t s, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = (int)send(s, data.data() + sent, (int)(data.size() - sent), SEND_FLAGS);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

static void handleClient(sock_t client) {
    std::string raw;
    char chunk[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos && raw.size() < 65536) {
        int n = (int)recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) return;
        raw.append(chunk, (size_t)n);
        headerEnd = raw.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos) return;

    std::string head = raw.substr(0, headerEnd);
    std::string body = raw.substr(headerEnd + 4);
    std::istringstream hs(head);
    std::string method, target, version;
    hs >> method >> target >> version;

    size_t contentLength = 0;
    std::string lowHead = RoomStore::lower(head);
    size_t cl = lowHead.find("content-length:");
    if (cl != std::string::npos) contentLength = (size_t)std::atol(head.c_str() + cl + 15);
    if (contentLength > 1000000) contentLength = 1000000;
    while (body.size() < contentLength) {
        int n = (int)recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        body.append(chunk, (size_t)n);
    }

    std::string path = target, queryText;
    size_t qm = target.find('?');
    if (qm != std::string::npos) { path = target.substr(0, qm); queryText = target.substr(qm + 1); }

    Response res;
    if (path.compare(0, 5, "/api/") == 0) res = handleApi(method, path, parseForm(queryText), body);
    else if (method == "GET") res = handleStatic(urlDecode(path));
    else res = errorResponse(405, "Method not allowed.");

    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n"
        << "Content-Type: " << res.type << "\r\n"
        << "Content-Length: " << res.body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "X-Content-Type-Options: nosniff\r\n"
        << "Connection: close\r\n\r\n";
    sendAll(client, out.str());
    sendAll(client, res.body);
}

int main(int argc, char** argv) {
    // Port: command line first, then the PORT variable (hosting services set it), then 8080.
    const char* portEnv = std::getenv("PORT");
    int port = argc > 1 ? std::atoi(argv[1]) : (portEnv ? std::atoi(portEnv) : 8080);
    // On your own computer only you can connect. On a host (PORT is set) it must accept outside connections.
    bool publicMode = (portEnv != NULL);

    std::filesystem::create_directories("data");
    if (!store.loadFromFile(DATA_FILE)) {          // first run: no data file yet
        store.loadSamples();
        store.saveToFile(DATA_FILE);
    }
    std::cout << "Loaded " << store.size() << " classrooms into the array.\n";

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::cerr << "WSAStartup failed\n"; return 1; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) { std::cerr << "Could not create socket\n"; return 1; }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(publicMode ? INADDR_ANY : INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "Could not use port " << port << ". Is the server already running?\n";
        return 1;
    }
    listen(server, 16);
    std::cout << "UCC Classroom Availability Finder running on port " << port
              << (publicMode ? " (public mode)" : " at http://localhost:" + std::to_string(port)) << "\nPress Ctrl+C to stop.\n";

    for (;;) {
        sock_t client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        // Do not wait forever for a client that connects and sends nothing (browsers sometimes do).
#ifdef _WIN32
        DWORD timeoutMs = 2000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
#else
        timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif
        handleClient(client);
        CLOSE_SOCKET(client);
    }
}
