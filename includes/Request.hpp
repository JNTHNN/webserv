#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <map>
#include <sstream>
#include <algorithm>


class Request {
	public:
		std::string							method;
		std::string							uri;
		std::string							rawUri;
		std::string							originalUri;
		std::string							version;
		std::map<std::string, std::string>	headers;
		std::string							body;
		bool								headersParsed;

		Request();
		bool	parse(const std::string &raw);
		bool	parseChunkedBody(const std::string& rawData, size_t headerEnd);
};

#endif
