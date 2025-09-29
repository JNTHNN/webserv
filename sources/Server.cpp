#include "Server.hpp"

static bool	running = true;
static void	handleSigint(int){ std::cout << "\nSIGINT received, shutting down cleanly...\n"; running = false; }

Server::Server(const std::vector<ServerConfig>& cfgs, int maxClients_, int timeoutSeconds_)
: configs(cfgs), maxClients(maxClients_), timeoutSeconds(timeoutSeconds_) {
	signal(SIGINT, handleSigint);
	try {
		setupSockets();
		setupPollFds();
	} catch (std::exception &e) {
		std::cout << e.what() << std::endl;
		running = false;
	}
}

Server::~Server() {
	for (size_t i = 1; i < fds.size(); ++i) {
		if (fds[i].fd != -1)
			close(fds[i].fd);
	}
	for (size_t i = 0; i < listen_fds.size(); ++i) {
		if (listen_fds[i] != -1)
			close(listen_fds[i]);
	}
}

/*
**  fcntl   -> permet de manipuler les fd 
**  F_GETFD / F_SETFD	: lire/écrire les flags du descripteur
**  FD_CLOEXEC			: ferme le fd lors d’un exec
**  F_SETFL				: définir les flags d’état
**  O_NONBLOCK			: mode non-bloquant
**  ----------------------------------
**  bind    -> associe une socket a une ip:port
**  listen  -> place la socket en mode ecoute donc accepte les connexions entrantes
*/

int	Server::openListenSocket(const std::string& host, int port) {
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;
	int	fdflags = fcntl(s, F_GETFD, 0);
	if (fdflags != -1)
		fcntl(s, F_SETFD, fdflags | FD_CLOEXEC);
	int	opt = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(s);
		return -1;
	}
	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(host.c_str());
	if (addr.sin_addr.s_addr == (in_addr_t)-1) {
		close(s);
		return -1;
	}
	if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
		std::cerr << "Error: bind failed on " << host << ":" << port << " - " << strerror(errno) << std::endl;
		close(s);
		return -1;
	}
	if (listen(s, maxClients) < 0) {
		close(s);
		return -1;
	}
	fcntl(s, F_SETFL, O_NONBLOCK);
	return s;
}

void	Server::setupSockets() {
	listen_fds.clear();
	listen_index_by_fd.clear();

	std::set<std::string>	used;

	for (size_t i = 0; i < configs.size(); ++i) {
		const ServerConfig&	cfg = configs[i];

		std::ostringstream	key;
		key << cfg.host << ":" << cfg.port;

		if (!used.insert(key.str()).second)
			throw std::runtime_error("Duplicate listen " + key.str() + " without vhost support");
		int	fd = openListenSocket(cfg.host, cfg.port);
		if (fd < 0)
			throw std::runtime_error("Failed to listen on " + key.str());
		listen_fds.push_back(fd);
		listen_index_by_fd[fd] = i;
		std::cout << "Listening on " << key.str() << std::endl;
	}
	if (listen_fds.empty())
		throw std::runtime_error("No listening sockets");
}

void	Server::setupPollFds() {
	fds.clear();
	for (size_t i = 0; i < listen_fds.size(); ++i) {
		struct pollfd   p;
		p.fd = listen_fds[i];
		p.events = POLLIN;
		p.revents = 0;
		fds.push_back(p);
	}
	for (int i = 0; i < maxClients; ++i) {
		struct pollfd	p;
		p.fd = -1;
		p.events = 0;
		p.revents = 0;
		fds.push_back(p);
	}
}

bool	Server::isListenFd(int fd) const {
	return listen_index_by_fd.find(fd) != listen_index_by_fd.end();
}

std::string Server::trimLeft(const std::string& s) {
	size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
		++i;
	return s.substr(i);
}
std::string Server::trimRight(const std::string& s) {
	size_t j = s.size();
	while (j && (s[j-1] == ' ' || s[j-1] == '\t' || s[j-1] == '\r' || s[j-1] == '\n'))
		--j;
	return s.substr(0,j);
}
std::string	Server::trim(const std::string& s) {
	return trimRight(trimLeft(s));
}

