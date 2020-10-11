#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <vector>

extern int globalMyID;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];


size_t split(const std::string &txt, std::vector<std::string> &strs, char ch)
{
    size_t pos = txt.find( ch );
    size_t initialPos = 0;
    strs.clear();

    // Decompose statement
    while( pos != std::string::npos ) {
        strs.push_back( txt.substr( initialPos, pos - initialPos ) );
        initialPos = pos + 1;

        pos = txt.find( ch, initialPos );
    }

    // Add the last one
    strs.push_back( txt.substr( initialPos, std::min( pos, txt.size() ) - initialPos + 1 ) );

    return strs.size();
}

void broadcastNewPath(short int neighbors[], int cost_to_node, std::string path_to_node, short int heardFrom){
    for(int i=0; i<255; i++){
        if(neighbors[i] == -1)
            break;
        // path<dest>cost<cost>;<path>
        int msgLen = 4+sizeof(short int)+4+sizeof(int)+1+path_to_node.length();
        short int no_neighbor = htons(heardFrom);
        std::string sendBuf = "path" + std::to_string(no_neighbor) + "cost" + std::to_string(cost_to_node)+";"+path_to_node;

        sendto(globalSocketUDP, sendBuf.c_str(), sendBuf.length(), 0,
            (struct sockaddr*)&globalNodeAddrs[neighbors[i]], sizeof(globalNodeAddrs[neighbors[i]]));
    }
    return;
}

