#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>
#include <algorithm>
#include <openssl/sha.h>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <map>
#include <set>

using namespace std;

// ─────────────────────────────────────────────
// FIX 1: Safe decompression — doubles buffer
//         until uncompress succeeds, no magic *100
// ─────────────────────────────────────────────
vector<unsigned char> safeDecompress(const vector<unsigned char>& compressed) {
    uLongf destLen = compressed.size() * 4;
    vector<unsigned char> dest(destLen);

    while (true) {
        int result = uncompress(dest.data(), &destLen,
                                compressed.data(), compressed.size());
        if (result == Z_OK) {
            dest.resize(destLen);
            return dest;
        } else if (result == Z_BUF_ERROR) {
            destLen *= 2;
            dest.resize(destLen);
        } else {
            cerr << "Error: decompression failed (code " << result << ")\n";
            return {};
        }
    }
}

// ─────────────────────────────────────────────
// FIX 2: Author from ~/.gitconfig or env vars
//         No more hardcoded VaibhavGupta@gmail.com
// ─────────────────────────────────────────────
string getAuthorString() {
    // Try environment variables first (same as real Git)
    const char* name = getenv("GIT_AUTHOR_NAME");
    const char* email = getenv("GIT_AUTHOR_EMAIL");

    string authorName  = name  ? name  : "";
    string authorEmail = email ? email : "";

    // Fall back to ~/.gitconfig
    if (authorName.empty() || authorEmail.empty()) {
        string configPath = string(getenv("HOME") ? getenv("HOME") : ".") + "/.gitconfig";
        ifstream config(configPath);
        string line;
        while (getline(config, line)) {
            if (line.find("name") != string::npos && authorName.empty()) {
                size_t eq = line.find('=');
                if (eq != string::npos)
                    authorName = line.substr(eq + 1);
                while (!authorName.empty() && authorName[0] == ' ')
                    authorName = authorName.substr(1);
            }
            if (line.find("email") != string::npos && authorEmail.empty()) {
                size_t eq = line.find('=');
                if (eq != string::npos)
                    authorEmail = line.substr(eq + 1);
                while (!authorEmail.empty() && authorEmail[0] == ' ')
                    authorEmail = authorEmail.substr(1);
            }
        }
    }

    if (authorName.empty())  authorName  = "Unknown";
    if (authorEmail.empty()) authorEmail = "unknown@example.com";

    time_t now = time(nullptr);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S %z", localtime(&now));

    return authorName + " <" + authorEmail + "> " + timeStr;
}

// ─────────────────────────────────────────────
// Helpers: SHA-1 hex and binary conversion
// ─────────────────────────────────────────────
string bytesToHex(const unsigned char* hash, int len = SHA_DIGEST_LENGTH) {
    stringstream ss;
    for (int i = 0; i < len; ++i)
        ss << hex << setw(2) << setfill('0') << (unsigned int)hash[i];
    return ss.str();
}

void hexToBytes(const string& hex, unsigned char* out) {
    for (size_t i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        string byte = hex.substr(i * 2, 2);
        out[i] = (unsigned char)strtol(byte.c_str(), nullptr, 16);
    }
}

// Write raw bytes to .git/objects/<xx>/<38 chars>
// Returns hash hex string
string writeObject(const string& objectData) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(objectData.c_str()),
         objectData.size(), hash);
    string hashHex = bytesToHex(hash);

    string folder   = ".git/objects/" + hashHex.substr(0, 2);
    string filePath = folder + "/" + hashHex.substr(2);

    if (filesystem::exists(filePath))
        return hashHex; // already stored — content-addressable dedup

    filesystem::create_directories(folder);

    uLongf compLen = compressBound(objectData.size());
    vector<unsigned char> comp(compLen);
    if (compress(comp.data(), &compLen,
                 reinterpret_cast<const Bytef*>(objectData.data()),
                 objectData.size()) != Z_OK) {
        cerr << "Error: compression failed\n";
        return "";
    }
    comp.resize(compLen);

    ofstream out(filePath, ios::binary);
    out.write(reinterpret_cast<const char*>(comp.data()), compLen);
    return hashHex;
}