bool	Server::headerHasChunked(const std::string& headers) {
	size_t start = 0;
	while (start < headers.size()) {
		size_t		end = headers.find("\r\n", start);
		std::string	line = headers.substr(start, (end==std::string::npos)?std::string::npos:end-start);
		if (line.empty())
			break;
		size_t colon = line.find(':');
		if (colon != std::string::npos) {
			std::string	name = trimRight(line.substr(0, colon));
			std::string	value = trim(line.substr(colon + 1));
			for (size_t i = 0; i < name.size(); ++i)
				name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
			if (name == "transfer-encoding") {
				if (value.find("chunked") != std::string::npos)
					return true;
			}
		}
		if (end == std::string::npos)
			break;
		start = end + 2;
	}
	return false;
}
size_t	Server::headerContentLength(const std::string& headers) {
	size_t start = 0;
	while (start < headers.size()) {
		size_t	end = headers.find("\r\n", start);
		std::string	line = headers.substr(start, (end == std::string::npos) ? std::string::npos:end - start);
		if (line.empty())
			break;
		size_t	colon = line.find(':');
		if (colon != std::string::npos) {
			std::string	name = trimRight(line.substr(0, colon));
			std::string	value = trim(line.substr(colon + 1));
			for (size_t i = 0; i < name.size(); ++i)
				name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
			if (name == "content-length") {
				size_t  cl = 0;
				for (size_t k = 0; k < value.size(); ++k) {
					if (value[k] < '0' || value[k] > '9')
						return 0;
					cl = cl * 10 + (value[k] - '0');
				}
				return cl;
			}
		}
		if (end == std::string::npos)
			break;
		start = end + 2;
	}
	return 0;
}

bool Server::isChunkedEncoding(const std::string& headers) {
	size_t start = 0;
	while (start < headers.size()) {
		size_t end = headers.find("\r\n", start);
		std::string line = headers.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
		if (line.empty()) break;
		
		size_t colon = line.find(':');
		if (colon != std::string::npos) {
			std::string name = trimRight(line.substr(0, colon));
			std::string value = trim(line.substr(colon + 1));
			
			for (size_t i = 0; i < name.size(); ++i) {
				name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
			}
			
			if (name == "transfer-encoding" && value == "chunked") {
				return true;
			}
		}
		
		if (end == std::string::npos) break;
		start = end + 2;
	}
	return false;
}

bool Server::isChunkedComplete(const std::string& data, size_t headerEnd) {
	size_t	pos = headerEnd + 4;
	
	while (pos < data.size()) {
		size_t	crlfPos = data.find("\r\n", pos);
		if (crlfPos == std::string::npos)
			return false;

		std::string	sizeStr = data.substr(pos, crlfPos - pos);
		size_t		semicolon = sizeStr.find(';');
		if (semicolon != std::string::npos)
			sizeStr = sizeStr.substr(0, semicolon);
		sizeStr = trim(sizeStr);
		
		char*	endPtr;
		size_t	chunkSize = std::strtoul(sizeStr.c_str(), &endPtr, 16);
		if (*endPtr != '\0')
			return false;
		
		if (chunkSize == 0) {
			pos = crlfPos + 2;
			size_t endTrailers = data.find("\r\n\r\n", pos);
			return (endTrailers != std::string::npos);
		}
		
		pos = crlfPos + 2;
		if (pos + chunkSize + 2 > data.size())
			return false;
		
		pos += chunkSize + 2;
	}
	return false;
}

const LocationConfig*	Server::matchLocation(const ServerConfig& sc, const std::string& uri) {
	const LocationConfig*   best = NULL;
	size_t                  bestLen = 0;
	std::map<std::string,LocationConfig>::const_iterator    it = sc.locations.begin();
	for (; it != sc.locations.end(); ++it) {
		const std::string&  p = it->first;
		if (uri.compare(0, p.size(), p) == 0 && p.size() > bestLen) {
			best = &it->second;
			bestLen = p.size();
		}
	}
	return best;
}

