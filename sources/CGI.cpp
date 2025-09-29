#include "CGI.hpp"

bool	CGI::execute(const std::string& scriptPath, const std::string& cgiPath, const Request& request, const ServerConfig& serverConfig, std::string& output) {
	
	if (access(cgiPath.c_str(), X_OK) != 0) {
		std::cerr << "CGI interpreter not found or not executable: " << cgiPath << " - " << strerror(errno) << std::endl;
		return false;
	}
	
	if (access(scriptPath.c_str(), R_OK) != 0) {
		std::cerr << "CGI script not found or not readable: " << scriptPath << " - " << strerror(errno) << std::endl;
		return false;
	}
	
	std::map<std::string, std::string> env = buildEnvironment(request, scriptPath, serverConfig);
	
	int pipeOut[2];
	int pipeIn[2];
	
	if (pipe(pipeOut) == -1 || pipe(pipeIn) == -1) {
		return false;
	}
	
	pid_t	pid = fork();
	if (pid == -1) {
		close(pipeOut[0]); close(pipeOut[1]);
		close(pipeIn[0]); close(pipeIn[1]);
		return false;
	}
	
	if (pid == 0) {
		dup2(pipeOut[1], STDOUT_FILENO);
		close(pipeOut[0]);
		close(pipeOut[1]);
		
		dup2(pipeIn[0], STDIN_FILENO);
		close(pipeIn[0]);
		close(pipeIn[1]);
		
		std::string	scriptDir = getDirectoryFromPath(scriptPath);
		if (!scriptDir.empty() && scriptDir != ".") {
			if (chdir(scriptDir.c_str()) != 0) {
				std::cerr << "CGI chdir failed: " << scriptDir << " - " << strerror(errno) << std::endl;
				exit(1);
			}
		}
		
		std::string scriptName = getFilenameFromPath(scriptPath);
		char*	argv[3];
		argv[0] = const_cast<char*>(cgiPath.c_str());
		argv[1] = const_cast<char*>(scriptName.c_str());
		argv[2] = NULL;
		
		char**	envp = buildEnvArray(env);
		
		execve(cgiPath.c_str(), argv, envp);
		freeEnvArray(envp);
		std::cerr << "CGI exec failed: " << strerror(errno) << std::endl;
		exit(1);
	}
	
	close(pipeOut[1]);
	close(pipeIn[0]);
	
	if (request.method == "POST" && !request.body.empty()) {
		write(pipeIn[1], request.body.c_str(), request.body.size());
	}
	close(pipeIn[1]);
	
	int		status;
	pid_t	result;
	int		timeout = 30;
	
	for (int i = 0; i < timeout * 10; i++) {
		result = waitpid(pid, &status, WNOHANG);
		if (result == pid) {
			output = readFromPipe(pipeOut[0]);
			close(pipeOut[0]);
			return WIFEXITED(status) && WEXITSTATUS(status) == 0;
		} else if (result == -1) {
			close(pipeOut[0]);
			return false;
		}
		usleep(100000);
	}
	
	std::cerr << "CGI timeout reached, killing process " << pid << std::endl;
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	close(pipeOut[0]);
	return false;
}

