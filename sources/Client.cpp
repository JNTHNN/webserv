#include "Client.hpp"


Client::Client() : fd(-1), lastActivity(std::time(NULL)), headersParsed(false) {}
Client::Client(int fd): fd(fd), lastActivity(std::time(NULL)), headersParsed(false) {}

static bool	ensureDir(const std::string& dir) {
	struct stat	st;
	if (stat(dir.c_str(), &st) == 0)
		return S_ISDIR(st.st_mode);
	return mkdir(dir.c_str(), 0755) == 0;
}

static bool	ensureUploadsDirPath(const std::string& store) {
	return ensureDir(store);
}

void	Client::handleRequest(const ServerConfig& serverConfig) {
	if (request.method.empty() || request.rawUri.empty() || request.version.empty()) {
		response.buildErrorResponseWithPages(400, "Bad Request", docRoot, errorPages);
		return;
	}
	if (request.version == "HTTP/1.1") {
		std::map<std::string,std::string>::iterator	hit = request.headers.find("Host");
		if (hit == request.headers.end() || hit->second.empty()) {
			response.buildErrorResponseWithPages(400, "Bad Request", docRoot, errorPages);
			return;
		}
	}
	if (request.version != "HTTP/1.1") {
		response.buildErrorResponseWithPages(505, "HTTP Version Not Supported", docRoot, errorPages);
		return;
	}

	if (request.method != "GET" && request.method != "POST" && request.method != "DELETE") {
		response.build405("GET, POST, DELETE");
		return;
	}

	if (redirectCode >= 300 && redirectCode <= 308 && !redirectURL.empty()) {
		response.buildRedirect(redirectCode, redirectURL);
		return;
	}

	std::string	base = docRoot.empty() ? "./www" : docRoot;
	std::string	rel = request.rawUri;
	if (rel.empty())
		rel = "/";
	size_t		qpos = rel.find('?');
	if (qpos != std::string::npos)
		rel.erase(qpos);
	if (rel.empty())
		rel = "/";
	if (rel[0] != '/')
		rel.insert(rel.begin(), '/');
	rel = compressSlashes(rel);
	if (rel.empty())
		rel = "/";

	{
		size_t	i = 0;
		while (i < rel.size()) {
			while (i < rel.size() && rel[i] == '/')
				++i;
			if (i >= rel.size())
				break;
			size_t	j = i;
			while (j < rel.size() && rel[j] != '/')
				++j;
			if (j > i) {
				std::string	seg = rel.substr(i, j - i);
				if (seg == "..") {
					response.buildErrorResponseWithPages(403, "Forbidden", docRoot, errorPages);
					return;
				}
			}
			i = j;
		}
	}

	rel = removeDotSegments(rel);
	if (rel.empty())
		rel = "/";

	if (!locationPrefix.empty()
		&& rel.size() >= locationPrefix.size()
		&& rel.compare(0, locationPrefix.size(), locationPrefix) == 0)
			rel.erase(0, locationPrefix.size());
	if (rel.empty())
		rel = "/";
	if (rel[0] != '/')
		rel.insert(rel.begin(), '/');

	if (request.method == "DELETE" && !uploadStore.empty())
		base = uploadStore;

	std::string	path = base + rel;
	char		resolvedBase[PATH_MAX];
	char		resolvedPath[PATH_MAX];

	if (!realpath(base.c_str(), resolvedBase) || !realpath(path.c_str(), resolvedPath)) {
		response.buildErrorResponseWithPages(404, "Not Found", docRoot, errorPages);
		return;
	}
	if (strncmp(resolvedPath, resolvedBase, strlen(resolvedBase)) != 0 ||
		(resolvedPath[strlen(resolvedBase)] != '/' && resolvedPath[strlen(resolvedBase)] != '\0')) {
		response.buildErrorResponseWithPages(403, "Forbidden", docRoot, errorPages);
		return;
	}

	if (!allowedMethods.empty() && allowedMethods.find(request.method) == allowedMethods.end()) {
		std::string	allow;
		for (std::set<std::string>::const_iterator it=allowedMethods.begin(); it!=allowedMethods.end(); ++it) {
			if (!allow.empty())
				allow += ", ";
			allow += *it;
		}
		std::cerr << allow << " - returning 405" << std::endl;
		response.build405(allow);
		return;
	}

	if (request.method == "GET")
		handleGet(resolvedPath, serverConfig);
	else if (request.method == "POST") {
		handlePost(serverConfig);
	}
	else if (request.method == "DELETE")
		handleDelete(resolvedPath);
}