void    Server::applyRoute(Client& c, const ServerConfig& sc, const LocationConfig* loc) {
	c.docRoot          = (loc && !loc->root.empty()) ? loc->root : "./www";
	c.indexFile        = (loc && !loc->index.empty()) ? loc->index : "index.html";
	c.autoindexEnabled = (loc ? loc->autoindex : false);

    c.allowedMethods.clear();
    if (loc && !loc->methods.empty()) {
    	c.allowedMethods = loc->methods;
	} else {
        c.allowedMethods.insert("GET");
	}
	if (loc && loc->has_max_body_size) {
		c.maxBodySize = loc->max_body_size;
	} else {
		c.maxBodySize = sc.max_body_size;
	}
	c.errorPages  = sc.error_pages;

	c.uploadEnabled = (loc && loc->upload_enable);
	if (loc && !loc->upload_store.empty())
		c.uploadStore = loc->upload_store;
	else
		c.uploadStore = c.docRoot + "/uploads";

	c.locationPrefix = (loc ? loc->path : "/");

	c.redirectCode = (loc ? loc->redirect_code : 0);
	c.redirectURL  = (loc ? loc->redirect_url  : std::string());

	c.cgiEnable = (loc ? loc->cgi_enable : false);
	c.cgiPath   = (loc ? loc->cgi_path   : std::string());
	c.cgiExt    = (loc ? loc->cgi_extension : std::string());
}

/*
**	poll	-> permet de surveiller plusieurs fd afin de savoir si on peut lire/ecrire/error dessus
**	POLLIN		: prêt à lire
**	POLLOUT		: prêt à écrire
**	POLLERR		: erreur
**	POLLHUP		: connexion fermée
**	POLLNVAL	: fd invalide
*/

