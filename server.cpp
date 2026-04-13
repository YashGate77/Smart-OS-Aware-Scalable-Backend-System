#include <iostream>
#include <cstring>
#include <string>
#include <winsock2.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <list>
#include <unordered_map>
#include <mysql.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libmysql.lib")
using namespace std;

// ─────────────────────────────────────────
//  LRU CACHE
// ─────────────────────────────────────────
class LRUCache {
public:
    int capacity;
    int hits   = 0;   // cache hit counter
    int misses = 0;   // cache miss counter

    LRUCache(int cap) : capacity(cap) {
        cout << "[Cache] LRU Cache initialized. "
             << "Capacity: " << cap << " items\n";
    }

    // ── GET: returns "" if not found ──────
    string get(const string& key) {
        unique_lock<mutex> lock(cacheMutex);

        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) {
            misses++;
            cout << "[Cache] MISS → " << key
                 << " | Hits: " << hits
                 << " Misses: " << misses << "\n";
            return "";  // not in cache
        }

        // Move to front (most recently used)
        cacheList.splice(cacheList.begin(),
                         cacheList, it->second);
        hits++;
        cout << "[Cache] HIT  → " << key
             << " | Hits: " << hits
             << " Misses: " << misses << "\n";
        return it->second->second;
    }

    // ── PUT: add/update item in cache ─────
    void put(const string& key, const string& value) {
        unique_lock<mutex> lock(cacheMutex);

        auto it = cacheMap.find(key);

        // If key exists — update and move to front
        if (it != cacheMap.end()) {
            it->second->second = value;
            cacheList.splice(cacheList.begin(),
                             cacheList, it->second);
            cout << "[Cache] UPDATED → " << key << "\n";
            return;
        }

        // If cache is full — evict LRU (back of list)
        if ((int)cacheList.size() >= capacity) {
            string evictedKey = cacheList.back().first;
            cacheMap.erase(evictedKey);
            cacheList.pop_back();
            cout << "[Cache] EVICTED → " << evictedKey << "\n";
        }

        // Insert at front (most recently used)
        cacheList.emplace_front(key, value);
        cacheMap[key] = cacheList.begin();
        cout << "[Cache] STORED → " << key
             << " | Size: " << cacheList.size() << "\n";
    }

    // ── INVALIDATE: remove specific key ───
    void invalidate(const string& key) {
        unique_lock<mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            cacheList.erase(it->second);
            cacheMap.erase(it);
            cout << "[Cache] INVALIDATED → " << key << "\n";
        }
    }

    // ── STATS: hit rate ───────────────────
    void printStats() {
        int total = hits + misses;
        float rate = total > 0
            ? (float)hits / total * 100 : 0;
        cout << "[Cache] Stats → "
             << "Hits: "   << hits
             << " Misses: " << misses
             << " Hit Rate: " << rate << "%\n";
    }

private:
    list<pair<string,string>>                    cacheList;
    unordered_map<string,
        list<pair<string,string>>::iterator>     cacheMap;
    mutex cacheMutex;
};

// Global cache — capacity 5 items
LRUCache cache(5);

// ─────────────────────────────────────────
//  DATABASE MANAGER
// ─────────────────────────────────────────
class Database {
public:
    MYSQL* conn;
    mutex  dbMutex;

    Database() {
        conn = mysql_init(NULL);
        if (!mysql_real_connect(conn,
                "localhost", "root", "1877",
                "os_project", 3306, NULL, 0)) {
            cout << "[DB] Connection failed: "
                 << mysql_error(conn) << "\n";
        } else {
            cout << "[DB] Connected to MySQL!\n";
        }
    }

    ~Database() {
        if (conn) mysql_close(conn);
    }

    bool checkUser(const string& user,
                   const string& pass) {
        unique_lock<mutex> lock(dbMutex);
        string q = "SELECT * FROM users WHERE username='"
                 + user + "' AND password='" + pass + "'";
        if (mysql_query(conn, q.c_str())) return false;
        MYSQL_RES* r = mysql_store_result(conn);
        bool found = mysql_num_rows(r) > 0;
        mysql_free_result(r);
        return found;
    }

    void logRequest(const string& path,
                    const string& priority,
                    int workerId) {
        unique_lock<mutex> lock(dbMutex);
        string q = "INSERT INTO request_logs "
                   "(path, priority, handled_by) VALUES ('"
                 + path + "','" + priority + "',"
                 + to_string(workerId) + ")";
        mysql_query(conn, q.c_str());
    }