// Read and decompress a .git/objects file, return raw bytes
vector<unsigned char> readObject(const string& hashHex) {
    string path = ".git/objects/" + hashHex.substr(0, 2) + "/" + hashHex.substr(2);
    if (!filesystem::exists(path)) {
        cerr << "Error: object not found: " << hashHex << "\n";
        return {};
    }
    ifstream f(path, ios::binary);
    vector<unsigned char> comp((istreambuf_iterator<char>(f)), {});
    return safeDecompress(comp);
}

// ─────────────────────────────────────────────
// Blob
// ─────────────────────────────────────────────
string createBlob(const string& filePath, bool write) {
    ifstream f(filePath, ios::binary);
    if (!f) { cerr << "Error: cannot open " << filePath << "\n"; return ""; }

    string content((istreambuf_iterator<char>(f)), {});
    string obj = "blob " + to_string(content.size()) + '\0' + content;

    if (!write) {
        // just compute hash, don't write
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(obj.c_str()), obj.size(), hash);
        return bytesToHex(hash);
    }
    return writeObject(obj);
}

// ─────────────────────────────────────────────
// Tree (recursive)
// ─────────────────────────────────────────────
string writeTree(const string& dirPath) {
    struct Entry { string mode, name; unsigned char hash[SHA_DIGEST_LENGTH]; };
    vector<Entry> entries;

    for (const auto& e : filesystem::directory_iterator(dirPath)) {
        if (e.path().filename() == ".git") continue;

        Entry ent;
        ent.name = e.path().filename().string();
        memset(ent.hash, 0, SHA_DIGEST_LENGTH);

        if (e.is_directory()) {
            string sub = writeTree(e.path().string());
            if (sub.empty()) continue;
            hexToBytes(sub, ent.hash);
            ent.mode = "40000";
        } else if (e.is_regular_file()) {
            string blobHash = createBlob(e.path().string(), true);
            if (blobHash.empty()) continue;
            hexToBytes(blobHash, ent.hash);
            ent.mode = "100644";
        } else continue;

        entries.push_back(ent);
    }

    sort(entries.begin(), entries.end(),
         [](const Entry& a, const Entry& b){ return a.name < b.name; });

    vector<char> body;
    for (const auto& e : entries) {
        string hdr = e.mode + ' ' + e.name;
        body.insert(body.end(), hdr.begin(), hdr.end());
        body.push_back('\0');
        body.insert(body.end(), e.hash, e.hash + SHA_DIGEST_LENGTH);
    }

    string obj = "tree " + to_string(body.size()) + '\0'
               + string(body.begin(), body.end());
    return writeObject(obj);
}

// ─────────────────────────────────────────────
// ls-tree
// ─────────────────────────────────────────────
void lsTree(const string& treeHash, const string& flag) {
    auto data = readObject(treeHash);
    if (data.empty()) return;

    // skip header (everything up to and including first \0)
    size_t idx = 0;
    while (idx < data.size() && data[idx] != '\0') idx++;
    idx++;

    while (idx < data.size()) {
        string mode;
        while (idx < data.size() && data[idx] != ' ')
            mode += (char)data[idx++];
        idx++; // skip space

        string name;
        while (idx < data.size() && data[idx] != '\0')
            name += (char)data[idx++];
        idx++; // skip null

        if (idx + SHA_DIGEST_LENGTH > data.size()) {
            cerr << "Error: unexpected end of tree data\n"; return;
        }
        string hashHex = bytesToHex(&data[idx]);
        idx += SHA_DIGEST_LENGTH;

        string type = (mode == "40000") ? "tree" : "blob";

        if (flag == "--name-only")
            cout << name << "\n";
        else
            cout << mode << " " << type << " " << hashHex << "\t" << name << "\n";
    }
}

