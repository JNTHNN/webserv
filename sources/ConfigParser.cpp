#include "ConfigParser.hpp"

ConfigParser::ConfigParser(const std::string& filepath) : _cur(0) {
	tokenize(filepath);
}

ConfigParser::~ConfigParser() {}

std::string ConfigParser::trim(const std::string& s) {
	size_t	i = 0, j = s.size();
	while (i < j && (s[i] == ' ' || s[i] == '\t'))
		++i;
	while (j && (s[j-1] == ' ' || s[j-1] == '\t' || s[j-1] == '\r' || s[j-1] == '\n'))
		--j;
	return (i < j) ? s.substr(i, j-i) : std::string();
}
bool ConfigParser::startsWith(const std::string& s, const std::string& pfx) {
	return s.size() >= pfx.size() && s.compare(0, pfx.size(), pfx) == 0;
}
void ConfigParser::tokenize(const std::string& filepath) {
	std::ifstream   in(filepath.c_str());
	if (!in)
		throw std::runtime_error("Cannot open config file: " + filepath);
	std::string	line;
	while (std::getline(in, line)) {
		std::string t = trim(line);
		if (!t.empty() && t[0] != '#')
			_lines.push_back(t);
	}
}
std::string ConfigParser::nextLine() {
	if (_cur >= _lines.size())
		throw std::runtime_error("Unexpected end of config");
	return _lines[_cur++];
}
size_t ConfigParser::expectChar(const std::string& s, size_t i, char c) {
	if (i >= s.size() || s[i] != c)
		throw std::runtime_error(std::string("Expected '") + c + "' in: " + s);
	return i+1;
}

std::vector<ServerConfig> ConfigParser::parseAll() {
	std::vector<ServerConfig>	all;
	while (_cur < _lines.size()) {
		std::string	l = nextLine();
		if (l == "server {")
			all.push_back(parseServer());
		else if (l == "server") {
			if (_cur >= _lines.size())
				throw std::runtime_error("Expected '{' after 'server' but reached end of file");
			std::string brace = nextLine();
			if (brace != "{")
				throw std::runtime_error("Expected '{' after 'server', got: " + brace);
			all.push_back(parseServer());
		} else
			throw std::runtime_error("Unknown top-level directive: " + l); }
	if (all.empty())
		throw std::runtime_error("No server block found");
	return all;
}

static int parseSize(const std::string& s) {
	if (s.empty())
		return 0;
	long		mult = 1;
	char		suf = s[s.size()-1];
	std::string	num = s;
	if (suf == 'K' || suf == 'k') {
		mult = 1024;
		num = s.substr(0, s.size()-1);
	} else if (suf =='M' || suf == 'm') {
		mult = 1024*1024;
		num = s.substr(0, s.size()-1);
	} else if (suf == 'G' || suf == 'g') {
		mult = 1024*1024*1024L;
		num = s.substr(0, s.size()-1); }
	std::istringstream	is(num);
	long				v = 0; is >> v;
	if (v < 0)
		v = 0;
	long	res = v * mult;
	if (res < 0)
		res = 0;
	return (int)res;
}

static void	removeSC(std::string& val)
{
	if (!val.empty() && val[val.size()-1] != ';')
		throw std::runtime_error("Missing ';' at this directive's value: " + val);
	if (!val.empty() && val[val.size()-1] == ';')
		val.erase(val.size()-1);
}

