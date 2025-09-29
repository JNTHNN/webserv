#include "ConfigParser.hpp"
#include "Server.hpp"

int main(int argc, char** argv) {
	std::string confPath = (argc > 1) ? argv[1] : "conf/server.conf"; 
	try {
		ConfigParser				parser(confPath);
		std::vector<ServerConfig>	servers = parser.parseAll();
		Server						server(servers, 128, 10);
		server.run();
	} catch (std::exception& e) {
		std::cerr << "Config error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