std::map<std::string, std::string>	CGI::buildEnvironment(const Request& request, const std::string& scriptPath, const ServerConfig& serverConfig) {
	std::map<std::string, std::string>	env;
	
	env["REQUEST_METHOD"] = request.method;
	std::string	scriptName;
	std::string	pathInfo = "";

	size_t		qpos = request.originalUri.find('?');
	std::string	uriWithoutQuery = (qpos != std::string::npos) ? request.originalUri.substr(0, qpos) : request.originalUri;
	std::string	scriptExt = getFilenameFromPath(scriptPath);
	size_t		extPos = scriptExt.find_last_of('.');
	if (extPos != std::string::npos) {
		std::string	extension = scriptExt.substr(extPos);
		size_t		scriptEndPos = uriWithoutQuery.find(extension);
		if (scriptEndPos != std::string::npos) {
			scriptEndPos += extension.length();
			scriptName = uriWithoutQuery.substr(0, scriptEndPos);
			if (scriptEndPos < uriWithoutQuery.length()) {
				pathInfo = uriWithoutQuery.substr(scriptEndPos);
			}
		} else {
			scriptName = uriWithoutQuery;
		}
	} else {
		scriptName = uriWithoutQuery;
	}
	
	env["SCRIPT_NAME"] = scriptName;
	env["PATH_INFO"] = pathInfo;
	env["DOCUMENT_ROOT"] = "./www";
	env["SERVER_NAME"] = serverConfig.server_name.empty() ? serverConfig.host : serverConfig.server_name;
	{
		std::ostringstream	oss;
		oss << serverConfig.port;
		env["SERVER_PORT"] = oss.str();
	}
	env["SERVER_PROTOCOL"] = "HTTP/1.1";
	env["GATEWAY_INTERFACE"] = "CGI/1.1";
	env["QUERY_STRING"] = (qpos != std::string::npos) ? request.originalUri.substr(qpos + 1) : "";
	env["REQUEST_URI"] = request.originalUri;
	env["REMOTE_ADDR"] = "127.0.0.1";
	env["REMOTE_HOST"] = "localhost";
	
	for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
		 it != request.headers.end(); ++it) {
		if (it->first == "Content-Type" || it->first == "Content-Length") {
			continue;
		}
		
		std::string	headerName = "HTTP_" + it->first;
		for (size_t i = 0; i < headerName.size(); ++i) {
			if (headerName[i] == '-')
				headerName[i] = '_';
			else
				headerName[i] = std::toupper(headerName[i]);
		}
		env[headerName] = it->second;
	}
	
	if (request.method == "POST" || request.method == "PUT" || !request.body.empty()) {
		std::ostringstream	oss;
		oss << request.body.size();
		env["CONTENT_LENGTH"] = oss.str();
		
		std::map<std::string, std::string>::const_iterator	ctIt = request.headers.find("Content-Type");
		if (ctIt != request.headers.end()) {
			env["CONTENT_TYPE"] = ctIt->second;
		} else {
			env["CONTENT_TYPE"] = "application/octet-stream";
		}
	} else {
		env["CONTENT_LENGTH"] = "0";
		env["CONTENT_TYPE"] = "";
	}
	return env;
}

void	CGI::setupEnvironment(const std::map<std::string, std::string>& env) {
	for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
		setenv(it->first.c_str(), it->second.c_str(), 1);
}

char**	CGI::buildEnvArray(const std::map<std::string, std::string>& env) {
	char**	envp = new char*[env.size() + 1];
	
	size_t i = 0;
	for (std::map<std::string, std::string>::const_iterator it = env.begin(); 
		 it != env.end(); ++it, ++i) {
		std::string envStr = it->first + "=" + it->second;
		envp[i] = new char[envStr.length() + 1];
		std::strcpy(envp[i], envStr.c_str());
	}
	envp[env.size()] = NULL;
	return envp;
}

void	CGI::freeEnvArray(char** envp) {
	if (!envp)
		return;
	for (size_t i = 0; envp[i] != NULL; ++i)
		delete[] envp[i];
	delete[] envp;
}

std::string	CGI::readFromPipe(int fd) {
	std::string	result;
	char		buffer[1024];
	ssize_t		bytesRead;
	
	while ((bytesRead = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
		buffer[bytesRead] = '\0';
		result += buffer;
	}
	return result;
}

std::string	CGI::getDirectoryFromPath(const std::string& path) {
	size_t lastSlash = path.find_last_of('/');
	if (lastSlash == std::string::npos)
		return ".";
	if (lastSlash == 0)
		return "/";
	return path.substr(0, lastSlash);
}

std::string	CGI::getFilenameFromPath(const std::string& path) {
	size_t lastSlash = path.find_last_of('/');
	if (lastSlash == std::string::npos)
		return path;
	return path.substr(lastSlash + 1);
}
