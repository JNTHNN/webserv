#ifndef UTILS_HPP
#define UTILS_HPP

#include <iostream>

std::string	removeDotSegments(const std::string& in);
std::string	percentDecode(const std::string& in, bool& ok);
std::string	compressSlashes(const std::string& in);

#endif
