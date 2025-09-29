#ifndef SERVERCONFIG_HPP
#define SERVERCONFIG_HPP

#include <string>
#include <map>
#include <set>

struct LocationConfig {
	std::string								path;
	std::string								root;
	std::string								index;
	std::set<std::string>					methods;
	bool									autoindex;
	bool									upload_enable;
	std::string								upload_store;

	bool									cgi_enable;
	std::string								cgi_path;
	std::string								cgi_extension;

	int										redirect_code;
	std::string								redirect_url;

	size_t									max_body_size;
	bool									has_max_body_size;

	LocationConfig(): autoindex(false), upload_enable(false), cgi_enable(false), redirect_code(0), max_body_size(0), has_max_body_size(false) {}
};

struct ServerConfig {
	std::string								host;
	int										port;
	std::string								server_name;
	std::map<int, std::string>				error_pages;
	std::map<std::string, LocationConfig>	locations;
	size_t									max_body_size;

	ServerConfig(): host("127.0.0.1"), port(8080), max_body_size(1048576) {}
};

#endif
