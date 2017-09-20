#LIBS=-lboost_system -lboost_program_options
LIBS=-lmongocxx -lbsoncxx -lcurl -lboost_system -lboost_thread -lpthread -lboost_regex -lboost_program_options

MYFLAGS= -ggdb -std=c++14  -I /usr/local/include/mongocxx/v_noabi/ -I /usr/local/include/bsoncxx/v_noabi/

all: parsedb
release: all


parsedb: parsedb.o
	$(CXX) $(MYFLAGS) -Wall -g $^ -o $@ ${LIBS}

clean:
	rm -fr *.o parsedb

%.o: %.cpp 
	$(CXX) $(MYFLAGS) -c $< -o $@ 