void	Client::handleGet(const char* resolvedPath, const ServerConfig& serverConfig) {
	struct stat	st;
	if (stat(resolvedPath, &st) != 0) {
		response.buildErrorResponseWithPages(404, "Not Found", docRoot, errorPages);
		return;
	}

	if (S_ISDIR(st.st_mode)) {
		std::string	idx = indexFile.empty() ? "index.html" : indexFile;
		std::string	indexPath = std::string(resolvedPath);
		if (!indexPath.empty() && indexPath[indexPath.size()-1] != '/')
			indexPath += "/";
		indexPath += idx;

		struct stat	stIdx;
		if (stat(indexPath.c_str(), &stIdx) == 0 && S_ISREG(stIdx.st_mode)) {
			response.buildFileResponse(indexPath);
			return;
		}

		if (!request.rawUri.empty() && request.rawUri[request.rawUri.size()-1] != '/') {
			std::string	to = request.rawUri + "/";
			response.buildRedirect(301, to);
			return;
		}

		if (autoindexEnabled)
			response.buildAutoindex(resolvedPath, request.rawUri);
		else
			response.buildErrorResponseWithPages(403, "Forbidden", docRoot, errorPages);
		return;
	}

	if (cgiEnable && !cgiPath.empty() && !cgiExt.empty()) {
		std::string filePath(resolvedPath);
		if (filePath.size() >= cgiExt.size() && 
			filePath.compare(filePath.size() - cgiExt.size(), cgiExt.size(), cgiExt) == 0) {
			
			std::string cgiOutput;
			if (CGI::execute(filePath, cgiPath, request, serverConfig, cgiOutput)) {
				response.buildCGIResponse(cgiOutput);
			} else {
				response.buildErrorResponseWithPages(500, "Internal Server Error", docRoot, errorPages);
			}
			return;
		}
	}
	response.buildFileResponse(resolvedPath);
}

bool	Client::isMultipart(std::string& boundaryOut) const {
	std::map<std::string, std::string>::const_iterator	it = request.headers.find("Content-Type");
	if (it == request.headers.end())
		return false;
	const std::string&	ct = it->second;
	if (ct.compare(0, 19, "multipart/form-data") != 0)
		return false;
	size_t	bpos = ct.find("boundary=");
	if (bpos == std::string::npos)
		return false;
	std::string	b = ct.substr(bpos + 9);
	if (!b.empty() && (b[0] == '"' || b[0] == '\''))
		if (b.size() >= 2 && b[b.size() - 1] == b[0])
			b = b.substr(1, b.size() - 2);
	boundaryOut = std::string("--") + b;
	return true;
}

std::string	Client::sanitizeFilename(const std::string& name) {
	std::string	out;
	out.reserve(name.size());
	for (size_t i = 0; i < name.size(); ++i) {
		char	c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')
			out += c;
		else
			out += '_';
	}
	if (out.empty() || out == "." || out == ".." || out[0] == '.') {
		std::ostringstream oss; oss << "upload_" << std::time(NULL);
		out = oss.str();
	}
	const size_t	MAXLEN = 255;
	if (out.size() > MAXLEN) {
		size_t dot = out.find_last_of('.');
		if (dot != std::string::npos && dot != 0 && (out.size() - dot) <= 10) {
			std::string ext = out.substr(dot);
			std::string base = out.substr(0, MAXLEN - ext.size());
			out = base + ext;
		} else
			out.erase(MAXLEN);
	}
	return out;
}

