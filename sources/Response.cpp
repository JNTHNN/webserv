#include "Response.hpp"

std::string	getMimeType(const std::string& path) {
	size_t	dot = path.find_last_of('.');
	if (dot == std::string::npos)
		return "application/octet-stream";
	std::string ext = path.substr(dot + 1);
	if (ext == "html" || ext == "htm")
		return "text/html";
	if (ext == "css")
		return "text/css";
	if (ext == "js")
		return "application/javascript";
	if (ext == "png")
		return "image/png";
	if (ext == "jpg" || ext == "jpeg")
		return "image/jpeg";
	if (ext == "gif")
		return "image/gif";
	if (ext == "json")
		return "application/json";
	if (ext == "txt")
		return "text/plain";
	if (ext == "svg") 
		return "image/svg+xml";
	if (ext == "ico") 
		return "image/x-icon";
	if (ext == "pdf") 
		return "application/pdf";
	if (ext == "webp")
		return "image/webp";
	if (ext == "wasm")
		return "application/wasm";
	return "application/octet-stream";
}

static std::string	replaceAll(std::string subject, const std::string& search, const std::string& repl) {
	size_t	pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), repl);
		pos += repl.length();
	}
	return subject;
}

static std::string	urlEncodeSegment(const std::string& s) {
	static const char*	hex = "0123456789ABCDEF";
	std::string			out; out.reserve(s.size() * 3);
	for (size_t i = 0; i < s.size(); ++i) {
		unsigned char	c = static_cast<unsigned char>(s[i]);
		bool			safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
								(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
		if (safe)
			out.push_back(c);
		else {
			out.push_back('%');
			out.push_back(hex[(c >> 4) & 0xF]);
			out.push_back(hex[c & 0xF]);
		}
	}
	return out;
}

static std::string	htmlEscape(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		char c = s[i];
		if (c == '&')
			out += "&amp;";
		else if (c == '<')
			out += "&lt;";
		else if (c == '>')
			out += "&gt;";
		else if (c == '"')
			out += "&quot;";
		else if (c == '\'')
			out += "&#39;";
		else out += c;
	}
	return out;
}

void	Response::buildSimple(int code, const std::string& status, const std::string& body) {
	std::ostringstream	oss;
	oss << "HTTP/1.1 " << code << " " << status << "\r\n"
		<< "Content-Type: text/plain\r\n"
		<< "Content-Length: " << body.size() << "\r\n"
		<< "Connection: close\r\n"
		<< "\r\n"
		<< body;
	raw = oss.str();
}

void	Response::buildFileResponse(const std::string& path) {
	std::ifstream	file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file) {
		buildSimple(404, "Not Found", "404 Not Found");
		return;
	}
	std::ostringstream	ss;
	ss << file.rdbuf();
	std::string			body = ss.str();
	std::string			mime = getMimeType(path);

	std::ostringstream	header;
	header << "HTTP/1.1 200 OK\r\n"
		   << "Content-Type: " << mime << "\r\n"
		   << "Content-Length: " << body.size() << "\r\n"
		   << "Connection: close\r\n"
		   << "\r\n"
		   << body;
	raw = header.str();
}

void	Response::buildErrorResponse(int code, const std::string& status) {
	std::ostringstream	oss;
	oss << "HTTP/1.1 " << code << " " << status << "\r\n"
		<< "Content-Type: text/html\r\n";
	std::string	body = status + " ("; {
		std::ostringstream	c;
		c << code;
		body += c.str();
		body += ")";
	}
	oss << "Content-Length: " << body.size() << "\r\n"
		<< "Connection: close\r\n"
		<< "\r\n" << body;
	raw = oss.str();
}

void	Response::buildAutoindex(const char* dirPath, const std::string& requestUri) {
	DIR*	dir = opendir(dirPath);
	if (!dir) {
		buildErrorResponse(403, "Forbidden");
		return;
	}

	std::string	baseUri = requestUri;
	if (baseUri.empty() || baseUri[baseUri.length()-1] != '/')
		baseUri += "/";

	std::string		listItems;
	struct dirent*	entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string	name = entry->d_name;
		if (name == "." || name == "..")
			continue;
		std::string	text = htmlEscape(name);
		std::string	href = baseUri + urlEncodeSegment(name);
		listItems += "<li><a href=\"" + href + "\">" + text + "</a></li>\n";
	}
	closedir(dir);

	std::ifstream	file("www/autoindex.html");
	std::string		html;
	if (file) {
		std::ostringstream	buf;
		buf << file.rdbuf();
		html = buf.str();
		std::string	uri2 = baseUri;
		html = replaceAll(html, "{{URI}}", uri2);
		html = replaceAll(html, "{{LIST}}", listItems);
	} else {
		std::ostringstream	b;
		b << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
		  << "<title>Index of " << htmlEscape(baseUri) << "</title>"
		  << "<link rel=\"stylesheet\" href=\"/css/style.css\"/></head><body>"
		  << "<h1>Index of " << htmlEscape(baseUri) << "</h1><ul>\n"
		  << listItems << "</ul></body></html>";
		html = b.str();
	}

	std::ostringstream	header;
	header << "HTTP/1.1 200 OK\r\n"
		   << "Content-Type: text/html\r\n"
		   << "Content-Length: " << html.size() << "\r\n"
		   << "Connection: close\r\n"
		   << "\r\n"
		   << html;
	raw = header.str();
}

