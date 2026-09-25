#include "sqlite.h"

// winsqlite3.h picks its calling convention from NTDDI_VERSION, so the SDK's
// version macros have to be defined before it is included.
#include <windows.h>
#include <winsqlite/winsqlite3.h>

namespace t5000::store
{
    Database::~Database()
    {
        close();
    }

    bool Database::open(const std::string& path, std::string& error)
    {
        close();

        sqlite3* db = nullptr;
        const int rc = sqlite3_open_v2(path.c_str(), &db,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK)
        {
            // A failed open can still hand back a connection, holding the
            // message, and it has to be closed either way.
            error = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
            sqlite3_close(db);
            return false;
        }

        m_db = db;

        // Another process holding a write lock - an older T5000 still
        // shutting down, a virus scanner - is waited out briefly rather than
        // reported as a failure on the first try.
        sqlite3_busy_timeout(m_db, 2000);
        return true;
    }

    void Database::close()
    {
        if (m_db)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    bool Database::exec(const char* sql, std::string& error)
    {
        if (!m_db)
        {
            error = "the database is not open";
            return false;
        }

        char* message = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &message) != SQLITE_OK)
        {
            error = message ? message : last_error();
            sqlite3_free(message);
            return false;
        }
        return true;
    }

    int Database::changes() const
    {
        return m_db ? sqlite3_changes(m_db) : 0;
    }

    std::string Database::last_error() const
    {
        return m_db ? sqlite3_errmsg(m_db) : "the database is not open";
    }

    Statement::Statement(Database& db, const char* sql)
        : m_db(db)
    {
        if (!db.is_open())
        {
            m_error = "the database is not open";
            return;
        }
        if (sqlite3_prepare_v2(db.handle(), sql, -1, &m_stmt, nullptr) != SQLITE_OK)
            fail("prepare");
    }

    Statement::~Statement()
    {
        sqlite3_finalize(m_stmt);
    }

    void Statement::fail(const char* what)
    {
        // The first failure is the one worth reporting; anything after it is
        // usually a consequence.
        if (m_error.empty())
            m_error = std::string(what) + ": " + m_db.last_error();
    }

    void Statement::bind(int index, int64_t value)
    {
        if (m_stmt && sqlite3_bind_int64(m_stmt, index, value) != SQLITE_OK)
            fail("bind");
    }

    void Statement::bind(int index, const std::string& utf8)
    {
        // SQLITE_TRANSIENT: SQLite copies the text now, so the string may go
        // out of scope before step().
        if (m_stmt && sqlite3_bind_text(m_stmt, index, utf8.c_str(), (int)utf8.size(),
                                        SQLITE_TRANSIENT) != SQLITE_OK)
            fail("bind");
    }

    Statement::Step Statement::step()
    {
        if (!ok() || !m_stmt)
            return Step::Error;

        const int rc = sqlite3_step(m_stmt);
        if (rc == SQLITE_ROW)
            return Step::Row;
        if (rc == SQLITE_DONE)
            return Step::Done;

        fail("step");
        return Step::Error;
    }

    void Statement::reset()
    {
        if (!m_stmt)
            return;
        sqlite3_reset(m_stmt);
        sqlite3_clear_bindings(m_stmt);
    }

    int64_t Statement::column_int(int index) const
    {
        return m_stmt ? sqlite3_column_int64(m_stmt, index) : 0;
    }

    std::string Statement::column_text(int index) const
    {
        if (!m_stmt)
            return std::string();

        // The text pointer first, then its length: asking for the length
        // first can make SQLite convert the value, invalidating the order.
        const unsigned char* text = sqlite3_column_text(m_stmt, index);
        const int n = sqlite3_column_bytes(m_stmt, index);
        return text ? std::string((const char*)text, (size_t)n) : std::string();
    }

    Transaction::Transaction(Database& db)
        : m_db(db)
    {
        // IMMEDIATE takes the write lock now, so a transaction that is going
        // to write cannot get halfway and then find the file busy.
        m_began = db.exec("BEGIN IMMEDIATE", m_error);
    }

    Transaction::~Transaction()
    {
        if (m_began && !m_done)
        {
            std::string ignored;
            m_db.exec("ROLLBACK", ignored);
        }
    }

    bool Transaction::commit()
    {
        if (!m_began || m_done)
            return false;

        m_done = true;
        if (!m_db.exec("COMMIT", m_error))
        {
            std::string ignored;
            m_db.exec("ROLLBACK", ignored);
            return false;
        }
        return true;
    }
}
