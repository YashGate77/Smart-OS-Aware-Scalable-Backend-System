#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <winsock2.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <list>
#include <unordered_map>
#include <mysql.h>
#include <ctime>
#include <cstdlib>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libmysql.lib")
using namespace std;

// ─────────────────────────────────────────
//  TOKEN MANAGER
// ─────────────────────────────────────────
class TokenManager {
public:
    // Generate a unique token
    string generateToken(const string& username) {
        unique_lock<mutex> lock(tokenMutex);

        // token = "TKN_username_randomNumber"
        string token = "TKN_" + username + "_"
                     + to_string(rand() % 900000 + 100000);

        // Store token → username mapping
        tokenStore[token] = username;
        cout << "[Token] Generated for "
             << username << ": " << token << "\n";
        return token;
    }

    // Validate token — returns username or ""
    string validateToken(const string& token) {
        unique_lock<mutex> lock(tokenMutex);
        auto it = tokenStore.find(token);
        if (it != tokenStore.end()) {
            cout << "[Token] Valid token for: "
                 << it->second << "\n";
            return it->second;
        }
        cout << "[Token] Invalid token: " << token << "\n";
        return "";
    }

    // Delete token on logout
    bool deleteToken(const string& token) {
        unique_lock<mutex> lock(tokenMutex);
        auto it = tokenStore.find(token);
        if (it != tokenStore.end()) {
            cout << "[Token] Deleted token for: "
                 << it->second << "\n";
            tokenStore.erase(it);
            return true;
        }
        return false;
    }

    // List all active tokens
    string listTokens() {
        unique_lock<mutex> lock(tokenMutex);
        string result = "";
        for (auto& pair : tokenStore)
            result += pair.second + " → "
                    + pair.first + "\n";
        return result.empty() ? "No active tokens" : result;
    }

private:
    unordered_map<string, string> tokenStore;
    mutex tokenMutex;
};

// ─────────────────────────────────────────
//  LRU CACHE
// ─────────────────────────────────────────
class LRUCache {
public:
    int capacity;
    int hits = 0, misses = 0;

    LRUCache(int cap) : capacity(cap) {}

    string get(const string& key) {
        unique_lock<mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) {
            misses++;
            return "";
        }
        cacheList.splice(cacheList.begin(),
                         cacheList, it->second);
        hits++;
        return it->second->second;
    }

    void put(const string& key, const string& val) {
        unique_lock<mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            it->second->second = val;
            cacheList.splice(cacheList.begin(),
                             cacheList, it->second);
            return;
        }
        if ((int)cacheList.size() >= capacity) {
            cacheMap.erase(cacheList.back().first);
            cacheList.pop_back();
        }
        cacheList.emplace_front(key, val);
        cacheMap[key] = cacheList.begin();
    }

    void invalidate(const string& key) {
        unique_lock<mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            cacheList.erase(it->second);
            cacheMap.erase(it);
        }
    }

private:
    list<pair<string,string>> cacheList;
    unordered_map<string,
        list<pair<string,string>>::iterator> cacheMap;
    mutex cacheMutex;
};

// ─────────────────────────────────────────
//  DATABASE
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
            cout << "[DB] Failed: "
                 << mysql_error(conn) << "\n";
        } else {
            cout << "[DB] Connected to MySQL!\n";
        }
    }

    ~Database() { if (conn) mysql_close(conn); }

    bool checkUser(const string& u, const string& p) {
        unique_lock<mutex> lock(dbMutex);
        string q = "SELECT * FROM users WHERE username='"
                 + u + "' AND password='" + p + "'";
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
        mysql_query(conn,
            "SELECT COUNT(*) FROM request_logs");
        MYSQL_RES* r = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(r);
        int count = stoi(string(row[0]));
        mysql_free_result(r);
        if (count >= 1000) return;
        string q = "INSERT INTO request_logs "
                   "(path, priority, handled_by) VALUES ('"
                 + path + "','" + priority + "',"
                 + to_string(workerId) + ")";
        mysql_query(conn, q.c_str());
    }

    string getUsersJSON() {
        unique_lock<mutex> lock(dbMutex);
        if (mysql_query(conn,
            "SELECT id, username FROM users"))
            return "[]";
        MYSQL_RES* r = mysql_store_result(conn);
        string json = "[";
        MYSQL_ROW row;
        bool first = true;
        while ((row = mysql_fetch_row(r))) {
            if (!first) json += ",";
            json += "{\"id\":" + string(row[0])
                  + ",\"username\":\"" + string(row[1])
                  + "\"}";
            first = false;
        }
        json += "]";
        mysql_free_result(r);
        return json;
    }

    string getLogsJSON() {
        unique_lock<mutex> lock(dbMutex);
        if (mysql_query(conn,
            "SELECT * FROM request_logs "
            "ORDER BY log_time DESC LIMIT 10"))
            return "[]";
        MYSQL_RES* r = mysql_store_result(conn);
        string json = "[";
        MYSQL_ROW row;
        bool first = true;
        while ((row = mysql_fetch_row(r))) {
            if (!first) json += ",";
            json += "{\"id\":"    + string(row[0])
                  + ",\"path\":\"" + string(row[1])
                  + "\",\"priority\":\"" + string(row[2])
                  + "\",\"worker\":" + string(row[3])
                  + ",\"time\":\"" + string(row[4])
                  + "\"}";
            first = false;
        }
        json += "]";
        mysql_free_result(r);
        return json;
    }
};