    string getLogs() {
        unique_lock<mutex> lock(dbMutex);
        if (mysql_query(conn,
            "SELECT * FROM request_logs "
            "ORDER BY log_time DESC LIMIT 10"))
            return "Query failed.";

        MYSQL_RES* r = mysql_store_result(conn);
        string html =
            "<table border='1' style='border-collapse:"
            "collapse;width:100%;color:white'>"
            "<tr style='background:#2E75B6'>"
            "<th>ID</th><th>Path</th><th>Priority</th>"
            "<th>Worker</th><th>Time</th></tr>";
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(r)))
            html += "<tr><td>" + string(row[0]) +
                    "</td><td>" + string(row[1]) +
                    "</td><td>" + string(row[2]) +
                    "</td><td>" + string(row[3]) +
                    "</td><td>" + string(row[4]) +
                    "</td></tr>";
        html += "</table>";
        mysql_free_result(r);
        return html;
    }

    string getUsers() {
        unique_lock<mutex> lock(dbMutex);
        if (mysql_query(conn,
            "SELECT id, username FROM users"))
            return "Query failed.";

        MYSQL_RES* r = mysql_store_result(conn);
        string html =
            "<table border='1' style='border-collapse:"
            "collapse;width:100%;color:white'>"
            "<tr style='background:#27ae60'>"
            "<th>ID</th><th>Username</th></tr>";
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(r)))
            html += "<tr><td>" + string(row[0]) +
                    "</td><td>" + string(row[1]) +
                    "</td></tr>";
        html += "</table>";
        mysql_free_result(r);
        return html;
    }
};

Database db;

// ─────────────────────────────────────────
//  REQUEST STRUCT
// ─────────────────────────────────────────
struct Request {
    SOCKET socket;
    int    priority;
    string path;
    bool operator>(const Request& o) const {
        return priority > o.priority;
    }
};

// ─────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────
string extractPath(const string& raw) {
    size_t s = raw.find(' ');
    if (s == string::npos) return "/";
    size_t e = raw.find(' ', s + 1);
    if (e == string::npos) return "/";
    return raw.substr(s + 1, e - s - 1);
}

string extractParam(const string& path,
                    const string& key) {
    size_t pos = path.find(key + "=");
    if (pos == string::npos) return "";
    pos += key.size() + 1;
    size_t end = path.find('&', pos);
    return end == string::npos
        ? path.substr(pos)
        : path.substr(pos, end - pos);
}

int getPriority(const string& path) {
    if (path.find("/high")   != string::npos) return 1;
    if (path.find("/login")  != string::npos) return 1;
    if (path.find("/normal") != string::npos) return 2;
    if (path.find("/users")  != string::npos) return 2;
    if (path.find("/logs")   != string::npos) return 2;
    if (path.find("/low")    != string::npos) return 3;
    return 2;
}

string getPriorityLabel(int p) {
    if (p == 1) return "HIGH";
    if (p == 2) return "NORMAL";
    return "LOW";
}

// ─────────────────────────────────────────
//  THREAD POOL
// ─────────────────────────────────────────
class ThreadPool {
public:
    ThreadPool(int n) {
        cout << "[ThreadPool] Starting " << n
             << " workers...\n";
        for (int i = 0; i < n; i++)
            workers.emplace_back(
                &ThreadPool::workerFunction, this, i);
    }

    void addTask(SOCKET sock, const string& raw) {
        string path = extractPath(raw);
        Request req;
        req.socket   = sock;
        req.priority = getPriority(path);
        req.path     = path;
        {
            unique_lock<mutex> lock(qMutex);
            taskQueue.push(req);
            cout << "[Scheduler] Queued: " << path
                 << " [" << getPriorityLabel(req.priority)
                 << "]\n";
        }
        cv.notify_one();
    }

    ~ThreadPool() {
        { unique_lock<mutex> lock(qMutex); stop = true; }
        cv.notify_all();
        for (auto& t : workers)
            if (t.joinable()) t.join();
    }

private:
    vector<thread> workers;
    priority_queue<Request, vector<Request>,
                   greater<Request>> taskQueue;
    mutex              qMutex;
    condition_variable cv;
    bool               stop = false;

    void workerFunction(int id) {
        cout << "[Worker " << id << "] Ready.\n";
        while (true) {
            Request req;
            {
                unique_lock<mutex> lock(qMutex);
                cv.wait(lock, [this] {
                    return !taskQueue.empty() || stop;
                });
                if (stop && taskQueue.empty()) return;
                req = taskQueue.top();
                taskQueue.pop();
            }
            handleClient(req, id);
        }
    }

