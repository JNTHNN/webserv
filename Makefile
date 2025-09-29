NAME     := webserv
CPP      := c++
CPPFLAGS := -Wall -Wextra -Werror -std=c++98 -Iincludes

SRC_DIR  := sources
OBJ_DIR  := .obj

SRCS := \
  $(SRC_DIR)/main.cpp \
  $(SRC_DIR)/Client.cpp \
  $(SRC_DIR)/Request.cpp \
  $(SRC_DIR)/Response.cpp \
  $(SRC_DIR)/Server.cpp \
  $(SRC_DIR)/ConfigParser.cpp \
  $(SRC_DIR)/utils.cpp \
  $(SRC_DIR)/CGI.cpp

OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CPP) $(CPPFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CPP) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