// ─────────────────────────────────────────────
// cat-file
// ─────────────────────────────────────────────
void catFile(const string& flag, const string& hashHex) {
    auto data = readObject(hashHex);
    if (data.empty()) return;

    // header ends at first \0
    size_t nullPos = 0;
    while (nullPos < data.size() && data[nullPos] != '\0') nullPos++;

    string header(data.begin(), data.begin() + nullPos);
    // header = "<type> <size>"
    size_t spacePos = header.find(' ');
    string type = header.substr(0, spacePos);
    string sizeStr = header.substr(spacePos + 1);

    if (flag == "-t") {
        cout << type << "\n";
    } else if (flag == "-s") {
        cout << sizeStr << "\n";
    } else if (flag == "-p") {
        // print content after header null
        for (size_t i = nullPos + 1; i < data.size(); ++i)
            cout << (char)data[i];
        cout << "\n";
    } else {
        cerr << "Unknown flag: " << flag << "\n";
    }
}

// ─────────────────────────────────────────────
// FIX 3: Index — proper read/write
//         Format per line: "<hash> <filepath>"
//         mygitAdd writes it; mygitCommit reads it
// ─────────────────────────────────────────────
struct IndexEntry { string hash, path; };

vector<IndexEntry> readIndex() {
    vector<IndexEntry> entries;
    ifstream f(".git/index");
    string line;
    while (getline(f, line)) {
        if (line.size() < 42) continue;
        entries.push_back({ line.substr(0, 40), line.substr(41) });
    }
    return entries;
}

void writeIndex(const vector<IndexEntry>& entries) {
    ofstream f(".git/index", ios::trunc);
    for (const auto& e : entries)
        f << e.hash << " " << e.path << "\n";
}

// Adds or updates one file in the index (by path)
void stageFile(vector<IndexEntry>& index, const string& filePath) {
    string hash = createBlob(filePath, true);
    if (hash.empty()) return;

    // normalise path  (strip leading ./)
    string normPath = filePath;
    if (normPath.size() >= 2 && normPath[0] == '.' && normPath[1] == '/')
        normPath = normPath.substr(2);

    // update if already staged, else append
    for (auto& e : index) {
        if (e.path == normPath) { e.hash = hash; return; }
    }
    index.push_back({ hash, normPath });
}

void mygitAdd(const vector<string>& files) {
    auto index = readIndex();

    for (const string& file : files) {
        if (file == ".") {
            for (const auto& e : filesystem::recursive_directory_iterator(".")) {
                if (!e.is_regular_file()) continue;
                string p = e.path().string();
                if (p.find(".git") != string::npos) continue;
                stageFile(index, p);
            }
        } else if (filesystem::is_directory(file)) {
            for (const auto& e : filesystem::recursive_directory_iterator(file)) {
                if (!e.is_regular_file()) continue;
                string p = e.path().string();
                if (p.find(".git") != string::npos) continue;
                stageFile(index, p);
            }
        } else if (filesystem::is_regular_file(file)) {
            stageFile(index, file);
        } else {
            cerr << "Error: '" << file << "' is not a valid file or directory\n";
        }
    }

    writeIndex(index);
    cout << "Changes staged.\n";
}

// ─────────────────────────────────────────────
// HEAD helpers
// ─────────────────────────────────────────────
// Returns the current commit hash (empty string if none yet)
string getCurrentCommit() {
    ifstream head(".git/HEAD");
    string ref; getline(head, ref);

    if (ref.substr(0, 5) == "ref: ") {
        string refPath = ".git/" + ref.substr(5);
        ifstream rf(refPath);
        if (!rf.is_open()) return ""; // branch exists but no commits yet
        string hash; getline(rf, hash);
        return hash;
    }
    // detached HEAD — ref IS the hash
    return ref;
}

// Returns the ref path that HEAD points to (e.g. "refs/heads/main")
// or empty string if detached
string getHeadRef() {
    ifstream head(".git/HEAD");
    string ref; getline(head, ref);
    if (ref.substr(0, 5) == "ref: ")
        return ref.substr(5);
    return "";
}

void updateRef(const string& refPath, const string& hash) {
    string fullPath = ".git/" + refPath;
    filesystem::create_directories(filesystem::path(fullPath).parent_path());
    ofstream f(fullPath, ios::trunc);
    f << hash << "\n";
}