void checkNeighborAvailability(short int neighbors[], int D[], std::string P[], timeval globalLastHeartbeat[]){
    extern struct timeval cur_time;
    gettimeofday(&cur_time, 0);
    for(int i=0; i<255; i++){
        if(neighbors[i] == -1)
            break;
        // Unavailable after 3 seconds
        if(cur_time.tv_sec - globalLastHeartbeat[neighbors[i]].tv_sec >= 3){
            if(D[neighbors[i]] > 0){
                D[neighbors[i]] = D[neighbors[i]]*-1;
                broadcastNewPath(neighbors, D[neighbors[i]], P[neighbors[i]], neighbors[i]);
            }
        }
    }
    return;
}
//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length)
{
    int i;
    for(i=0;i<256;i++)
        if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
            sendto(globalSocketUDP, buf, length, 0,
                  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void* announceToNeighbors(void* unusedParam)
{
    struct timespec sleepFor;
    sleepFor.tv_sec = 0;
    sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
    while(1)
    {
        hackyBroadcast("HEREIAM", 7);
        nanosleep(&sleepFor, 0);
    }
}

void listenForNeighbors(char* logFile, int D[], std::string P[])
{
    FILE* theLogFile;
    theLogFile = fopen(logFile, "wb");
    char fromAddr[100];
    struct sockaddr_in theirAddr;
    socklen_t theirAddrLen;
    unsigned char recvBuf[1000];

    int bytesRecvd;
    short int neighbors[255];
    for(int i=0; i<255; i+=1)
        neighbors[255] = -1;
    while(1)
    {
        theirAddrLen = sizeof(theirAddr);
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000 , 0, 
                    (struct sockaddr*)&theirAddr, &theirAddrLen)) == -1)
        {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }
        
        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
        
        short int heardFrom = -1;
        if(strstr(fromAddr, "10.1.1."))
        {
            heardFrom = atoi(
                    strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
            
            //TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
            if(D[heardFrom] < 0){
                D[heardFrom] = D[heardFrom] * -1;
            }
            // Update neighbors list
            for(int i=0; i<255; i++) {
                if(neighbors[i] == heardFrom)
                    break;
                if(neighbors[i] == -1) {
                    neighbors[i] = heardFrom; 
                    break
                }
            }
            std::string path_to_node = "";
            path_to_node = std::to_string(globalMyID) + " " + std::to_string(heardFrom);

            // Path is different. Send to all neighbors 
            if(path_to_node.compare(P[heardFrom]) != 0){
                P[heardFrom] = path_to_node;
                broadcastNewPath(neighbors, D[heardFrom], path_to_node, heardFrom);
            }
            
            //record that we heard from heardFrom just now.
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }

        checkNeighborAvailability(neighbors, D, P, globalLastHeartbeat); //Check for unavailable nodes
        
        char logLine[100];
        short int dest;
        char no_dest[2];
        short int no_dest_int;
        char new_cost[4];
        int new_cost_int;
        int cost;
        int nexthop = 0;
        char message[100];
        char send_message[106];
        char path[1000];
        std::vector<std::string> cur_split_path;
        std::vector<std::string> new_split_path;
        memset(message, 0, 100);
        memset(send_message, 0, 106);
        //Is it a packet from the manager? (see mp2 specification for more details)
        //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        if(!strncmp(reinterpret_cast<const char*>(recvBuf), "send", 4))
        {
            //TODO send the requested message to the requested destination node
            // ...
            int messageBytes = bytesRecvd - 4 - sizeof(short int);
            memcpy(no_dest, recvBuf+4, sizeof(short int));
            memcpy(message, recvBuf+4+sizeof(short int), messageBytes);
            no_dest_int = (uint8_t)no_dest[0] + (uint8_t)no_dest[1] << 8;
            dest = ntohs(no_dest_int);
            if(dest == globalMyID){
                sprintf(logLine, "receive packet message %s\n", message); 
            }
            else if(D[dest] < 0){
                sprintf(logLine, "unreachable dest %d\n", dest); 
            }
            else if(heardFrom > 0){                    
                sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", dest, nexthop, message); 
            }
            else{
                sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", dest, nexthop, message); 
            }

            sprintf(logLine, "sending packet dest %hd nexthop %d message %s\n", dest, nexthop, message); 
            std::cout << logLine << std::endl;
            fwrite(logLine, 1, strlen(logLine), theLogFile);
            int next_dest = dest; //TODO: find next dest
            sprintf(send_message, "send%hd%s", next_dest, nexthop, message); 
            sendto(globalSocketUDP, send_message, strlen(send_message), 0,
                (struct sockaddr*)&globalNodeAddrs[next_dest], sizeof(globalNodeAddrs[next_dest]));
        }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
        if(!strncmp(reinterpret_cast<const char*>(recvBuf), "cost", 4))
        {
            // TODO: Busted right now
            //TODO record the cost change (remember, the link might currently be down! in that case,
            //this is the new cost you should treat it as having once it comes back up.)
            int messageBytes = bytesRecvd - 4 - sizeof(short int);
            memcpy(no_dest, recvBuf+4, sizeof(short int));
            memcpy(new_cost, recvBuf+4+sizeof(short int), 4);
            no_dest_int = (uint8_t)no_dest[0] + (uint8_t)no_dest[1] << 8;
            new_cost_int = (uint8_t)no_dest[0] + (uint8_t)no_dest[1] << 8 + (uint8_t)no_dest[0] << 16 + (uint8_t)no_dest[1] << 24;
            dest = ntohs(no_dest_int);
            cost = ntohl(new_cost_int);
            sprintf(logLine, "new cost from self to dest: %hd cost: %d \n", dest, cost); 
            std::cout << logLine << std::endl;
            if(D[dest] < 0)
                D[dest] = cost*-1;
            else
                D[dest] = cost;
            
            // TODO: Alert all neighbors
        }
        //'path'<2 byte signed>cost<int>;<some ASCII message>
        if(!strncmp(reinterpret_cast<const char*>(recvBuf), "path", 4))
        {
            int messageBytes = bytesRecvd - 4 - sizeof(short int) - 4 - sizeof(int) - 1;
            memcpy(no_dest, recvBuf+4, sizeof(short int));
            memcpy(new_cost, recvBuf+4+sizeof(short int)+4, sizeof(int));
            memcpy(path, recvBuf+4+sizeof(short int)+4+sizeof(int)+1, messageBytes);
            no_dest_int = (uint8_t)no_dest[0] + (uint8_t)no_dest[1] << 8;
            new_cost_int = (uint8_t)no_dest[0] + (uint8_t)no_dest[1] << 8 + (uint8_t)no_dest[0] << 16 + (uint8_t)no_dest[1] << 24;
            dest = ntohs(no_dest_int);
            cost = ntohl(new_cost_int);
            std::string path_str(path);
            split(P[dest], cur_split_path, ' ');
            split(path_str, new_split_path, ' ');
            if(cost <= D[dest]){
                if(cost == D[dest]){
                    //TODO: tiebreak
                    int cur_next_step = stoi(cur_split_path[1]);
                    int new_next_step = stoi(new_split_path[0]);
                    if(new_next_step < cur_next_step){
                        P[dest] = path_str;
                        broadcastNewPath(neighbors, D[dest], P[dest], dest);
                    }
                }
                else{
                    path_str = std::to_string(globalMyID) + " " + path_str;
                    P[dest] = path_str;
                    D[dest] = cost;
                    broadcastNewPath(neighbors, D[dest], P[dest], dest);
                }
            }
        }
    }
    //(should never reach here)
    close(globalSocketUDP);
}

