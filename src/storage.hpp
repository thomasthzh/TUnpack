#pragma once
#include "input.hpp"

namespace lu::storage {
void put64(std::string& out,uint64_t value);
uint64_t get64(const std::string& in,size_t& position);
void putString(std::string& out,const std::wstring& value);
std::wstring getString(const std::string& in,size_t& position);
fs::path dataPath(const wchar_t* name);
void atomicWrite(const fs::path& file,const std::string& data);
}