// ─────────────────────────────────────────────
// Commit — NOW reads the index to build tree
// ─────────────────────────────────────────────
// Builds a tree object from only the staged files (index entries).
// Returns the tree hash.
string buildTreeFromIndex(const vector<IndexEntry>& index) {
    // Group files by their immediate directory
    // We build a nested structure then write bottom-up.

    // For simplicity: re-use writeTree(".") only for root-level files,
    // but correctly filter to ONLY staged paths.
    //
    // Full implementation: build an in-memory tree, write recursively.

    struct InMemoryTree {
        map<string, string>        blobs;  // name → hash
        map<string, InMemoryTree*> subtrees;

        ~InMemoryTree() {
            for (auto& p : subtrees) delete p.second;
        }

        string write() {
            struct Entry { string mode, name; unsigned char hash[SHA_DIGEST_LENGTH]; };
            vector<Entry> entries;

            for (auto& [name, hash] : blobs) {
                Entry e; e.mode = "100644"; e.name = name;
                hexToBytes(hash, e.hash);
                entries.push_back(e);
            }
            for (auto& [name, sub] : subtrees) {
                string subHash = sub->write();
                if (subHash.empty()) continue;
                Entry e; e.mode = "40000"; e.name = name;
                hexToBytes(subHash, e.hash);
                entries.push_back(e);
            }

            sort(entries.begin(), entries.end(),
                 [](const Entry& a, const Entry& b){ return a.name < b.name; });

            vector<char> body;
            for (const auto& e : entries) {
                string hdr = e.mode + ' ' + e.name;
                body.insert(body.end(), hdr.begin(), hdr.end());
                body.push_back('\0');
                body.insert(body.end(), e.hash, e.hash + SHA_DIGEST_LENGTH);
            }

            string obj = "tree " + to_string(body.size()) + '\0'
                       + string(body.begin(), body.end());
            return writeObject(obj);
        }
    };

    InMemoryTree root;

    for (const auto& entry : index) {
        // split path into components
        vector<string> parts;
        stringstream ss(entry.path);
        string part;
        while (getline(ss, part, '/'))
            if (!part.empty()) parts.push_back(part);

        // walk/create the tree structure
        InMemoryTree* cur = &root;
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!cur->subtrees.count(parts[i]))
                cur->subtrees[parts[i]] = new InMemoryTree();
            cur = cur->subtrees[parts[i]];
        }
        cur->blobs[parts.back()] = entry.hash;
    }

    return root.write();
}

void mygitCommit(const string& message) {
    auto index = readIndex();
    if (index.empty()) {
        cerr << "Nothing to commit. Use 'mygit add <file>' first.\n";
        return;
    }

    // FIX: build tree from STAGED files, not entire working directory
    string treeHash = buildTreeFromIndex(index);
    if (treeHash.empty()) { cerr << "Error: failed to build tree.\n"; return; }

    string parentHash = getCurrentCommit();
    string author     = getAuthorString();

    string body;
    body += "tree "      + treeHash  + "\n";
    if (!parentHash.empty())
        body += "parent " + parentHash + "\n";
    body += "author "    + author    + "\n";
    body += "committer " + author    + "\n";
    body += "\n" + message + "\n";

    string obj = "commit " + to_string(body.size()) + '\0' + body;
    string commitHash = writeObject(obj);

    // Update branch ref
    string headRef = getHeadRef();
    if (!headRef.empty()) {
        updateRef(headRef, commitHash);
    } else {
        // detached HEAD — update HEAD directly
        ofstream hf(".git/HEAD", ios::trunc);
        hf << commitHash << "\n";
    }

    // Clear the index — staged changes are now committed
    writeIndex({});

    cout << "[" << commitHash.substr(0, 7) << "] " << message << "\n";
}

