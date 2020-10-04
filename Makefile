all: vec ls

vec: main.cpp monitor_neighbors.h
	g++ -pthread -std=c++98 -o vec_router main.cpp monitor_neighbors.h

ls: main.cpp monitor_neighbors.h
	g++ -pthread -std=c++98 -o ls_router main.cpp monitor_neighbors.h

.PHONY: clean
clean:
	rm *.o vec_router ls_router