    // ─────────────────────────────────────
    //  HANDLE CLIENT — Cache integrated
    // ─────────────────────────────────────
    void handleClient(const Request& req, int wid) {
        string path  = req.path;
        string label = getPriorityLabel(req.priority);
        string body;
        bool   fromCache = false;

        // ── ROUTE: /login ─────────────────
        if (path.find("/login") != string::npos) {
            string user = extractParam(path, "user");
            string pass = extractParam(path, "pass");

            // Cache key = "login:yash:1234"
            string cacheKey = "login:" + user + ":" + pass;
            string cached   = cache.get(cacheKey);

            if (!cached.empty()) {
                // CACHE HIT — no DB query needed!
                body      = cached;
                fromCache = true;
                cout << "[Worker " << wid
                     << "] Login served from CACHE!\n";
            } else {
                // CACHE MISS — query DB
                bool ok  = db.checkUser(user, pass);
                string color = ok ? "#2ecc71" : "#e74c3c";
                string status = ok ? "SUCCESS" : "FAILED";
                string msg = ok
                    ? "Welcome, " + user + "!"
                    : "Invalid credentials!";

                body =
                    "<!DOCTYPE html><html><body style='"
                    "font-family:Arial;display:flex;"
                    "justify-content:center;align-items:"
                    "center;height:100vh;margin:0;"
                    "background:#1a1a2e'>"
                    "<div style='text-align:center;"
                    "color:white'>"
                    "<h1 style='color:" + color
                    + "'>Login " + status + "</h1>"
                    "<p style='font-size:24px'>"
                    + msg + "</p>"
                    "<p>Worker: <b>" + to_string(wid)
                    + "</b> | Source: <b style='color:"
                    "#e67e22'>DATABASE</b></p>"
                    "<br>"
                    "<a href='/users' style='color:#3498db'>"
                    "Users</a> | "
                    "<a href='/logs' style='color:#3498db'>"
                    "Logs</a> | "
                    "<a href='/cache' style='color:#f39c12'>"
                    "Cache Stats</a>"
                    "</div></body></html>";

                // Store result in cache
                cache.put(cacheKey, body);
            }

            // Update source label if from cache
            if (fromCache) {
                body =
                    "<!DOCTYPE html><html><body style='"
                    "font-family:Arial;display:flex;"
                    "justify-content:center;align-items:"
                    "center;height:100vh;margin:0;"
                    "background:#1a1a2e'>"
                    "<div style='text-align:center;"
                    "color:white'>"
                    "<h2 style='color:#f39c12'>"
                    "Served from LRU CACHE!</h2>"
                    "<p style='font-size:20px'>"
                    "No database query needed</p>"
                    "<p>Worker: <b>" + to_string(wid)
                    + "</b> | Source: <b style='color:"
                    "#2ecc71'>CACHE (instant)</b></p>"
                    "<br>"
                    "<a href='/users' style='color:#3498db'>"
                    "Users</a> | "
                    "<a href='/logs' style='color:#3498db'>"
                    "Logs</a> | "
                    "<a href='/cache' style='color:#f39c12'>"
                    "Cache Stats</a>"
                    "</div></body></html>";
            }
            db.logRequest(path, label, wid);
        }

        // ── ROUTE: /users ─────────────────
        else if (path == "/users") {
            string cached = cache.get("page:/users");
            if (!cached.empty()) {
                body      = cached;
                fromCache = true;
            } else {
                string table = db.getUsers();
                body =
                    "<!DOCTYPE html><html><body style='"
                    "font-family:Arial;padding:30px;"
                    "background:#1a1a2e;color:white'>"
                    "<h1 style='color:#2ecc71'>"
                    "Users Table</h1>"
                    "<p style='color:#e67e22'>"
                    "Source: DATABASE</p>"
                    + table +
                    "<br><a href='/' style='color:#3498db'>"
                    "Home</a> | "
                    "<a href='/cache' style='color:#f39c12'>"
                    "Cache Stats</a>"
                    "</body></html>";
                cache.put("page:/users", body);
            }
            if (fromCache) {
                string table = db.getUsers();
                body =
                    "<!DOCTYPE html><html><body style='"
                    "font-family:Arial;padding:30px;"
                    "background:#1a1a2e;color:white'>"
                    "<h1 style='color:#2ecc71'>"
                    "Users Table</h1>"
                    "<p style='color:#2ecc71'>"
                    "Source: LRU CACHE (instant!)</p>"
                    + table +
                    "<br><a href='/' style='color:#3498db'>"
                    "Home</a> | "
                    "<a href='/cache' style='color:#f39c12'>"
                    "Cache Stats</a>"
                    "</body></html>";
            }
            db.logRequest(path, label, wid);
        }

        // ── ROUTE: /logs ──────────────────
        else if (path == "/logs") {
            // Logs always fresh — no cache
            // (data changes every request)
            cache.invalidate("page:/logs");
            string table = db.getLogs();
            body =
                "<!DOCTYPE html><html><body style='"
                "font-family:Arial;padding:30px;"
                "background:#1a1a2e;color:white'>"
                "<h1 style='color:#3498db'>"
                "Request Logs</h1>"
                "<p style='color:#e67e22'>"
                "Always fresh — cache disabled for logs</p>"
                + table +
                "<br><a href='/' style='color:#3498db'>"
                "Home</a> | "
                "<a href='/cache' style='color:#f39c12'>"
                "Cache Stats</a>"
                "</body></html>";
            db.logRequest(path, label, wid);
        }

        // ── ROUTE: /cache (stats page) ────
        else if (path == "/cache") {
            cache.printStats();
            int total = cache.hits + cache.misses;
            float rate = total > 0
                ? (float)cache.hits / total * 100 : 0;

            body =
                "<!DOCTYPE html><html><body style='"
                "font-family:Arial;padding:30px;"
                "background:#1a1a2e;color:white'>"
                "<h1 style='color:#f39c12'>"
                "LRU Cache Statistics</h1>"
                "<table border='1' style='border-collapse:"
                "collapse;width:50%;color:white'>"
                "<tr style='background:#f39c12;color:black'>"
                "<th>Metric</th><th>Value</th></tr>"
                "<tr><td>Cache Hits</td><td style='color:"
                "#2ecc71'><b>" + to_string(cache.hits)
                + "</b></td></tr>"
                "<tr><td>Cache Misses</td><td style='color:"
                "#e74c3c'><b>" + to_string(cache.misses)
                + "</b></td></tr>"
                "<tr><td>Total Requests</td><td><b>"
                + to_string(total) + "</b></td></tr>"
                "<tr><td>Hit Rate</td><td style='color:"
                "#f39c12'><b>" + to_string((int)rate)
                + "%</b></td></tr>"
                "<tr><td>Cache Capacity</td><td><b>"
                + to_string(cache.capacity)
                + " items</b></td></tr>"
                "</table>"
                "<br><p style='color:#aaa'>Visit same URL "
                "twice to see cache hit!</p>"
                "<a href='/' style='color:#3498db'>Home</a>"
                "</body></html>";
        }

        // ── ROUTE: / (Home) ───────────────
        else {
            db.logRequest(path, label, wid);
            body =
                "<!DOCTYPE html><html><body style='"
                "font-family:Arial;display:flex;"
                "justify-content:center;align-items:"
                "center;height:100vh;margin:0;"
                "background:#1a1a2e'>"
                "<div style='text-align:center;"
                "color:white'>"
                "<h1 style='color:#f39c12'>SOSBS</h1>"
                "<p style='color:#aaa'>Smart OS-Aware "
                "Scalable Backend System</p><br>"
                "<p><a href='/login?user=yash&pass=1234' "
                "style='color:#2ecc71;font-size:18px'>"
                "Login as Yash</a></p>"
                "<p><a href='/login?user=admin&pass=admin123'"
                " style='color:#2ecc71;font-size:18px'>"
                "Login as Admin</a></p>"
                "<p><a href='/login?user=fake&pass=wrong' "
                "style='color:#e74c3c;font-size:18px'>"
                "Login (Wrong)</a></p>"
                "<p><a href='/users' "
                "style='color:#3498db;font-size:18px'>"
                "Users Table</a></p>"
                "<p><a href='/logs' "
                "style='color:#3498db;font-size:18px'>"
                "Request Logs</a></p>"
                "<p><a href='/cache' "
                "style='color:#f39c12;font-size:18px'>"
                "Cache Statistics</a></p>"
                "</div></body></html>";
        }

        string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: " + to_string(body.size())
            + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        send(req.socket, response.c_str(),
             response.size(), 0);
        closesocket(req.socket);
    }
};

