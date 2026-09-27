#pragma once
#include <string>

// SHA-256 as lowercase hex, for committing to a plan before revealing it
std::string Sha256Hex(const std::string &data);