void    Server::run() {
	while (running) {
		time_t	now = std::time(NULL);

		int	ret = poll(&fds[0], fds.size(), 1000);
		if (ret <= 0) {
			if (ret < 0 && running)
				perror("poll");
			continue;
		}

		for (size_t i = 0; i < listen_fds.size(); ++i) {
			if (fds[i].fd != -1 && (fds[i].revents & POLLIN))
				acceptNewClient(fds[i].fd);
		}

		for (size_t i = listen_fds.size(); i < fds.size(); ++i) {
			int	fd = fds[i].fd;
			if (fd == -1)
				continue;
			struct pollfd   &p = fds[i];
			if (p.revents & (POLLERR | POLLNVAL)) {
				closeClient(i);
				continue;
			}
			Client& client = clients[fd];
			if (now - client.lastActivity > timeoutSeconds) {
				if (client.writeBuffer.empty()) {
					closeClient(i);
					continue;
				}
				p.events = POLLOUT;
			}
			bool	did = false;
			if ((p.revents & POLLOUT) && !did) {
				handleWriteEvent((int)i);
				did = true;
			}
			if (p.fd == -1)
				continue;
			if ((p.revents & POLLIN) && !did) {
				handleReadEvent((int)i);
				did = true;
			}
			if (p.fd == -1)
				continue;

			if (p.revents & POLLHUP) {
				if (!client.writeBuffer.empty()) {
					fds[i].events = POLLOUT;
					continue;
				}
				if (!client.headersParsed) {
					size_t  headerEnd = client.readBuffer.find("\r\n\r\n");
					if (headerEnd == std::string::npos) {
						client.response.buildErrorResponseWithPages(400, "Bad Request", client.docRoot, client.errorPages);
						client.writeBuffer = client.response.raw;
						client.headersParsed = true;
						fds[i].events = POLLOUT;
						continue;
					}

					size_t		headersLen = headerEnd + 4;
					std::string	headers = client.readBuffer.substr(0, headersLen);
					size_t		lineEnd = headers.find("\r\n");
					std::string	reqLine = (lineEnd == std::string::npos) ? headers : headers.substr(0, lineEnd);
					std::string	method, target, version;
					{
						std::istringstream	iss(reqLine);
						iss >> method >> target >> version;
					}
					std::string	pthRaw = "/";
					if (!target.empty()) {
						size_t  qm = target.find('?');
						pthRaw = (qm == std::string::npos) ? target : target.substr(0, qm);
						if (pthRaw.empty())
							pthRaw = "/";
					}
					bool		ok = true;
					std::string	decoded = percentDecode(pthRaw, ok);
					if (!ok) {
						client.response.buildErrorResponseWithPages(400, "Bad Request", client.docRoot, client.errorPages);
						client.writeBuffer = client.response.raw;
						client.headersParsed = true;
						fds[i].events = POLLOUT;
						continue;
					}
					std::string	forMatch = compressSlashes(decoded);
					if (forMatch.empty())
						forMatch = "/";
					std::string	normalizedForMatch = removeDotSegments(forMatch);
					if (normalizedForMatch.empty())
						normalizedForMatch = "/";
					const ServerConfig&		sc = configs[client.serverIndex];
					const LocationConfig*	loc = matchLocation(sc, normalizedForMatch);
					applyRoute(client, sc, loc);

					size_t	cl = headerContentLength(headers);
					bool	isChunked = isChunkedEncoding(headers);
					size_t	bodyHave = client.readBuffer.size() - headersLen;
					
					if (cl > 0 && cl > client.maxBodySize) {
						client.response.buildErrorResponseWithPages(413, "Payload Too Large", client.docRoot, client.errorPages);
						client.writeBuffer = client.response.raw;
						client.headersParsed = true;
						fds[i].events = POLLOUT;
						continue;
					}
					
					if (isChunked) {
						if (!isChunkedComplete(client.readBuffer, headersLen)) {
							continue;
						}
					} else if (cl > 0) {
						if (bodyHave < cl) {
							continue;
						}
					}

					if (client.request.parse(client.readBuffer)) {
						if (isChunked) {
							if (!client.request.parseChunkedBody(client.readBuffer, headersLen)) {
								client.response.buildErrorResponseWithPages(400, "Bad Request - Invalid Chunked Encoding", client.docRoot, client.errorPages);
								client.writeBuffer = client.response.raw;
								client.headersParsed = true;
								fds[i].events = POLLOUT;
								continue;
							}
						}
						
						client.request.rawUri = forMatch;
						const LocationConfig*   loc2 = matchLocation(sc, normalizedForMatch);
						applyRoute(client, sc, loc2);

						client.headersParsed = true;
						client.handleRequest(sc);
						client.writeBuffer = client.response.raw;
						fds[i].events = POLLOUT;
					} else {
						client.response.buildErrorResponseWithPages(400, "Bad Request", client.docRoot, client.errorPages);
						client.writeBuffer = client.response.raw;
						client.headersParsed = true;
						fds[i].events = POLLOUT;
					}
					continue;
				}
				closeClient(i);
				continue;
			}
		}
	}
}

void	Server::acceptNewClient(int listen_fd) {
	int	client_fd = accept(listen_fd, NULL, NULL);
	if (client_fd < 0)
		return;
	int	fdflags = fcntl(client_fd, F_GETFD, 0);
	if (fdflags != -1)
		fcntl(client_fd, F_SETFD, fdflags | FD_CLOEXEC);
	fcntl(client_fd, F_SETFL, O_NONBLOCK);
	for (size_t i = listen_fds.size(); i < fds.size(); ++i) {
		if (fds[i].fd == -1) {
			fds[i].fd = client_fd;
			fds[i].events = POLLIN;
			clients[client_fd] = Client(client_fd);
			size_t sidx = listen_index_by_fd[listen_fd];
			clients[client_fd].serverIndex = sidx;
			return;
		}
	}
	close(client_fd);
}

void    Server::closeClient(int index) {
	int fd = fds[index].fd;
	if (fd != -1) {
		close(fd);
		fds[index].fd = -1;
		clients.erase(fd);
	}
}

