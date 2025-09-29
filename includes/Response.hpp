#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <sstream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <map>
#include <algorithm>



class Response {
	public:
		std::string	raw;

		void	buildSimple(int code, const std::string& status, const std::string& message);
		void	buildFileResponse(const std::string& filepath);
		void	buildErrorResponse(int code, const std::string& status);
		void	buildAutoindex(const char* dirPath, const std::string& requestUri);

		void	build405(const std::string& allow);
		void	buildRedirect(int code, const std::string& location);
		void	buildErrorResponseWithPages(int code, const std::string& status, const std::string& docRoot, const std::map<int,std::string>& errorPages);
		void	buildCGIResponse(const std::string& cgiOutput);
};

std::string	getMimeType(const std::string& path);

#endif