bool Client::saveMultipartFiles(const std::string& boundary, std::vector<std::string>& savedFiles) {
	const std::string&	body = request.body;
	const std::string	boundaryEnd = boundary + "--";
	size_t				pos = 0;

	pos = body.find(boundary, pos);
	if (pos == std::string::npos)
		return false;
	pos += boundary.size();
	if (pos < body.size() && body.compare(pos, 2, "\r\n") == 0)
		pos += 2;

	while (pos < body.size()) {
		size_t headersEnd = body.find("\r\n\r\n", pos);
		if (headersEnd == std::string::npos)
			break;
		std::string partHeaders = body.substr(pos, headersEnd - pos);
		pos = headersEnd + 4;

		std::string filename;
		size_t cdPos = partHeaders.find("Content-Disposition:");
		if (cdPos != std::string::npos) {
			size_t fnPos = partHeaders.find("filename=", cdPos);
			if (fnPos != std::string::npos) {
				fnPos += 9;
				if (fnPos < partHeaders.size() && (partHeaders[fnPos] == '"' || partHeaders[fnPos] == '\'')) {
					char	quote = partHeaders[fnPos];
					size_t	endq = partHeaders.find(quote, fnPos + 1);
					if (endq != std::string::npos)
						filename = partHeaders.substr(fnPos + 1, endq - (fnPos + 1));
				} else {
					size_t end = partHeaders.find_first_of(";\r\n", fnPos);
					filename = partHeaders.substr(fnPos, end == std::string::npos ? std::string::npos : end - fnPos);
				}
			}
		}

		size_t nextBoundary = body.find("\r\n" + boundary, pos);
		size_t nextBoundaryEnd = body.find("\r\n" + boundaryEnd, pos);
		bool isFinal = false;
		size_t partEnd;
		if (nextBoundary == std::string::npos && nextBoundaryEnd == std::string::npos)
			partEnd = body.size();
		else if (nextBoundaryEnd != std::string::npos && (nextBoundary == std::string::npos || nextBoundaryEnd < nextBoundary)) {
			partEnd = nextBoundaryEnd;
			isFinal = true;
		}
		else
			partEnd = nextBoundary;

		if (partEnd >= 2 && body.compare(partEnd - 2, 2, "\r\n") == 0)
			partEnd -= 2;

		if (!filename.empty()) {
			if (!ensureUploadsDirPath(uploadStore.empty() ? docRoot : uploadStore))
				return false;
			std::string		safe = sanitizeFilename(filename);
			std::string		targetDir = uploadStore.empty() ? docRoot : uploadStore;
			std::string		outPath = targetDir + "/" + safe;
			std::ofstream	out(outPath.c_str(), std::ios::binary);
			if (!out)
				return false;
			out.write(&body[pos], (std::streamsize)(partEnd - pos));
			out.close();
			savedFiles.push_back(outPath);
		}
		if (isFinal) {
			pos = partEnd + 2 + boundaryEnd.size();
			break;
		} else {
			pos = partEnd + 2 + boundary.size();
			if (pos < body.size() && body.compare(pos, 2, "\r\n") == 0) pos += 2;
		}
	}
	return !savedFiles.empty();
}