// ─────────────────────────────────────────────
// Log
// ─────────────────────────────────────────────
void mygitLog() {
    string commitHash = getCurrentCommit();
    if (commitHash.empty()) {
        cout << "No commits yet.\n"; return;
    }

    while (!commitHash.empty()) {
        auto data = readObject(commitHash);
        if (data.empty()) return;

        size_t nullPos = 0;
        while (nullPos < data.size() && data[nullPos] != '\0') nullPos++;
        string content(data.begin() + nullPos + 1, data.end());

        stringstream ss(content);
        string line, treeHash, parentHash, author, committer, message;
        bool inMsg = false;
        while (getline(ss, line)) {
            if (!inMsg) {
                if (line.rfind("tree ",   0) == 0) treeHash   = line.substr(5);
                else if (line.rfind("parent ",    0) == 0) parentHash = line.substr(7);
                else if (line.rfind("author ",    0) == 0) author     = line.substr(7);
                else if (line.rfind("committer ", 0) == 0) committer  = line.substr(10);
                else if (line.empty()) inMsg = true;
            } else {
                message += line + "\n";
            }
        }

        cout << "commit " << commitHash << "\n";
        if (!parentHash.empty()) cout << "Parent: " << parentHash << "\n";
        cout << "Author: " << author    << "\n";
        cout << "Date:   " << committer << "\n";
        cout << "\n    " << message << "\n";

        commitHash = parentHash;
    }
}

// ─────────────────────────────────────────────
// FIX 4: checkout — restores working directory
//         from any commit hash
// ─────────────────────────────────────────────

// Forward declarations
void restoreTree(const string& treeHash, const string& basePath);

void restoreTree(const string& treeHash, const string& basePath) {
    auto data = readObject(treeHash);
    if (data.empty()) return;

    // skip header
    size_t idx = 0;
    while (idx < data.size() && data[idx] != '\0') idx++;
    idx++;

    while (idx < data.size()) {
        string mode;
        while (idx < data.size() && data[idx] != ' ')
            mode += (char)data[idx++];
        idx++;

        string name;
        while (idx < data.size() && data[idx] != '\0')
            name += (char)data[idx++];
        idx++;

        if (idx + SHA_DIGEST_LENGTH > data.size()) break;
        string entryHash = bytesToHex(&data[idx]);
        idx += SHA_DIGEST_LENGTH;

        string fullPath = basePath + "/" + name;

        if (mode == "40000") {
            // directory
            filesystem::create_directories(fullPath);
            restoreTree(entryHash, fullPath);
        } else {
            // blob — decompress and write file
            auto blob = readObject(entryHash);
            if (blob.empty()) continue;

            // skip blob header
            size_t nul = 0;
            while (nul < blob.size() && blob[nul] != '\0') nul++;
            nul++;

            filesystem::create_directories(
                filesystem::path(fullPath).parent_path());

            ofstream out(fullPath, ios::binary | ios::trunc);
            out.write(reinterpret_cast<const char*>(&blob[nul]),
                      blob.size() - nul);
        }
    }
}

void mygitCheckout(const string& target) {
    // target can be a commit hash OR a branch name
    string commitHash;

    // Check if target is a branch
    string branchRef = "refs/heads/" + target;
    string branchPath = ".git/" + branchRef;
    if (filesystem::exists(branchPath)) {
        ifstream f(branchPath);
        getline(f, commitHash);
        // Update HEAD to point to this branch (attached)
        ofstream head(".git/HEAD", ios::trunc);
        head << "ref: " << branchRef << "\n";
        cout << "Switched to branch '" << target << "'\n";
    } else {
        // Treat as a commit hash (detached HEAD)
        commitHash = target;
        ofstream head(".git/HEAD", ios::trunc);
        head << commitHash << "\n";
        cout << "HEAD is now at " << commitHash.substr(0, 7) << "\n";
    }

    if (commitHash.empty()) {
        cerr << "Error: '" << target << "' is not a branch or commit hash\n";
        return;
    }

    // Read the commit to get its tree hash
    auto commitData = readObject(commitHash);
    if (commitData.empty()) { cerr << "Error: invalid commit\n"; return; }

    size_t nullPos = 0;
    while (nullPos < commitData.size() && commitData[nullPos] != '\0') nullPos++;
    string content(commitData.begin() + nullPos + 1, commitData.end());

    string treeHash;
    stringstream ss(content);
    string line;
    while (getline(ss, line)) {
        if (line.rfind("tree ", 0) == 0) {
            treeHash = line.substr(5);
            break;
        }
    }

    if (treeHash.empty()) { cerr << "Error: could not find tree in commit\n"; return; }

    // Restore the working directory from the tree
    // Only touch tracked files — don't delete untracked files
    restoreTree(treeHash, ".");

    // Clear index — working tree now matches this commit
    writeIndex({});
}