void    Server::handleReadEvent(int index) {
	int     fd = fds[index].fd;
	Client&	client = clients[fd];
	char    buf[4096];

	ssize_t bytes = recv(fd, buf, sizeof(buf), 0);
	if (bytes <= 0) {
		closeClient(index);
		return;
	}
	client.readBuffer.append(buf, (size_t)bytes);
	client.lastActivity = std::time(NULL);

	size_t  headerEnd = client.readBuffer.find("\r\n\r\n");
	if (headerEnd == std::string::npos)
		return;

	size_t      headersLen = headerEnd + 4;
	std::string headers = client.readBuffer.substr(0, headersLen);
	size_t      lineEnd = headers.find("\r\n");
	std::string reqLine = (lineEnd == std::string::npos) ? headers : headers.substr(0, lineEnd);
	std::string method, target, version;
	{
		std::istringstream  iss(reqLine);
		iss >> method >> target >> version;
	}

	std::string pthRaw = "/";
	if (!target.empty()) {
		size_t  qm = target.find('?');
		pthRaw = (qm == std::string::npos) ? target : target.substr(0, qm);
		if (pthRaw.empty())
			pthRaw = "/";
	}

	bool        ok = true;
	std::string decoded = percentDecode(pthRaw, ok);
	if (!ok) {
		client.response.buildErrorResponseWithPages(400, "Bad Request", client.docRoot, client.errorPages);
		client.writeBuffer = client.response.raw;
		fds[index].events = POLLOUT;
		client.headersParsed = true;
		return;
	}

	std::string forMatch = compressSlashes(decoded);
	if (forMatch.empty())
		forMatch = "/";
	std::string normalizedForMatch = removeDotSegments(forMatch);
	if (normalizedForMatch.empty())
		normalizedForMatch = "/";

	const ServerConfig&     sc = configs[client.serverIndex];
	const LocationConfig*   loc = matchLocation(sc, normalizedForMatch);
	applyRoute(client, sc, loc);

	size_t  cl = headerContentLength(headers);
	bool    isChunked = isChunkedEncoding(headers);
	size_t  bodyHave = client.readBuffer.size() - headersLen;
	
	if (isChunked && !(loc && loc->cgi_enable)) {
		client.response.buildErrorResponseWithPages(501, "Not Implemented", client.docRoot, client.errorPages);
		client.writeBuffer = client.response.raw;
		fds[index].events = POLLOUT;
		client.headersParsed = true;
		return;
	}
	
	if (cl > 0 && cl > client.maxBodySize) {
		client.response.buildErrorResponseWithPages(413, "Payload Too Large", client.docRoot, client.errorPages);
		client.writeBuffer = client.response.raw;
		fds[index].events = POLLOUT;
		client.headersParsed = true;
		return;
	}
	
	if (isChunked) {
		if (!isChunkedComplete(client.readBuffer, headersLen)) {
			return;
		}
	} else if (cl > 0) {
		if (bodyHave < cl) {
			return;
		}
	}

	if (!client.headersParsed) {
		if (client.request.parse(client.readBuffer)) {
			if (isChunked) {
				if (!client.request.parseChunkedBody(client.readBuffer, headersLen)) {
					client.response.buildErrorResponseWithPages(400, "Bad Request - Invalid Chunked Encoding", client.docRoot, client.errorPages);
					client.writeBuffer = client.response.raw;
					client.headersParsed = true;
					fds[index].events = POLLOUT;
					return;
				}
			}
			
			client.request.rawUri = forMatch;
			
			const LocationConfig*   loc2 = matchLocation(sc, normalizedForMatch);
			applyRoute(client, sc, loc2);

			client.headersParsed = true;
			client.handleRequest(sc);
			client.writeBuffer = client.response.raw;
			fds[index].events = POLLOUT; 
		} else {
			client.response.buildErrorResponseWithPages(400, "Bad Request", client.docRoot, client.errorPages);
			client.writeBuffer = client.response.raw;
			fds[index].events = POLLOUT;
			client.headersParsed = true;
		}
	}
}

void    Server::handleWriteEvent(int index) {
	int     fd = fds[index].fd;
	Client& client = clients[fd];

	ssize_t sent = send(fd, client.writeBuffer.c_str(), client.writeBuffer.size(), 0);
	if (sent > 0) {
		client.writeBuffer.erase(0, (size_t)sent);
		if (client.writeBuffer.empty())
			closeClient(index);
		else
			fds[index].events = POLLOUT;
	} else
		closeClient(index);
}
