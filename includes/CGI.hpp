#ifndef CGI_HPP
#define CGI_HPP

#include "Request.hpp"
#include "ServerConfig.hpp"
#include <errno.h>
#include <sys/wait.h>
#include <iostream>
#include <cstring>

class CGI {
	public:
		static bool	execute(const std::string& scriptPath, const std::string& cgiPath,
							const Request& request,
							const ServerConfig& serverConfig,
							std::string& output);

	private:
		static std::map<std::string, std::string>	buildEnvironment(const Request& request, 
																		const std::string& scriptPath,
																		const ServerConfig& serverConfig);
		
		static char**								buildEnvArray(const std::map<std::string, std::string>& env);
		static void									freeEnvArray(char** envp);
		static void									setupEnvironment(const std::map<std::string, std::string>& env);
		static std::string							readFromPipe(int fd);
		static std::string							getDirectoryFromPath(const std::string& path);
		static std::string							getFilenameFromPath(const std::string& path);
};

#endif