void Client::handlePost(const ServerConfig& serverConfig) {
	if (!allowedMethods.empty() && allowedMethods.find("POST") == allowedMethods.end()) {
		std::string	allow;
		for (std::set<std::string>::const_iterator it=allowedMethods.begin(); it!=allowedMethods.end(); ++it) {
			if (!allow.empty()) allow += ", ";
			allow += *it;
		}
		response.build405(allow);
		return;
	}
	
	if (cgiEnable && !cgiPath.empty() && !cgiExt.empty()) {
		std::string base = docRoot.empty() ? "./www" : docRoot;
		std::string rel = request.rawUri;
		if (rel.empty()) rel = "/";
		
		size_t qpos = rel.find('?');
		if (qpos != std::string::npos)
			rel.erase(qpos);
		
		if (rel.empty()) rel = "/";
		if (rel[0] != '/') rel.insert(rel.begin(), '/');
		rel = compressSlashes(rel);
		if (rel.empty()) rel = "/";
		
		if (!locationPrefix.empty() && rel.size() >= locationPrefix.size() &&
			rel.compare(0, locationPrefix.size(), locationPrefix) == 0) {
			rel.erase(0, locationPrefix.size());
		}
		if (rel.empty()) rel = "/";
		if (rel[0] != '/') rel.insert(rel.begin(), '/');
		
		std::string path = base + rel;
		char resolvedPath[PATH_MAX];
		
		if (realpath(path.c_str(), resolvedPath)) {
			std::string filePath(resolvedPath);
			if (filePath.size() >= cgiExt.size() && 
				filePath.compare(filePath.size() - cgiExt.size(), cgiExt.size(), cgiExt) == 0) {
				
				std::string cgiOutput;
				if (CGI::execute(filePath, cgiPath, request, serverConfig, cgiOutput)) {
					response.buildCGIResponse(cgiOutput);
				} else {
					response.buildErrorResponseWithPages(500, "Internal Server Error", docRoot, errorPages);
				}
				return;
			}
		}
	}
	
	if (!uploadEnabled) {
		response.buildSimple(200, "OK", "POST request received");
		return;
	}

	std::string	boundary;
	if (!isMultipart(boundary)) {
		response.buildErrorResponseWithPages(400, "Bad Request", docRoot, errorPages);
		return;
	}

	std::vector<std::string>	saved;
	if (saveMultipartFiles(boundary, saved) && !saved.empty()) {
		std::ostringstream	msg;
		msg << "Saved " << saved.size() << " file(s):\n";
		for (size_t i = 0; i < saved.size(); ++i) msg << saved[i] << "\n";
		response.buildSimple(201, "Created", msg.str());
		return;
	}
	response.buildErrorResponseWithPages(400, "Bad Request", docRoot, errorPages);
}

void	Client::handleDelete(const char* resolvedPath) {
	if (!allowedMethods.empty() && allowedMethods.find("DELETE") == allowedMethods.end()) {
		std::string	allow;
		for (std::set<std::string>::const_iterator it=allowedMethods.begin(); it!=allowedMethods.end(); ++it) {
			if (!allow.empty()) allow += ", ";
			allow += *it;
		}
		response.build405(allow);
		return;
	}

	char	allowedBuf[PATH_MAX];
	char	targetBuf[PATH_MAX];

	std::string allowRoot = uploadStore.empty() ? docRoot : uploadStore;

	if (!realpath(allowRoot.c_str(), allowedBuf)) {
		response.buildErrorResponseWithPages(500, "Internal Server Error", docRoot, errorPages);
		return;
	}
	if (!realpath(resolvedPath, targetBuf)) {
		response.buildErrorResponseWithPages(404, "Not Found", docRoot, errorPages);
		return;
	}

	size_t	allowedLen = strlen(allowedBuf);
	if (strncmp(targetBuf, allowedBuf, allowedLen) != 0 ||
		(targetBuf[allowedLen] != '/' && targetBuf[allowedLen] != '\0')) {
		response.buildErrorResponseWithPages(403, "Forbidden", docRoot, errorPages);
		return;
	}

	struct stat	st;
	if (stat(targetBuf, &st) != 0) {
		response.buildErrorResponseWithPages(404, "Not Found", docRoot, errorPages);
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		response.buildErrorResponseWithPages(403, "Forbidden", docRoot, errorPages);
		return;
	}

	if (remove(targetBuf) == 0)
		response.buildSimple(204, "No Content", "");
	else
		response.buildErrorResponseWithPages(404, "Not Found", docRoot, errorPages);
}

void Client::reset() {
	readBuffer.clear();
	writeBuffer.clear();
	headersParsed = false;
	request = Request();
	response = Response();
	lastActivity = std::time(NULL);
	serverIndex = 0;
	docRoot.clear();
	indexFile.clear();
	autoindexEnabled = false;
	allowedMethods.clear();
	errorPages.clear();
	maxBodySize = 0;
	uploadEnabled = false;
	uploadStore.clear();
	redirectCode = 0;
	redirectURL.clear();
	cgiEnable = false;
	cgiPath.clear();
	cgiExt.clear();
	locationPrefix.clear();
}