void	Response::build405(const std::string& allow) {
	std::string			body = "405 Method Not Allowed";
	std::ostringstream	header;
	header << "HTTP/1.1 405 Method Not Allowed\r\n"
		   << "Allow: " << allow << "\r\n"
		   << "Content-Type: text/plain\r\n"
		   << "Content-Length: " << body.size() << "\r\n"
		   << "Connection: close\r\n\r\n"
		   << body;
	raw = header.str();
}

void	Response::buildRedirect(int code, const std::string& location) {
	std::string			status = (code==301?"Moved Permanently": (code==302?"Found":"Redirect"));
	std::string			body = status + "\nLocation: " + location + "\n";
	std::ostringstream	header;
	header << "HTTP/1.1 " << code << " " << status << "\r\n"
		   << "Location: " << location << "\r\n"
		   << "Content-Type: " << "text/plain" << "\r\n"
		   << "Content-Length: " << body.size() << "\r\n"
		   << "Connection: close\r\n\r\n"
		   << body;
	raw = header.str();
}

void	Response::buildErrorResponseWithPages(int code, const std::string& status, const std::string& docRoot, const std::map<int,std::string>& errorPages) {
    std::string	body;
    std::string	pagePath;

    const char	SEP = '/';
    std::string	base = docRoot;
    if (!base.empty() && base[base.size()-1] == SEP)
		base.erase(base.size()-1);

    std::map<int,std::string>::const_iterator	it = errorPages.find(code);
    if (it != errorPages.end()) {
        const std::string&	cfg = it->second;

        std::string relJoin = base;
        if (!cfg.empty() && cfg[0] == SEP)
			relJoin += cfg;
        else {
			relJoin += SEP; relJoin += cfg;
		}

        std::ifstream f1(relJoin.c_str(), std::ios::in | std::ios::binary);
        if (f1) {
            std::ostringstream	ss;
			ss << f1.rdbuf();
            body = ss.str();
        } else {
            if (!cfg.empty() && cfg[0] == SEP) {
                std::ifstream f2(cfg.c_str(), std::ios::in | std::ios::binary);
                if (f2) {
                    std::ostringstream	ss;
					ss << f2.rdbuf();
                    body = ss.str();
                }
            }
        }
    }

    if (body.empty()) {
        std::ostringstream	ss;
        ss << status << " (" << code << ")";
        body = ss.str();
    }

    std::ostringstream	header;
    header << "HTTP/1.1 " << code << " " << status << "\r\n"
           << "Content-Type: text/html\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
    raw = header.str();
}

void	Response::buildCGIResponse(const std::string& cgiOutput) {
	size_t	headerEnd = cgiOutput.find("\r\n\r\n");
	if (headerEnd == std::string::npos) {
		headerEnd = cgiOutput.find("\n\n");
		if (headerEnd == std::string::npos) {
			std::ostringstream	header;
			header << "HTTP/1.1 200 OK\r\n"
				   << "Content-Type: text/html\r\n"
				   << "Content-Length: " << cgiOutput.size() << "\r\n"
				   << "Connection: close\r\n\r\n"
				   << cgiOutput;
			raw = header.str();
			return;
		}
	}
	
	std::string	cgiHeaders = cgiOutput.substr(0, headerEnd);
	std::string	content = cgiOutput.substr(headerEnd + (cgiOutput[headerEnd] == '\r' ? 4 : 2));
	std::string lowerHeaders = cgiHeaders;
	std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(), ::tolower);
	bool isChunked = (lowerHeaders.find("transfer-encoding: chunked") != std::string::npos);
	
	std::ostringstream	response;
	response << "HTTP/1.1 200 OK\r\n";
	
	if (isChunked) {
		std::string	normalizedHeaders = replaceAll(cgiHeaders, "\n", "\r\n");
		normalizedHeaders = replaceAll(normalizedHeaders, "\r\r\n", "\r\n");
		
		response << normalizedHeaders;
		if (!normalizedHeaders.empty() && normalizedHeaders.substr(normalizedHeaders.size()-2) != "\r\n") {
			response << "\r\n";
		}
		response << "Connection: close\r\n\r\n";
		response << content;
	} else {
		std::string	normalizedHeaders = replaceAll(cgiHeaders, "\n", "\r\n");
		normalizedHeaders = replaceAll(normalizedHeaders, "\r\r\n", "\r\n");
		response << normalizedHeaders;
		
		if (!normalizedHeaders.empty() && normalizedHeaders.substr(normalizedHeaders.size()-2) != "\r\n") {
			response << "\r\n";
		}
		response << "Connection: close\r\n\r\n";
		response << content;
	}
	
	raw = response.str();
}
