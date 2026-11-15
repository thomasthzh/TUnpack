#pragma once
#include "input.hpp"
#include <mutex>

namespace lu {
struct Password {
    std::wstring value;
    uint64_t uses=0, last=0;
    std::wstring directory;
};
struct PasswordImport {size_t added=0,duplicates=0;};
struct PasswordImportFailure {fs::path file;std::wstring message;};
struct PasswordImportBatch {PasswordImport totals;size_t files=0;std::vector<PasswordImportFailure> failures;};
class PasswordBook {
    fs::path file_;
    std::vector<Password> entries_;
    mutable std::mutex mutex_;
    void saveLocked();
public:
    explicit PasswordBook(fs::path file={});
    size_t count() const;
    PasswordImport add(const std::vector<std::wstring>& values);
    PasswordImport preview(const std::vector<std::wstring>& values) const;
    std::vector<Password> snapshot() const;
    void success(const std::wstring& value,const fs::path& file);
    std::vector<std::wstring> ranked(const fs::path& file,const std::wstring& preferred={}) const;
    PasswordImport importText(const fs::path& file);
    PasswordImportBatch importTexts(const std::vector<fs::path>& files);
};
std::vector<std::wstring> splitLines(const std::wstring& text);
}