// ─────────────────────────────────────────────
// FIX 5: branch command
//         mygit branch               → list branches
//         mygit branch <name>        → create branch at HEAD
//         mygit branch -d <name>     → delete branch
// ─────────────────────────────────────────────
void mygitBranch(const vector<string>& args) {
    string headsDir = ".git/refs/heads";

    if (args.empty()) {
        // List all branches, mark current with *
        string headRef = getHeadRef();
        string currentBranch;
        if (!headRef.empty() && headRef.rfind("refs/heads/", 0) == 0)
            currentBranch = headRef.substr(string("refs/heads/").size());

        if (!filesystem::exists(headsDir)) {
            cout << "(no branches)\n"; return;
        }
        for (const auto& e : filesystem::directory_iterator(headsDir)) {
            string name = e.path().filename().string();
            cout << (name == currentBranch ? "* " : "  ") << name << "\n";
        }
        return;
    }

    if (args[0] == "-d" || args[0] == "--delete") {
        if (args.size() < 2) { cerr << "Usage: mygit branch -d <name>\n"; return; }
        string path = headsDir + "/" + args[1];
        if (!filesystem::exists(path)) {
            cerr << "Error: branch '" << args[1] << "' not found\n"; return;
        }
        // Prevent deleting current branch
        string headRef = getHeadRef();
        if (headRef == "refs/heads/" + args[1]) {
            cerr << "Error: cannot delete the currently checked-out branch\n"; return;
        }
        filesystem::remove(path);
        cout << "Deleted branch " << args[1] << "\n";
        return;
    }

    // Create new branch at current HEAD
    string branchName = args[0];
    string path = headsDir + "/" + branchName;
    if (filesystem::exists(path)) {
        cerr << "Error: branch '" << branchName << "' already exists\n"; return;
    }

    string currentCommit = getCurrentCommit();
    if (currentCommit.empty()) {
        cerr << "Error: no commits yet — cannot create a branch\n"; return;
    }

    ofstream f(path);
    f << currentCommit << "\n";
    cout << "Created branch '" << branchName
         << "' at " << currentCommit.substr(0, 7) << "\n";
}

// ─────────────────────────────────────────────
// switch — same as checkout for branches
//          kept separate to match real Git UX
// ─────────────────────────────────────────────
void mygitSwitch(const string& branchName) {
    string branchPath = ".git/refs/heads/" + branchName;
    if (!filesystem::exists(branchPath)) {
        cerr << "Error: branch '" << branchName << "' does not exist\n";
        cerr << "Tip: use 'mygit branch " << branchName
             << "' to create it first\n";
        return;
    }
    mygitCheckout(branchName);
}

// ─────────────────────────────────────────────
// status — show staged files vs working tree
// ─────────────────────────────────────────────
void mygitStatus() {
    auto index = readIndex();
    set<string> staged;
    for (const auto& e : index) staged.insert(e.path);

    if (staged.empty()) {
        cout << "Nothing staged for commit.\n";
    } else {
        cout << "Changes to be committed:\n";
        for (const auto& p : staged)
            cout << "  staged: " << p << "\n";
    }

    // Check for modified unstaged files
    cout << "\nUnstaged changes:\n";
    bool anyUnstaged = false;
    for (const auto& entry : filesystem::recursive_directory_iterator(".")) {
        if (!entry.is_regular_file()) continue;
        string p = entry.path().string();
        if (p.find(".git") != string::npos) continue;
        // normalise
        if (p.size() >= 2 && p[0] == '.' && p[1] == '/')
            p = p.substr(2);

        string workingHash = createBlob(entry.path().string(), false);

        bool inIndex = false;
        for (const auto& e : index) {
            if (e.path == p) {
                inIndex = true;
                if (e.hash != workingHash) {
                    cout << "  modified: " << p << "\n";
                    anyUnstaged = true;
                }
                break;
            }
        }
        if (!inIndex && staged.find(p) == staged.end()) {
            cout << "  untracked: " << p << "\n";
            anyUnstaged = true;
        }
    }
    if (!anyUnstaged) cout << "  (clean)\n";
}