// ─────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────
int main() {
    WSADATA wsa;
    SOCKET  server_fd;
    struct sockaddr_in address;

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        cout << "WSAStartup failed!\n"; return 1;
    }
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        cout << "Socket failed!\n"; return 1;
    }

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address,
             sizeof(address)) == SOCKET_ERROR) {
        cout << "Bind failed! " << WSAGetLastError()
             << "\n"; return 1;
    }
    if (listen(server_fd, 10) == SOCKET_ERROR) {
        cout << "Listen failed!\n"; return 1;
    }

    ThreadPool pool(4);

    cout << "\n========================================\n";
    cout << "  SOSBS Server — http://localhost:8080\n";
    cout << "  Phase 5: LRU Cache ACTIVE\n";
    cout << "----------------------------------------\n";
    cout << "  /              → Home\n";
    cout << "  /login?user=yash&pass=1234\n";
    cout << "  /users         → Users table\n";
    cout << "  /logs          → Request logs\n";
    cout << "  /cache         → Cache statistics\n";
    cout << "========================================\n\n";

    while (true) {
        int addrlen   = sizeof(address);
        SOCKET client = accept(server_fd,
                               (struct sockaddr*)&address,
                               &addrlen);
        if (client == INVALID_SOCKET) continue;

        char buffer[4096] = {0};
        recv(client, buffer, 4096, 0);
        pool.addTask(client, string(buffer));
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}
