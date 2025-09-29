#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "ServerConfig.hpp"
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>


class ConfigParser {
	public:
		ConfigParser(const std::string& filepath);
		~ConfigParser();

		std::vector<ServerConfig>	parseAll();

	private:
		std::vector<std::string>	_lines;
		size_t						_cur;

		void						tokenize(const std::string& filepath);
		std::string					trim(const std::string& s);
		std::string					nextLine();
		bool						startsWith(const std::string& s, const std::string& pfx);

		ServerConfig				parseServer();
		LocationConfig				parseLocation(const std::string& path);

		size_t						expectChar(const std::string& s, size_t i, char c);
};

#endif