// ─────────────────────────────────────────────
// main
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    cout << unitbuf;
    cerr << unitbuf;

    if (argc < 2) {
        cerr << "Usage: mygit <command> [args]\n";
        cerr << "Commands: init, add, commit, log, status, branch, "
                "switch, checkout, write-tree, ls-tree, cat-file, hash-object\n";
        return EXIT_FAILURE;
    }

    string cmd = argv[1];

    // ── init ──────────────────────────────────
    if (cmd == "init") {
        try {
            filesystem::create_directories(".git/objects");
            filesystem::create_directories(".git/refs/heads");
            ofstream(".git/HEAD") << "ref: refs/heads/main\n";
            cout << "Initialized empty MyGit repository in .git/\n";
        } catch (const filesystem::filesystem_error& e) {
            cerr << e.what() << "\n"; return EXIT_FAILURE;
        }
    }

    // ── add ───────────────────────────────────
    else if (cmd == "add") {
        if (argc < 3) { cerr << "Usage: mygit add <file|directory|.>\n"; return EXIT_FAILURE; }
        vector<string> files;
        for (int i = 2; i < argc; ++i) files.push_back(argv[i]);
        mygitAdd(files);
    }

    // ── commit ────────────────────────────────
    else if (cmd == "commit") {
        if (argc == 4 && string(argv[2]) == "-m") {
            mygitCommit(argv[3]);
        } else {
            cerr << "Usage: mygit commit -m \"message\"\n"; return EXIT_FAILURE;
        }
    }

    // ── log ───────────────────────────────────
    else if (cmd == "log") {
        mygitLog();
    }

    // ── status ────────────────────────────────
    else if (cmd == "status") {
        mygitStatus();
    }

    // ── branch ────────────────────────────────
    else if (cmd == "branch") {
        vector<string> args;
        for (int i = 2; i < argc; ++i) args.push_back(argv[i]);
        mygitBranch(args);
    }

    // ── switch ────────────────────────────────
    else if (cmd == "switch") {
        if (argc != 3) { cerr << "Usage: mygit switch <branch>\n"; return EXIT_FAILURE; }
        mygitSwitch(argv[2]);
    }

    // ── checkout ──────────────────────────────
    else if (cmd == "checkout") {
        if (argc != 3) { cerr << "Usage: mygit checkout <branch|commit-hash>\n"; return EXIT_FAILURE; }
        mygitCheckout(argv[2]);
    }

    // ── write-tree ────────────────────────────
    else if (cmd == "write-tree") {
        string h = writeTree(".");
        if (!h.empty()) cout << h << "\n";
    }

    // ── ls-tree ───────────────────────────────
    else if (cmd == "ls-tree") {
        if (argc < 3) { cerr << "Usage: mygit ls-tree [--name-only] <hash>\n"; return EXIT_FAILURE; }
        string flag = (argc == 4) ? argv[2] : "";
        string hash = (argc == 4) ? argv[3] : argv[2];
        lsTree(hash, flag);
    }

    // ── cat-file ──────────────────────────────
    else if (cmd == "cat-file") {
        if (argc != 4) { cerr << "Usage: mygit cat-file [-p|-t|-s] <hash>\n"; return EXIT_FAILURE; }
        catFile(argv[2], argv[3]);
    }

    // ── hash-object ───────────────────────────
    else if (cmd == "hash-object") {
        if (argc < 3) { cerr << "Usage: mygit hash-object [-w] <file>\n"; return EXIT_FAILURE; }
        bool write = (argc == 4 && string(argv[2]) == "-w");
        string file = write ? argv[3] : argv[2];
        string h = createBlob(file, write);
        if (!h.empty()) cout << h << "\n";
    }

    else {
        cerr << "Unknown command: " << cmd << "\n"; return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