// ─────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────
Database     db;
LRUCache     cache(5);
TokenManager tokenMgr;

// ─────────────────────────────────────────
//  REQUEST STRUCT
// ─────────────────────────────────────────
struct Request {
    SOCKET socket;
    int    priority;
    string path;
    string method;
    string body;
    string headers;

    bool operator>(const Request& o) const {
        return priority > o.priority;
    }
};

// ─────────────────────────────────────────
//  HTTP PARSER
// ─────────────────────────────────────────
struct ParsedRequest {
    string method;   // GET, POST, DELETE
    string path;     // /api/login
    string body;     // {"username":"yash",...}
    string headers;  // full headers
};

ParsedRequest parseHTTP(const string& raw) {
    ParsedRequest pr;
    istringstream stream(raw);
    string line;

    // First line: "POST /api/login HTTP/1.1"
    getline(stream, line);
    istringstream firstLine(line);
    firstLine >> pr.method >> pr.path;

    // Read headers
    string headerSection = "";
    while (getline(stream, line) && line != "\r")
        headerSection += line + "\n";
    pr.headers = headerSection;

    // Read body (after blank line)
    string bodySection = "";
    while (getline(stream, line))
        bodySection += line;
    pr.body = bodySection;

    return pr;
}

// Extract JSON value: {"username":"yash"} → "yash"
string extractJSON(const string& json,
                   const string& key) {
    string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    pos += search.size();
    size_t end = json.find("\"", pos);
    return json.substr(pos, end - pos);
}

// Extract token from headers
// "Authorization: Bearer TKN_yash_123"
string extractToken(const string& headers) {
    string search = "Authorization: Bearer ";
    size_t pos = headers.find(search);
    if (pos == string::npos) return "";
    pos += search.size();
    size_t end = headers.find("\n", pos);
    string token = headers.substr(pos, end - pos);
    // trim \r if present
    if (!token.empty() && token.back() == '\r')
        token.pop_back();
    return token;
}

int getPriority(const string& path) {
    if (path.find("/api/login")  != string::npos) return 1;
    if (path.find("/api/logout") != string::npos) return 1;
    if (path.find("/api/users")  != string::npos) return 2;
    if (path.find("/api/logs")   != string::npos) return 2;
    return 2;
}

string getPriorityLabel(int p) {
    if (p == 1) return "HIGH";
    if (p == 2) return "NORMAL";
    return "LOW";
}

