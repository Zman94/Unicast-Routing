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
        char send_message[1000];
        int msgLen = 4+sizeof(short int)+4+sizeof(int)+1+path_to_node.length();
        short int no_neighbor = htons(heardFrom);
        int no_cost = htonl(cost_to_node);
        // std::string sendBuf = "path" + std::to_string(no_neighbor) + "cost" + std::to_string(cost_to_node)+";"+path_to_node;
        strcpy(send_message, "path");
        memcpy(send_message+4, &no_neighbor, sizeof(short int));
        strcpy(send_message+4+sizeof(short int), "cost");
        memcpy(send_message+4+sizeof(short int)+4, &no_cost, sizeof(int));
        strcpy(send_message+4+sizeof(short int)+4+sizeof(int), ";");
        memcpy(send_message+4+sizeof(short int)+4+sizeof(int)+1, path_to_node.c_str(), path_to_node.length());

        sendto(globalSocketUDP, send_message, msgLen, 0,
            (struct sockaddr*)&globalNodeAddrs[neighbors[i]], sizeof(globalNodeAddrs[neighbors[i]]));
    }
    return;
}

void checkNeighborAvailability(short int neighbors[], int D[], std::string P[], timeval globalLastHeartbeat[], FILE* theLogFile){
    struct timeval cur_time;
    gettimeofday(&cur_time, 0);
    for(int i=0; i<255; i++){
        if(neighbors[i] == -1)
            break;
        // Unavailable after 3 seconds
        // TODO: revert back to 3 seconds
        if(cur_time.tv_sec - globalLastHeartbeat[neighbors[i]].tv_sec >= 30){
            // TODO: check every path to see if it uses our lost neighbor. Alert about all paths
            // Use neighbors list
            for(int j=0; j<255; j++){
                if(neighbors[j] == -1)
                    break;
                std::vector<std::string> cur_split_path;
                split(P[neighbors[j]], cur_split_path, ' ');
                for(int k=0; k<cur_split_path.size(); k++){
                    if(stoi(cur_split_path[k]) == neighbors[i]){ //path goes through bad node
                        if(D[neighbors[j]] >= 0){
                            D[neighbors[j]] = D[neighbors[j]]*-1;
                            char logLine[100];
                            broadcastNewPath(neighbors, D[neighbors[j]], P[neighbors[j]], neighbors[j]);
                            break;
                        }
                    }
                }
            }
            // remove the neighbor
            int j = i+1;
            for(;j<255; j++){ // Find next -1
                if(neighbors[j] == -1)
                    break;
            }
            neighbors[i] = neighbors[j-1]; // Remove value at i (no longer neighbor)
            neighbors[j-1] = -1;
            i--;
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

void listenForNeighbors(char* logFile, int D[], std::string P[], int initialCosts[])
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
        neighbors[i] = -1;
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
        char logLine[100];
        std::vector<std::string> cur_split_path;
        std::vector<std::string> new_split_path;
        if(strstr(fromAddr, "10.1.1."))
        {
            heardFrom = atoi(
                    strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
            
            // Update neighbors list
            for(int i=0; i<255; i++) {
                if(neighbors[i] == heardFrom)
                    break;
                if(neighbors[i] == -1) {
                    // Have to broadcast our paths to the neighbor
                    neighbors[i] = heardFrom; 
                    for(int j=0; j<255; j++){
                        if(neighbors[j] == heardFrom)
                            break;
                        broadcastNewPath(neighbors, D[neighbors[j]], P[neighbors[j]], neighbors[j]);
                    }
                    break;
                }
            }

            std::string path_to_node = "";
            if(P[heardFrom].length() == 0){
                path_to_node = std::to_string(globalMyID) + " " + std::to_string(heardFrom);
                P[heardFrom] = path_to_node;
                if(D[heardFrom] < 0){
                    std::cout << "D[heardFrom] " << D[heardFrom] << std::endl;
                    D[heardFrom] = initialCosts[heardFrom];
                }
                broadcastNewPath(neighbors, D[heardFrom], P[heardFrom], heardFrom);
            }
            else if(D[heardFrom] < 0){
                D[heardFrom] = initialCosts[heardFrom];
                path_to_node = std::to_string(globalMyID) + " " + std::to_string(heardFrom);
                P[heardFrom] = path_to_node;
                broadcastNewPath(neighbors, D[heardFrom], P[heardFrom], heardFrom);
            }
            else if(D[heardFrom] >= initialCosts[heardFrom]){
                if(D[heardFrom] == initialCosts[heardFrom]){
                    split(P[heardFrom], cur_split_path, ' ');
                    int cur_next_step = stoi(cur_split_path[1]);
                    if(heardFrom < cur_next_step){ // if path directly to node is better
                        path_to_node = std::to_string(globalMyID) + " " + std::to_string(heardFrom);
                        P[heardFrom] = path_to_node;
                        broadcastNewPath(neighbors, D[heardFrom], P[heardFrom], heardFrom);
                    }
                }
                else{ // direct connect is better
                    std::cout << "direct connect better heardFrom " << heardFrom << " P[heardFrom] " << P[heardFrom] << " D[heardFrom] " << D[heardFrom] << " initialCosts[heardFrom] " << initialCosts[heardFrom] << std::endl;
                    path_to_node = std::to_string(globalMyID) + " " + std::to_string(heardFrom);
                    P[heardFrom] = path_to_node;
                    D[heardFrom] = initialCosts[heardFrom];
                    broadcastNewPath(neighbors, D[heardFrom], P[heardFrom], heardFrom);
                }
            }
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }

        checkNeighborAvailability(neighbors, D, P, globalLastHeartbeat, theLogFile); //Check for unavailable nodes
        
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
            int next_dest;
            split(P[dest], cur_split_path, ' ');
            if(cur_split_path.size() == 1){
                next_dest = globalMyID;
            }
            else{
                next_dest = stoi(cur_split_path[1]);
            }
            if(dest == globalMyID){
                sprintf(logLine, "receive packet message %s\n", message); 
                std::cout << logLine;
                fwrite(logLine, 1, strlen(logLine), theLogFile);
                fflush(theLogFile);
                continue; // don't send packet if at dest
            }
            else if(D[dest] < 0){
                sprintf(logLine, "unreachable dest %d\n", dest); 
            }
            else if(heardFrom > 0){                    
                sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", dest, next_dest, message); 
            }
            else{
                sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", dest, next_dest, message); 
            }

            std::cout << logLine;
            fwrite(logLine, 1, strlen(logLine), theLogFile);
            fflush(theLogFile);

            short int no_destID = htons(dest);
            strcpy(send_message, "send");
            memcpy(send_message+4, &no_destID, sizeof(short int));
            memcpy(send_message+4+sizeof(short int), message, messageBytes);
            int msgLen = 4+sizeof(short int)+messageBytes;

            sendto(globalSocketUDP, send_message, msgLen, 0,
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
            no_dest_int = (uint8_t)no_dest[0] + ((uint8_t)no_dest[1] << 8);
            new_cost_int = (uint8_t)new_cost[0] + ((uint8_t)new_cost[1] << 8) + ((uint8_t)new_cost[2] << 16) + ((uint8_t)new_cost[3] << 24);
            dest = ntohs(no_dest_int);
            cost = ntohl(new_cost_int);
            std::string path_str(path, messageBytes);
            split(P[dest], cur_split_path, ' ');
            split(path_str, new_split_path, ' ');
            if(cost >= 0){ // new path is valid
                if(D[dest] < 0){ // Currently have no path
                    D[dest] = cost + initialCosts[heardFrom]; // new path
                    path_str = std::to_string(globalMyID) + " " + path_str;
                    P[dest] = path_str;
                }
                else{ // Both paths are valid
                    cost = cost + initialCosts[heardFrom];
                    if(cost <= D[dest]){
                        if(cost == D[dest]){ // tiebreak
                            //TODO: tiebreak
                            int cur_next_step = stoi(cur_split_path[1]);
                            int new_next_step = stoi(new_split_path[0]);
                            if(new_next_step < cur_next_step){
                                path_str = std::to_string(globalMyID) + " " + path_str;
                                P[dest] = path_str;
                            }
                            else{ // our path is better
                                continue;
                            }
                        }
                        else{
                            path_str = std::to_string(globalMyID) + " " + path_str;
                            P[dest] = path_str;
                            D[dest] = cost;
                        }
                    }
                    else{ // D[dest] < cost
                        continue;
                    }
                }
            }
            else{ // new path is invalid
                // check if our path has heardFrom in it
                // if yes, send new cost - D[heardFrom] to neighbors
                // else send our path to neighbors
                if(D[dest] < 0) // no valid path // shouldn't get here
                    continue;
                bool found_path = false;
                if(cur_split_path.size() < 1)
                    continue;
                for(int i=0; i<cur_split_path.size(); i++){
                    if(stoi(cur_split_path[i]) == heardFrom){ // no valid path
                        if(D[dest]*-1 > cost-initialCosts[heardFrom]){ // current path would be better
                            D[dest] = D[dest]*-1;
                        }
                        else{
                            D[dest] = cost-initialCosts[heardFrom];
                            P[dest] = std::to_string(globalMyID) + " " + path_str;
                        }
                        break;
                    }
                }
            }
            broadcastNewPath(neighbors, D[dest], P[dest], dest);
        }
    }
    //(should never reach here)
    close(globalSocketUDP);
}