ServerConfig ConfigParser::parseServer() {
	ServerConfig	sc;
	bool			closed = false;
	while (_cur < _lines.size()) {
		std::string	l = nextLine();
		if (l == "}")
		{
			closed = true;
			break;
		}
		if (startsWith(l, "listen ")) {
			std::string	val = trim(l.substr(7));
			removeSC(val);
			size_t	c = val.find(':');
			if (c == std::string::npos)
				throw std::runtime_error("listen needs host:port");
			sc.host = val.substr(0, c);
			sc.port = std::atoi(val.substr(c+1).c_str());
			if (sc.port < 1 || sc.port > 65535)
				throw std::runtime_error("Invalid port");
		} else if (startsWith(l, "server_name ")) {
			std::string val = trim(l.substr(12));
			removeSC(val);
			sc.server_name = val;
		} else if (startsWith(l, "client_max_body_size ")) {
			std::string val = trim(l.substr(21));
			removeSC(val);
			sc.max_body_size = (size_t)parseSize(val);
		} else if (startsWith(l, "error_page ")) {
			std::istringstream	is(l.substr(11));
			int					code;
			std::string 		path;
			is >> code >> path;
			removeSC(path);
			sc.error_pages[code] = path;
		} else if (startsWith(l, "location ")) {
			size_t		sp = l.find(' ');
			size_t		br = l.find('{');
			std::string	locPath = trim(l.substr(sp, (br==std::string::npos ? l.size() : br) - sp));
			if (!locPath.empty() && locPath[0]==' ')
				locPath.erase(0,1);
			if (br == std::string::npos) {
				if (_cur >= _lines.size())
					throw std::runtime_error("Expected '{' after location " + locPath);
				std::string	brace = nextLine();
				if (brace != "{")
					throw std::runtime_error("Expected '{' after location " + locPath + ", got: " + brace);
			}
			LocationConfig lc = parseLocation(locPath);
			sc.locations[locPath] = lc;
		} else if (l == "{")
			throw std::runtime_error("Unexpected '{' inside server block");
		else
			throw std::runtime_error("Unknown directive: " + l);
	}
	if (!closed)
		throw std::runtime_error("Missing '}' at end of server block");
	return sc;
}

LocationConfig ConfigParser::parseLocation(const std::string& path) {
	LocationConfig	lc;
	bool			closed = false;
	lc.path = path;

	while (_cur < _lines.size()) {
		std::string	l = nextLine();
		if (l == "}")
		{
			closed = true;
			break;
		}
		if (startsWith(l, "root ")) {
			std::string	val = trim(l.substr(5));
			removeSC(val);
			lc.root = val;
		} else if (startsWith(l, "index ")) {
			std::string	val = trim(l.substr(6));
			removeSC(val);
			lc.index = val;
		} else if (startsWith(l, "autoindex ")) {
			std::string	val = trim(l.substr(10));
			removeSC(val);
			lc.autoindex = (val == "on");
		} else if (startsWith(l, "methods ")) {
			std::string	val = trim(l.substr(8));
			removeSC(val);
			std::istringstream	is(val);
			std::string			m;
			while (is >> m) {
				for (size_t i = 0; i < m.size(); ++i)
					m[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(m[i])));
				lc.methods.insert(m);
			}
		} else if (startsWith(l, "upload_enable ")) {
			std::string	val = trim(l.substr(14));
			removeSC(val);
			lc.upload_enable = (val == "on");
		} else if (startsWith(l, "upload_store ")) {
			std::string	val = trim(l.substr(13));
			removeSC(val);
			lc.upload_store = val;
		} else if (startsWith(l, "cgi_enable ")) {
			std::string	val = trim(l.substr(11));
			removeSC(val);
			lc.cgi_enable = (val == "on");
		} else if (startsWith(l, "cgi_path ")) {
			std::string	val = trim(l.substr(9));
			removeSC(val);
			lc.cgi_path = val;
		} else if (startsWith(l, "cgi_extension ")) {
			std::string	val = trim(l.substr(14));
			removeSC(val);
			lc.cgi_extension = val;
		} else if (startsWith(l, "cgi_ext ")) {
			std::string	val = trim(l.substr(8));
			removeSC(val);
			lc.cgi_extension = val;
		} else if (startsWith(l, "return ")) {
			std::istringstream	is(l.substr(7));
			int					code;
			std::string			url;
			is >> code >> url;
			removeSC(url);
			lc.redirect_code = code;
			lc.redirect_url  = url;
		} else if (startsWith(l, "client_max_body_size ")) {
			std::string val = trim(l.substr(21));
			removeSC(val);
			lc.max_body_size = (size_t)parseSize(val);
			lc.has_max_body_size = true;
		} else if (l == "{")
			throw std::runtime_error("Unexpected '{' inside location block for: " + path);
		else
			throw std::runtime_error("Unknown location directive: " + l);
	}
	if (!closed)
		throw std::runtime_error("Missing '}' at end of location block for: " + path);
	return lc;
}