// ─────────────────────────────────────────
//  RESPONSE BUILDERS
// ─────────────────────────────────────────
string jsonResponse(int code, const string& body) {
    string status =
        code == 200 ? "200 OK" :
        code == 201 ? "201 Created" :
        code == 401 ? "401 Unauthorized" :
        code == 404 ? "404 Not Found" :
                      "500 Internal Server Error";
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + to_string(body.size())
         + "\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

string htmlResponse(const string& body) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html\r\n"
           "Content-Length: " + to_string(body.size())
         + "\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

// ─────────────────────────────────────────
//  THREAD POOL
// ─────────────────────────────────────────
class ThreadPool {
public:
    ThreadPool(int n) {
        for (int i = 0; i < n; i++)
            workers.emplace_back(
                &ThreadPool::workerFunction, this, i);
    }

    void addTask(SOCKET sock, const string& raw) {
        ParsedRequest pr = parseHTTP(raw);
        Request req;
        req.socket   = sock;
        req.method   = pr.method;
        req.path     = pr.path;
        req.body     = pr.body;
        req.headers  = pr.headers;
        req.priority = getPriority(pr.path);

        {
            unique_lock<mutex> lock(qMutex);
            taskQueue.push(req);
            cout << "[Scheduler] " << req.method
                 << " " << req.path
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

    void handleClient(const Request& req, int wid) {
        string method  = req.method;
        string path    = req.path;
        string body    = req.body;
        string headers = req.headers;
        string label   = getPriorityLabel(req.priority);
        string response;

        cout << "[Worker " << wid << "] "
             << method << " " << path << "\n";

        // ══════════════════════════════════
        //  POST /api/login
        //  Body: {"username":"yash","password":"1234"}
        // ══════════════════════════════════
        if (method == "POST" &&
            path == "/api/login") {

            string user = extractJSON(body, "username");
            string pass = extractJSON(body, "password");

            if (user.empty() || pass.empty()) {
                response = jsonResponse(401,
                    "{\"status\":\"error\","
                    "\"message\":\"Missing credentials\"}");
            } else {
                // Check cache first
                string cacheKey = "auth:" + user + pass;
                string cached   = cache.get(cacheKey);
                bool   ok;

                if (!cached.empty()) {
                    ok = (cached == "true");
                    cout << "[Worker " << wid
                         << "] Auth from CACHE!\n";
                } else {
                    ok = db.checkUser(user, pass);
                    cache.put(cacheKey,
                              ok ? "true" : "false");
                }

                if (ok) {
                    string token =
                        tokenMgr.generateToken(user);
                    response = jsonResponse(200,
                        "{\"status\":\"success\","
                        "\"message\":\"Login successful\","
                        "\"token\":\"" + token + "\","
                        "\"username\":\"" + user + "\"}");
                } else {
                    response = jsonResponse(401,
                        "{\"status\":\"error\","
                        "\"message\":"
                        "\"Invalid credentials\"}");
                }
            }
            db.logRequest(path, label, wid);
        }

        // ══════════════════════════════════
        //  GET /api/users  (requires token)
        // ══════════════════════════════════
        else if (method == "GET" &&
                 path == "/api/users") {

            string token = extractToken(headers);
            string user  = tokenMgr.validateToken(token);

            if (user.empty()) {
                response = jsonResponse(401,
                    "{\"status\":\"error\","
                    "\"message\":\"Invalid or missing "
                    "token. Login first.\"}");
            } else {
                // Check cache
                string cached = cache.get("users_json");
                string json;
                if (!cached.empty()) {
                    json = cached;
                    cout << "[Worker " << wid
                         << "] Users from CACHE!\n";
                } else {
                    json = db.getUsersJSON();
                    cache.put("users_json", json);
                }
                response = jsonResponse(200,
                    "{\"status\":\"success\","
                    "\"requested_by\":\"" + user + "\","
                    "\"users\":" + json + "}");
            }
            db.logRequest(path, label, wid);
        }

        // ══════════════════════════════════
        //  GET /api/logs  (requires token)
        // ══════════════════════════════════
        else if (method == "GET" &&
                 path == "/api/logs") {

            string token = extractToken(headers);
            string user  = tokenMgr.validateToken(token);

            if (user.empty()) {
                response = jsonResponse(401,
                    "{\"status\":\"error\","
                    "\"message\":\"Unauthorized\"}");
            } else {
                cache.invalidate("logs_json");
                string json = db.getLogsJSON();
                response = jsonResponse(200,
                    "{\"status\":\"success\","
                    "\"requested_by\":\"" + user + "\","
                    "\"logs\":" + json + "}");
            }
            db.logRequest(path, label, wid);
        }

        // ══════════════════════════════════
        //  DELETE /api/logout
        // ══════════════════════════════════
        else if (method == "DELETE" &&
                 path == "/api/logout") {

            string token = extractToken(headers);
            bool   ok    = tokenMgr.deleteToken(token);

            if (ok) {
                response = jsonResponse(200,
                    "{\"status\":\"success\","
                    "\"message\":\"Logged out "
                    "successfully\"}");
            } else {
                response = jsonResponse(401,
                    "{\"status\":\"error\","
                    "\"message\":\"Token not found\"}");
            }
            db.logRequest(path, label, wid);
        }

        // ══════════════════════════════════
        //  GET / — API Dashboard (HTML)
        // ══════════════════════════════════
        else if (path == "/" || path == "") {
            string activeTokens = tokenMgr.listTokens();
            string htmlBody =
                "<!DOCTYPE html><html><body style='"
                "font-family:Arial;padding:40px;"
                "background:#1a1a2e;color:white'>"
                "<h1 style='color:#f39c12'>SOSBS "
                "REST API</h1>"
                "<p style='color:#aaa'>Smart OS-Aware "
                "Scalable Backend System — Phase 6</p>"
                "<hr style='border-color:#333'>"

                "<h2 style='color:#2ecc71'>"
                "Available Endpoints</h2>"
                "<table border='1' style='border-collapse"
                ":collapse;width:100%'>"
                "<tr style='background:#27ae60'>"
                "<th>Method</th><th>Endpoint</th>"
                "<th>Auth</th><th>Description</th></tr>"
                "<tr><td style='color:#3498db'>POST</td>"
                "<td>/api/login</td><td>No</td>"
                "<td>Login → get token</td></tr>"
                "<tr><td style='color:#2ecc71'>GET</td>"
                "<td>/api/users</td><td>Yes</td>"
                "<td>Get all users</td></tr>"
                "<tr><td style='color:#2ecc71'>GET</td>"
                "<td>/api/logs</td><td>Yes</td>"
                "<td>Get request logs</td></tr>"
                "<tr><td style='color:#e74c3c'>DELETE</td>"
                "<td>/api/logout</td><td>Yes</td>"
                "<td>Logout + delete token</td></tr>"
                "</table>"

                "<h2 style='color:#3498db'>"
                "How to Test (Postman)</h2>"
                "<p><b style='color:#f39c12'>Step 1 — "
                "Login:</b><br>"
                "POST http://localhost:8080/api/login<br>"
                "Body (JSON): {\"username\":\"yash\","
                "\"password\":\"1234\"}<br>"
                "→ Copy the token from response</p>"
                "<p><b style='color:#f39c12'>Step 2 — "
                "Get Users:</b><br>"
                "GET http://localhost:8080/api/users<br>"
                "Header: Authorization: Bearer "
                "YOUR_TOKEN_HERE</p>"
                "<p><b style='color:#f39c12'>Step 3 — "
                "Logout:</b><br>"
                "DELETE http://localhost:8080/api/logout<br>"
                "Header: Authorization: Bearer "
                "YOUR_TOKEN_HERE</p>"

                "<h2 style='color:#e67e22'>"
                "Active Tokens</h2><pre style='color:#aaa'>"
                + activeTokens + "</pre>"
                "<h2 style='color:#9b59b6'>"
                "Cache Stats</h2>"
                "<p>Hits: <b style='color:#2ecc71'>"
                + to_string(cache.hits) + "</b> | "
                "Misses: <b style='color:#e74c3c'>"
                + to_string(cache.misses) + "</b></p>"
                "</body></html>";
            response = htmlResponse(htmlBody);
            db.logRequest(path, label, wid);
        }

        // ══════════════════════════════════
        //  404 — Unknown route
        // ══════════════════════════════════
        else {
            response = jsonResponse(404,
                "{\"status\":\"error\","
                "\"message\":\"Route not found\","
                "\"available\":[\"/api/login\","
                "\"/api/users\",\"/api/logs\","
                "\"/api/logout\"]}");
        }

        send(req.socket, response.c_str(),
             response.size(), 0);
        closesocket(req.socket);
    }
};

// ─────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────
int main() {
    srand(time(NULL));  // for token randomness

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
        cout << "Bind failed! "
             << WSAGetLastError() << "\n"; return 1;
    }
    if (listen(server_fd, 10) == SOCKET_ERROR) {
        cout << "Listen failed!\n"; return 1;
    }

    ThreadPool pool(4);

    cout << "\n╔══════════════════════════════════╗\n";
    cout << "║   SOSBS Server — Phase 6 READY  ║\n";
    cout << "║   http://localhost:8080          ║\n";
    cout << "╠══════════════════════════════════╣\n";
    cout << "║ POST   /api/login                ║\n";
    cout << "║ GET    /api/users  (token needed)║\n";
    cout << "║ GET    /api/logs   (token needed)║\n";
    cout << "║ DELETE /api/logout (token needed)║\n";
    cout << "╚══════════════════════════════════╝\n\n";

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
