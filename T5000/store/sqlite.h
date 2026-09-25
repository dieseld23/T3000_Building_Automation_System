#pragma once

// A thin layer over the SQLite that ships with Windows (winsqlite3.dll).
//
// Windows' copy rather than one built into this project. It is in System32
// and SysWOW64 on every supported Windows, so the tool stays a single file
// with nothing to download, and its header and import library come with the
// Windows SDK. The copy in this repository (SQLiteDriver) is SQLite 3.4.0 from
// 2007, wrapped in a class that needs MFC, which this project does not link.
//
// Two rules for code on top of this, both because the DLL is the operating
// system's and not ours:
//
//   - Only long-standing entry points. The import library binds at load
//     time, so a function missing from an older Windows' DLL would stop
//     T5000.exe starting at all, not just fail one call. Everything used
//     here predates Windows 10.
//
//   - No upserts (ON CONFLICT ... DO UPDATE, SQLite 3.24). Read, then update
//     or insert, inside one transaction.
//
// Every value goes in through bind(). Nothing a device or an operator
// supplied is ever spliced into SQL text.

#include <stdint.h>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace t5000::store
{
    class Database
    {
    public:
        Database() = default;
        ~Database();
        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;

        // Opens the file, creating it if it does not exist. `path` is UTF-8,
        // or ":memory:" for a database that lives only as long as this
        // object, which is what the self-tests use.
        bool open(const std::string& path, std::string& error);
        void close();
        bool is_open() const { return m_db != nullptr; }

        // Runs SQL that takes no parameters and returns no rows.
        bool exec(const char* sql, std::string& error);

        // Rows changed by the last INSERT, UPDATE or DELETE.
        int changes() const;

        std::string last_error() const;
        sqlite3* handle() const { return m_db; }

    private:
        sqlite3* m_db = nullptr;
    };

    class Statement
    {
    public:
        Statement(Database& db, const char* sql);
        ~Statement();
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        // False when the SQL did not prepare or a bind failed; error() says
        // why. step() refuses to run a statement in that state.
        bool ok() const { return m_error.empty(); }
        const std::string& error() const { return m_error; }

        // Parameters are numbered from 1, as SQLite numbers them.
        void bind(int index, int64_t value);
        void bind(int index, const std::string& utf8);

        enum class Step
        {
            Row,    // a row is ready to be read with column_*
            Done,   // finished
            Error,  // error() says why
        };
        Step step();

        // Ready to run again with new bindings.
        void reset();

        // Columns are numbered from 0, as SQLite numbers them.
        int64_t     column_int(int index) const;
        std::string column_text(int index) const;

    private:
        void fail(const char* what);

        Database&     m_db;
        sqlite3_stmt* m_stmt = nullptr;
        std::string   m_error;
    };

    // BEGIN IMMEDIATE when made, ROLLBACK when destroyed unless commit() was
    // called. So a function that returns early on an error leaves the file as
    // it was, without having to remember to.
    class Transaction
    {
    public:
        explicit Transaction(Database& db);
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        bool began() const { return m_began; }
        const std::string& error() const { return m_error; }
        bool commit();

    private:
        Database&   m_db;
        bool        m_began = false;
        bool        m_done  = false;
        std::string m_error;
    };
}
