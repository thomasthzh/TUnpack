#pragma once
#include "input.hpp"
namespace lu {
// A format verifier only rejects candidates. Full CRC/MAC validation remains mandatory.
struct PasswordCandidate {std::wstring value,engineValue;bool usable=true;};
std::vector<PasswordCandidate> zipPasswordCandidates(const Input& input,const std::vector<std::wstring>& candidates,const Progress& progress);
}
