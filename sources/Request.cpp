#include "Request.hpp"

static std::string strip_right(const std::string& s) {
	size_t	j = s.size();
	while (j && (s[j - 1] == '\r' || s[j - 1] == '\n' || s[j - 1] == ' '  || s[j - 1] == '\t'))
		--j;
	return s.substr(0, j);
}

static std::string strip_left(const std::string& s) {
	size_t	i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
		++i;
	return s.substr(i);
}

static std::string trim(const std::string& s) {
	return strip_right(strip_left(s));
}

static std::string title_case_header(const std::string& k) {
	std::string	out;
	out.reserve(k.size());
	bool		newWord = true;
	for (size_t i = 0; i < k.size(); ++i) {
		char	c = k[i];
		if (c == '-') {
			out += '-';
			newWord = true;
		} else {
			if (newWord) {
				if (c >= 'a' && c <= 'z')
					c = static_cast<char>(c - 'a' + 'A');
				newWord = false;
			} else {
				if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c - 'A' + 'a');
			}
			out += c;
		}
	}
	return out;
}

static inline int hexVal(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static bool percentDecodePath(const std::string& in, std::string& out) {
	out.clear();
	out.reserve(in.size());

	for (size_t i = 0; i < in.size(); ++i) {
		char	c = in[i];
		if (c == '%') {
			if (i + 2 >= in.size())
				return false;
			int	hi = hexVal(in[i + 1]);
			int	lo = hexVal(in[i + 2]);
			if (hi < 0 || lo < 0)
				return false;
			out.push_back(static_cast<char>((hi << 4) | lo));
			i += 2;
		} else
			out.push_back(c);
	}
	return true;
}

Request::Request() : method(""), uri(""), rawUri(""), originalUri(""), version(""), headersParsed(false)
{
}

bool Request::parse(const std::string &raw) {
	method.clear();
	uri.clear();
	version.clear();
	rawUri.clear();
	originalUri.clear();
	headers.clear();
	body.clear();
	headersParsed = false;
	std::istringstream	stream(raw);
	std::string			line;
	if (!std::getline(stream, line))
		return false;
	line = strip_right(line);
	std::istringstream	first(line);
	first >> method >> uri >> version;
	if (method.empty() || uri.empty() || version.empty())
		return false;
	
	originalUri = uri;
	rawUri = uri;
	
	std::string	pathOnly = uri;
	size_t		hashPos = pathOnly.find('#');
	if (hashPos != std::string::npos)
		pathOnly.erase(hashPos);

	size_t qPos = pathOnly.find('?');
	if (qPos != std::string::npos)
		pathOnly.erase(qPos);

	std::string	decodedPath;
	if (!percentDecodePath(pathOnly, decodedPath))
		return false;
	if (decodedPath.empty())
		decodedPath = "/";
	if (decodedPath[0] != '/')
		decodedPath.insert(decodedPath.begin(), '/');
	uri = decodedPath;
	while (std::getline(stream, line)) {
		std::string	trimmedRight = strip_right(line);
		if (trimmedRight.empty())
			break;
		size_t	colon = trimmedRight.find(':');
		if (colon == std::string::npos)
			continue;
		std::string	key   = title_case_header(trim(trimmedRight.substr(0, colon)));
		std::string	value = trim(trimmedRight.substr(colon + 1));
		headers[key] = value;
	}
	std::string::size_type	bodyStart = raw.find("\r\n\r\n");
	if (bodyStart != std::string::npos) {
		bodyStart += 4;
		if (bodyStart < raw.length())
			body = raw.substr(bodyStart);
	}
	headersParsed = true;
	return true;
}

bool Request::parseChunkedBody(const std::string& rawData, size_t headerEnd) {
	std::map<std::string, std::string>::iterator it = headers.find("Transfer-Encoding");
	if (it == headers.end() || it->second != "chunked")
		return false;
	
	body.clear();
	size_t	pos = headerEnd + 4;
	
	if (pos > rawData.size() || (pos < rawData.size() && rawData.substr(headerEnd, 4) == "\n\n")) {
		pos = headerEnd + 2;
	}
	
	while (pos < rawData.size()) {
		size_t	crlfPos = rawData.find("\r\n", pos);
		size_t	lfPos = rawData.find("\n", pos);
		bool usesCRLF = true;
		
		if (crlfPos == std::string::npos && lfPos != std::string::npos) {
			crlfPos = lfPos;
			usesCRLF = false;
		} else if (lfPos != std::string::npos && lfPos < crlfPos) {
			crlfPos = lfPos;
			usesCRLF = false;
		}
		
		if (crlfPos == std::string::npos)
			return false;
		
		std::string	sizeStr = rawData.substr(pos, crlfPos - pos);	
		size_t		semicolon = sizeStr.find(';');
		if (semicolon != std::string::npos) {
			sizeStr = sizeStr.substr(0, semicolon);
		}
		sizeStr = trim(sizeStr);
		
		char* endPtr;
		size_t chunkSize = std::strtoul(sizeStr.c_str(), &endPtr, 16);
		if (*endPtr != '\0') {
			return false;
		}
		
		if (chunkSize == 0) {
			pos = crlfPos + (usesCRLF ? 2 : 1);
			break;
		}
		
		pos = crlfPos + (usesCRLF ? 2 : 1);
		int	lineEndingSize = usesCRLF ? 2 : 1;
		if (pos + chunkSize + lineEndingSize > rawData.size())
			return false;
		
		body.append(rawData.substr(pos, chunkSize));
		pos += chunkSize + (usesCRLF ? 2 : 1);
	}
	
	return true;
}
