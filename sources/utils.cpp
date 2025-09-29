#include "utils.hpp"


std::string removeDotSegments(const std::string& in) {
	std::string	input = in;
	std::string output;
	while (!input.empty()) {
		if (input.compare(0, 3, "../") == 0)
			input.erase(0, 3);
		else if (input.compare(0, 2, "./") == 0)
			input.erase(0, 2);
		else if (input.compare(0, 3, "/./") == 0)
			input.replace(0, 3, "/");
		else if (input == "/.")
			input.replace(0, 2, "/");
		else if (input.compare(0, 4, "/../") == 0) {
			input.replace(0, 4, "/");
			size_t  pos = output.find_last_of('/');
			if (pos != std::string::npos)
				output.erase(pos);
			else
				output.clear();
		}
		else if (input == "/..") {
			input.replace(0, 3, "/");
			size_t  pos = output.find_last_of('/');
			if (pos != std::string::npos)
				output.erase(pos);
			else
				output.clear();
		}
		else if (input == "." || input == "..") {
			input.clear();
		}
		else {
			size_t  pos = input.find('/', 1);
			if (pos == std::string::npos)
				pos = input.size();
			output.append(input.substr(0, pos));
			input.erase(0, pos);
		}
	}
	return output;
}

static inline int fromHex(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

std::string percentDecode(const std::string& in, bool& ok) {
	std::string out;
	out.reserve(in.size());
	ok = true;
	for (size_t i = 0; i < in.size(); ++i) {
		if (in[i] == '%') {
			if (i + 2 >= in.size()) {
				ok = false;
				return "";
			}
			int hi = fromHex(in[i+1]);
			int lo = fromHex(in[i+2]);
			if (hi < 0 || lo < 0) {
				ok = false;
				return "";
			}
			out.push_back(static_cast<char>((hi << 4) | lo));
			i += 2;
		} else
			out.push_back(in[i]);
	}
	return out;
}

std::string compressSlashes(const std::string& in) {
	std::string out;
	out.reserve(in.size());
	bool        prevSlash = false;
	for (size_t i = 0; i < in.size(); ++i) {
		char c = in[i];
		if (c == '/') {
			if (!prevSlash) out.push_back('/');
			prevSlash = true;
		} else {
			out.push_back(c);
			prevSlash = false;
		}
	}
	return out;
}
