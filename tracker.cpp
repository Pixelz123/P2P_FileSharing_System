#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <mutex>
#include <condition_variable>
#include <arpa/inet.h>
#include <vector>
#include <map>

#define MAX_FNAME 512
#define MAX_REQSIZE 512
#define MAX_CLIENT 2000

using namespace std;
map<string, vector<int>> file_peerlist; // structure filename : ip also send a filesoze...

void *sendiplist(void *args)
{ // READER part of the reader writer problem concurrency ...
    
    int *sock_ac_ptr = (int *)args;
    int sock_ac = *sock_ac_ptr;
    // ================ reader part ===========//
    char *fname = new char[MAX_FNAME];

    recv(sock_ac, fname, MAX_FNAME, 0);
    int listsize = file_peerlist[fname].size();
    send(sock_ac, &listsize, sizeof(int), 0);

    vector<int> iplist = file_peerlist[fname];

    send(sock_ac, iplist.data(), sizeof(int) * listsize, 0);
    close(sock_ac);
    delete (fname);
    // ================ reader part ==========//

    return NULL;
}

void *reg_file(void *args)
{
    int *sock_fd_ptr = (int *)args;
    int sock_fd = *sock_fd_ptr;

    char *fname = new char[MAX_FNAME];
    int port;
    int num_chunks = -1;

    // =======writer part============//
    recv(sock_fd, fname, MAX_FNAME, 0);
    recv(sock_fd, &port, sizeof(int), 0);

    if (file_peerlist[fname].size() == 0)
    {
        recv(sock_fd, &num_chunks, sizeof(int), 0);
    }

    if (file_peerlist[fname].size())
    {
        num_chunks = file_peerlist[fname].back();
        file_peerlist[fname].pop_back();
    }

    file_peerlist[fname].push_back(port);

    if (num_chunks != -1)
        file_peerlist[fname].push_back(num_chunks);
    for (auto i: file_peerlist){
        cout <<"\n============"<<i.first<<"=======================\n";
        for(auto j:i.second){
            cout <<j<<" ";
        }
        cout<<"\n==========================================\n";
    }

    //=========writer part===========//

    return NULL;
}

int main(int argv, char *argc[])
{
    int port = atoi(argc[1]);
    int sock_fd;
    sockaddr_in sock_addr, sock_ac_addr;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "Socket creation fails...\n";
        return 0;
    }
    sock_addr.sin_port = htons(port);
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
    {
        cerr << "Binding fails...\n";
        return 0;
    }
    if (listen(sock_fd, 13))
    {
        cerr << "Listening fails...\n";
        return 0;
    }
    cout << "Tracker listening on port " << port << '\n';
    int sock_ac;
    int sock_ac_addr_len;

    pthread_t tid[MAX_CLIENT];
    int peer_sock[MAX_CLIENT];
    int ptr = 0;

    while (1)
    {
        memset(&sock_ac_addr, 0, sizeof(sock_ac_addr));
        sock_ac_addr_len = sizeof(sock_ac_addr);
        sock_ac = accept(sock_fd, (struct sockaddr *)&sock_ac_addr, (socklen_t *)&sock_ac_addr_len);
        if (sock_ac < 0)
        {
            cout << "Accepting failure ...\n";
        }
        else
        {

            char *request_type = new char[MAX_REQSIZE];
            recv(sock_ac, request_type, MAX_REQSIZE, 0);

            if (strcmp(request_type, "iplist") == 0)
            {
                peer_sock[ptr] = sock_ac;
                pthread_create(&tid[ptr], NULL, sendiplist, (void *)&peer_sock[ptr]);
                ptr++;
            }
            else if (strcmp(request_type, "trac_connect") == 0)
            {
                cout << "connection request...\n";
            }
            else if (strcmp(request_type, "reg_file") == 0)
            {
                peer_sock[ptr] = sock_ac;
                pthread_create(&tid[ptr], NULL, reg_file, (void *)&peer_sock[ptr]);
                ptr++;
            }
            else
            {
                cout << "Invalid request\n";
            }
            delete (request_type);
        }
    }
    close(sock_fd);
    return 1;
}