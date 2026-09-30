#pragma once

// A file in %TEMP% for a test that needs a real one, deleted when the object
// goes out of scope.
//
// Most database tests run in memory. The ones about the FILE - that a list
// survives being closed and reopened, and that a file this build should not
// touch is left alone - need a real one, and it must not land beside the exe,
// where the post-build self-test runs.
//
// The name has an accented letter in it on purpose, so every test that uses
// one also checks that a path reaches SQLite as UTF-8 rather than in the ANSI
// code page.

#include <windows.h>
#include <stdio.h>

#include <string>

namespace t5000::testing
{
    class TempFile
    {
    public:
        explicit TempFile(const wchar_t* tag)
        {
            static int counter = 0;

            wchar_t dir[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, dir);

            wchar_t name[128] = {};
            swprintf_s(name, L"T5000-selftest-\u00e9-%lu-%d-%s.db",
                       (unsigned long)GetCurrentProcessId(), ++counter, tag);

            m_wide = std::wstring(dir) + name;
            DeleteFileW(m_wide.c_str());

            const int n = WideCharToMultiByte(CP_UTF8, 0, m_wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n > 1)
            {
                m_utf8.assign((size_t)n, '\0');
                WideCharToMultiByte(CP_UTF8, 0, m_wide.c_str(), -1, &m_utf8[0], n, nullptr, nullptr);
                m_utf8.resize((size_t)n - 1);
            }
        }

        ~TempFile()
        {
            DeleteFileW(m_wide.c_str());
            DeleteFileW((m_wide + L"-journal").c_str());
        }

        TempFile(const TempFile&) = delete;
        TempFile& operator=(const TempFile&) = delete;

        const std::string&  utf8() const { return m_utf8; }
        const std::wstring& wide() const { return m_wide; }

        bool exists() const { return GetFileAttributesW(m_wide.c_str()) != INVALID_FILE_ATTRIBUTES; }

        bool write(const std::string& bytes) const
        {
            FILE* f = nullptr;
            if (_wfopen_s(&f, m_wide.c_str(), L"wb") != 0 || !f)
                return false;
            const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
            fclose(f);
            return ok;
        }

        std::string read() const
        {
            std::string out;
            FILE* f = nullptr;
            if (_wfopen_s(&f, m_wide.c_str(), L"rb") != 0 || !f)
                return out;
            char buf[4096];
            size_t n = 0;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                out.append(buf, n);
            fclose(f);
            return out;
        }

    private:
        std::wstring m_wide;
        std::string  m_utf8;
    };
}
