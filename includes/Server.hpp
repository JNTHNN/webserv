#ifndef SERVER_HPP
#define SERVER_HPP

#include "Client.hpp"
#include "ServerConfig.hpp"
#include "utils.hpp"
#include <poll.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <csignal>

class Server {
	private:
		std::vector<int>				listen_fds;
		std::map<int,size_t>			listen_index_by_fd;
		std::vector<ServerConfig>		configs;

		int								maxClients;
		int								timeoutSeconds;
		std::vector<struct pollfd>		fds;
		std::map<int, Client>			clients;

		int								openListenSocket(const std::string& host, int port);
		void							setupSockets();
		void							setupPollFds();
		bool							isListenFd(int fd) const;

		void							acceptNewClient(int listen_fd);
		void							closeClient(int index);
		void							handleReadEvent(int index);
		void							handleWriteEvent(int index);

		static const LocationConfig*	matchLocation(const ServerConfig& sc, const std::string& uri);
		static void						applyRoute(Client& c, const ServerConfig& sc, const LocationConfig* loc);

		static bool						headerHasChunked(const std::string& headers);
		static size_t					headerContentLength(const std::string& headers);
		static bool						isChunkedEncoding(const std::string& headers);
		static bool						isChunkedComplete(const std::string& data, size_t headerEnd);
		static std::string				trimLeft(const std::string& s);
		static std::string				trimRight(const std::string& s);
		static std::string				trim(const std::string& s);

	public:
		Server(const std::vector<ServerConfig>& cfgs, int maxClients, int timeoutSeconds);
		~Server();

		void run();
};

#endif