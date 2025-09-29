#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "Request.hpp"
#include "Response.hpp"
#include "ServerConfig.hpp"
#include "utils.hpp"
#include "CGI.hpp"
#include <ctime>
#include <cstring>
#include <vector>

class Client {
	public:
		int							fd;
		std::string					readBuffer;
		std::string					writeBuffer;
		std::time_t					lastActivity;
		bool						headersParsed;

		Request						request;
		Response					response;

		size_t						serverIndex;
		std::string					docRoot;
		std::string					indexFile;
		bool						autoindexEnabled;
		std::set<std::string>		allowedMethods;
		std::map<int,std::string>	errorPages;
		size_t      				maxBodySize;

		bool						uploadEnabled;
		std::string					uploadStore;
		std::string					locationPrefix;

		int							redirectCode;
		std::string					redirectURL;

		bool						cgiEnable;
		std::string					cgiPath;
		std::string					cgiExt;

		Client();
		Client(int fd);

		void 		handleRequest(const ServerConfig& serverConfig);
		void 		reset();

	private:
		void 		handleGet(const char* resolvedPath, const ServerConfig& serverConfig);
		void 		handlePost(const ServerConfig& serverConfig);
		void 		handleDelete(const char* resolvedPath);

		bool        isMultipart(std::string& boundaryOut) const;
		std::string sanitizeFilename(const std::string& name);
		bool        saveMultipartFiles(const std::string& boundary, std::vector<std::string>& savedFiles);
};

#endif
