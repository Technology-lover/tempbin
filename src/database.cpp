#include "database.hpp"
#include <stdexcept>

static void check(int rc, sqlite3* db, const char* msg) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw std::runtime_error(std::string(msg) + ": " + sqlite3_errmsg(db));
}

Database::Database(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        throw std::runtime_error("Cannot open SQLite database");
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA synchronous=FULL;");
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA busy_timeout=5000;");
}
Database::~Database() { if (db_) sqlite3_close(db_); }

void Database::exec(const char* sql) {
    char* err=nullptr; int rc=sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc!=SQLITE_OK) { std::string e=err?err:"SQLite error"; sqlite3_free(err); throw std::runtime_error(e); }
}

void Database::initialize() {
    std::lock_guard lk(mutex_);
    exec(R"sql(
      CREATE TABLE IF NOT EXISTS files(
        id TEXT PRIMARY KEY,
        original_name TEXT NOT NULL,
        stored_name TEXT NOT NULL UNIQUE,
        size INTEGER NOT NULL,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_files_expires ON files(expires_at);
      CREATE TABLE IF NOT EXISTS pastes(
        id TEXT PRIMARY KEY,
        content TEXT NOT NULL,
        created_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL
      );
      CREATE INDEX IF NOT EXISTS idx_pastes_expires ON pastes(expires_at);
    )sql");
}

bool Database::insert_file(const FileRecord& r) {
    std::lock_guard lk(mutex_);
    sqlite3_stmt* st=nullptr;
    const char* q="INSERT INTO files(id,original_name,stored_name,size,created_at,expires_at) VALUES(?,?,?,?,?,?)";
    check(sqlite3_prepare_v2(db_,q,-1,&st,nullptr),db_,"prepare");
    sqlite3_bind_text(st,1,r.id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,2,r.original_name.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,3,r.stored_name.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(st,4,(sqlite3_int64)r.size);
    sqlite3_bind_int64(st,5,r.created_at); sqlite3_bind_int64(st,6,r.expires_at);
    int rc=sqlite3_step(st); sqlite3_finalize(st);
    return rc==SQLITE_DONE;
}

bool Database::insert_paste(const PasteRecord& r) {
    std::lock_guard lk(mutex_);
    sqlite3_stmt* st=nullptr;
    const char* q="INSERT INTO pastes(id,content,created_at,expires_at) VALUES(?,?,?,?)";
    check(sqlite3_prepare_v2(db_,q,-1,&st,nullptr),db_,"prepare");
    sqlite3_bind_text(st,1,r.id.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,2,r.content.c_str(),-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(st,3,r.created_at); sqlite3_bind_int64(st,4,r.expires_at);
    int rc=sqlite3_step(st); sqlite3_finalize(st); return rc==SQLITE_DONE;
}

bool Database::get_file_by_stored_name(const std::string& stored, FileRecord& o) {
    std::lock_guard lk(mutex_); sqlite3_stmt* st=nullptr;
    check(sqlite3_prepare_v2(db_,"SELECT id,original_name,stored_name,size,created_at,expires_at FROM files WHERE stored_name=?",-1,&st,nullptr),db_,"prepare");
    sqlite3_bind_text(st,1,stored.c_str(),-1,SQLITE_TRANSIENT);
    int rc=sqlite3_step(st);
    if(rc==SQLITE_ROW){
      o.id=(const char*)sqlite3_column_text(st,0); o.original_name=(const char*)sqlite3_column_text(st,1);
      o.stored_name=(const char*)sqlite3_column_text(st,2); o.size=(uint64_t)sqlite3_column_int64(st,3);
      o.created_at=sqlite3_column_int64(st,4); o.expires_at=sqlite3_column_int64(st,5);
    }
    sqlite3_finalize(st); return rc==SQLITE_ROW;
}

bool Database::get_paste(const std::string& id, PasteRecord& o) {
    std::lock_guard lk(mutex_); sqlite3_stmt* st=nullptr;
    check(sqlite3_prepare_v2(db_,"SELECT id,content,created_at,expires_at FROM pastes WHERE id=?",-1,&st,nullptr),db_,"prepare");
    sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(st);
    if(rc==SQLITE_ROW){ o.id=(const char*)sqlite3_column_text(st,0); o.content=(const char*)sqlite3_column_text(st,1); o.created_at=sqlite3_column_int64(st,2); o.expires_at=sqlite3_column_int64(st,3); }
    sqlite3_finalize(st); return rc==SQLITE_ROW;
}

static std::vector<FileRecord> file_rows(sqlite3* db, const char* q, int64_t now, unsigned limit) {
    std::vector<FileRecord> v; sqlite3_stmt* st=nullptr;
    check(sqlite3_prepare_v2(db,q,-1,&st,nullptr),db,"prepare");
    sqlite3_bind_int64(st,1,now); sqlite3_bind_int(st,2,(int)limit);
    while(sqlite3_step(st)==SQLITE_ROW){ FileRecord r; r.id=(const char*)sqlite3_column_text(st,0); r.original_name=(const char*)sqlite3_column_text(st,1); r.stored_name=(const char*)sqlite3_column_text(st,2); r.size=(uint64_t)sqlite3_column_int64(st,3); r.created_at=sqlite3_column_int64(st,4); r.expires_at=sqlite3_column_int64(st,5); v.push_back(std::move(r)); }
    sqlite3_finalize(st); return v;
}
std::vector<FileRecord> Database::list_files(int64_t n,unsigned l){std::lock_guard lk(mutex_);return file_rows(db_,"SELECT id,original_name,stored_name,size,created_at,expires_at FROM files WHERE expires_at>? ORDER BY created_at DESC LIMIT ?",n,l);}
std::vector<FileRecord> Database::expired_files(int64_t n){std::lock_guard lk(mutex_);return file_rows(db_,"SELECT id,original_name,stored_name,size,created_at,expires_at FROM files WHERE expires_at<=? ORDER BY expires_at LIMIT ?",n,1000);}

static std::vector<PasteRecord> paste_rows(sqlite3* db,const char* q,int64_t now,unsigned limit){
 std::vector<PasteRecord> v; sqlite3_stmt* st=nullptr; check(sqlite3_prepare_v2(db,q,-1,&st,nullptr),db,"prepare"); sqlite3_bind_int64(st,1,now); sqlite3_bind_int(st,2,(int)limit);
 while(sqlite3_step(st)==SQLITE_ROW){PasteRecord r;r.id=(const char*)sqlite3_column_text(st,0);r.content=(const char*)sqlite3_column_text(st,1);r.created_at=sqlite3_column_int64(st,2);r.expires_at=sqlite3_column_int64(st,3);v.push_back(std::move(r));} sqlite3_finalize(st);return v;
}
std::vector<PasteRecord> Database::list_pastes(int64_t n,unsigned l){std::lock_guard lk(mutex_);return paste_rows(db_,"SELECT id,content,created_at,expires_at FROM pastes WHERE expires_at>? ORDER BY created_at DESC LIMIT ?",n,l);}
std::vector<PasteRecord> Database::expired_pastes(int64_t n){std::lock_guard lk(mutex_);return paste_rows(db_,"SELECT id,content,created_at,expires_at FROM pastes WHERE expires_at<=? ORDER BY expires_at LIMIT ?",n,1000);}

bool Database::delete_file(const std::string& id){std::lock_guard lk(mutex_);sqlite3_stmt* st=nullptr;check(sqlite3_prepare_v2(db_,"DELETE FROM files WHERE id=?",-1,&st,nullptr),db_,"prepare");sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(st);sqlite3_finalize(st);return rc==SQLITE_DONE;}
bool Database::delete_paste(const std::string& id){std::lock_guard lk(mutex_);sqlite3_stmt* st=nullptr;check(sqlite3_prepare_v2(db_,"DELETE FROM pastes WHERE id=?",-1,&st,nullptr),db_,"prepare");sqlite3_bind_text(st,1,id.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(st);sqlite3_finalize(st);return rc==SQLITE_DONE;}
bool Database::file_exists_by_stored_name(const std::string& s){FileRecord r;return get_file_by_stored_name(s,r);}
bool Database::paste_exists(const std::string& id){PasteRecord r;return get_paste(id,r);}
