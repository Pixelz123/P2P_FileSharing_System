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
#include <bits/stdc++.h>
#include <pthread.h>
#include <mutex>
#include <condition_variable>
#include <arpa/inet.h>
#include <vector>
#include <semaphore.h>
#include <map>

using namespace std;
using namespace std::chrono;

#define TR_PORT 2232
#define SERVER_IP "127.0.0.1"
#define CHUNK_SIZE 1024
#define MAX_FNAME 512
#define MAX_REQSIZE 512
#define MAX_FILEPOOL 10

class Filepool
{
    // file pool to store open files and use it as cache
    // this prevent multipe ssd access while sending chunks or writing to a new file
public:
    int pool_size;
    map<string, ifstream> pool;
    Filepool(int size)
    {
        pool_size = size;
    }

    ifstream getfile(string &filename)
    {
        if (pool.find(filename) != pool.end())
        {
            return move(pool[filename]);
        }
        else
        {
            cout << "file not in cache .. encacheing ....\n";
            ifstream file(filename, ios::binary);
            if (!file.is_open())
            {
                cerr << "Error opening file:: " << filename << '\n';
            }
            else
            {
                pool[filename] = move(file);
            }
        }
        return move(pool[filename]);
    }
};
Filepool obj(MAX_FILEPOOL);

int peer_port;
map<string, vector<int>> bit_fields;
mutex pool_mutex;

condition_variable pool_cond;
sem_t file_write_semaphore;

struct th_args
{
    string filename;
    vector<int> ip_list;
    ofstream *file;
    int chunk_no;
};
queue<th_args *> th_pool;

char *getchunk(int server_port, string &filename, int chunk_no)
{
    int sock_fd;
    struct sockaddr_in sock_addr;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "Binding error ...\n";
        return NULL;
    }
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, SERVER_IP, &sock_addr.sin_addr) <= 0)
    {
        cerr << "Connecting issue...\n";
        return NULL;
    }
    if (connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
    {
        cerr << "Failed to connect to server...\n";
        return NULL;
    }
    char *peer_request_type = new char[MAX_REQSIZE];
    strcpy(peer_request_type, "chunk_request");
    send(sock_fd, peer_request_type, MAX_REQSIZE, 0);
    delete (peer_request_type);

    char *fname = new char[MAX_FNAME];
    int cno = chunk_no;
    strcpy(fname, filename.c_str());

    send(sock_fd, fname, MAX_FNAME, 0);
    send(sock_fd, &cno, sizeof(cno), 0);
    delete (fname);

    char *chunk = new char[CHUNK_SIZE];
    if (recv(sock_fd, chunk, CHUNK_SIZE, 0) <= 0)
    {
        return NULL;
    }

    cout << "Received chunk " << chunk_no << " from peer " << server_port << '\n';
    bit_fields[filename].push_back(chunk_no);
    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    return chunk;
}

int reg_file(string &filename, int init = 0, int chunks = 0)
{
    if (init == 1)
    {
        if (bit_fields.count(filename))
            return 0;
        for (int i = 0; i < chunks; i++)
        {
            bit_fields[filename].push_back(i);
        }
    }
    int sock_fd;
    sockaddr_in sockaddr;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "tracker informing fails..";
        return 0;
    }
    sockaddr.sin_port = htons(TR_PORT);
    sockaddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, SERVER_IP, &sockaddr.sin_addr) <= 0)
    {
        cerr << "tracker connect fails...\n";
        return 0;
    }
    if (connect(sock_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
    {
        cerr << "tracker inform fails...\n";
        return 0;
    }
    char *fname = new char[MAX_FNAME];
    strcpy(fname, filename.c_str());
    int port = peer_port + 1;
    int num_chunks = -1;

    char *peer_request = new char[MAX_REQSIZE];
    strcpy(peer_request, "reg_file");
    send(sock_fd, peer_request, MAX_REQSIZE, 0);
    send(sock_fd, fname, MAX_FNAME, 0);
    send(sock_fd, &port, sizeof(int), 0);

    if (init)
    {
        num_chunks = chunks;
        send(sock_fd, &num_chunks, sizeof(int), 0);
    }

    return 1;
}

void *th_function(void *args)
{
    unique_lock<mutex> lock(pool_mutex);
    pool_cond.wait(lock, []
                   { return !th_pool.empty(); });
    th_args *t_args = th_pool.front();
    vector<int> ip_list = t_args->ip_list;
    string fname = t_args->filename;
    int chunk_no = t_args->chunk_no;
    ofstream *file = t_args->file;
    th_pool.pop();
    lock.unlock();

    for (int i = 0, ptr = rand() % ip_list.size(); i < ip_list.size(); i++)
    {
        sem_wait(&file_write_semaphore);
        char *buffer = getchunk(ip_list[ptr], fname, chunk_no);
        if (buffer != NULL)
        {
            file->seekp(CHUNK_SIZE * chunk_no);
            *file << buffer;
            delete (buffer);
            sem_post(&file_write_semaphore);
            return NULL;
        }
        else
        {
            cout << "faulty node ::"<<ip_list[ptr]<<'\n';;
            ptr = (ptr + 1) % ip_list.size();
            sem_post(&file_write_semaphore);
        }
    }

    return NULL;
}

vector<vector<int>> prep_req_list(map<int, vector<int>> &peer_chunk_map, int num_chunks)
{
    vector<vector<int>> req_list(num_chunks);
    for (auto peer : peer_chunk_map)
    {
        for (auto chunk : peer.second)
        {
            req_list[chunk].push_back(peer.first);
        }
    }
    return req_list;
}

char *sendchunk(string &filename, int chunk_no)
{
    // ifstream file;
    ifstream file = obj.getfile(filename);
    char *chunk = new char[CHUNK_SIZE];
    file.seekg(chunk_no * CHUNK_SIZE);
    file.read(chunk, CHUNK_SIZE);
    file.close();
    return chunk;
}

void get_bitfield(string &filename, vector<int> &peer_list, map<int, vector<int>> &peer_chunk_map)
{

    for (auto peer : peer_list)
    {
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            cerr << "Bind error skipping this ip\n";
            continue;
        }
        sockaddr_in sock_addr;
        memset(&sock_addr, 0, sizeof(sock_addr));
        sock_addr.sin_port = htons(peer);
        sock_addr.sin_family = AF_INET;
        if (inet_pton(AF_INET, SERVER_IP, &sock_addr.sin_addr) <= 0)
        {
            cerr << "Invalid peer ip or port... skipping this ip\n";
            continue;
        }
        if (connect(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
        {
            cerr << "Connection issue with IP::" << SERVER_IP << ":" << peer << '\n';
            cout << "Skipping this IP\n";
            continue;
        }
        cout << "Connected to " << SERVER_IP << ":" << peer << ' ' << "for bitfield..\n";

        char *peer_req_type = new char[MAX_REQSIZE];
        strcpy(peer_req_type, "bitfield_request");
        send(sock, peer_req_type, MAX_REQSIZE, 0);

        char *fname = new char[MAX_FNAME];
        strcpy(fname, filename.c_str());
        send(sock, fname, MAX_FNAME, 0);

        int bitfield_size;
        recv(sock, &bitfield_size, sizeof(bitfield_size), 0);

        vector<int> bitfield(bitfield_size);
        recv(sock, bitfield.data(), sizeof(int) * bitfield_size, 0);
        peer_chunk_map[peer] = bitfield;
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
}

void *peer_server(void *args)
{
    int *port = (int *)args;
    int SERVER_PORT = *port;
    SERVER_PORT++;
    int sock_fd, ac_sock;
    struct sockaddr_in sock_addr, ac_addr;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cerr << "Socket fails...\n";
        return 0;
    }
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port = htons(SERVER_PORT);

    if (bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
    {
        cout << "Binding fails..." << '\n';
        return 0;
    }
    if (listen(sock_fd, 2000))
    {
        cout << ":Listen error" << '\n';
        return 0;
    }
    string filename = "";
    cout << "\nlistening on port  " << SERVER_PORT << '\n';
    int ac_sock_len;
    while (1)
    {
        bzero((char *)&ac_addr, sizeof(ac_addr));
        ac_sock_len = sizeof(ac_addr);
        ac_sock = accept(sock_fd, (struct sockaddr *)&ac_addr, (socklen_t *)&ac_sock_len);
        if (ac_sock < 0)
        {
            cerr << "Bad request ...\n";
            pthread_exit(NULL);
        }

        char *peer_request_type = new char[MAX_REQSIZE];
        recv(ac_sock, peer_request_type, MAX_REQSIZE, 0);

        cout << "Received request type ::" << peer_request_type << '\n';

        if (strcmp(peer_request_type, "chunk_request") == 0)
        {
            char *fname = new char[MAX_FNAME];
            int cno;
            recv(ac_sock, fname, MAX_FNAME, 0);
            recv(ac_sock, &cno, sizeof(cno), 0);

            string filename(fname);

            char *buffer = new char[1024];
            buffer = sendchunk(filename, cno);
            send(ac_sock, buffer, 1024, 0);
            if (buffer != NULL)
                cout << "sent sucessfully... " << "chunk ::" << cno << '\n';
            else
            {
                cout << "chunk not sent ...\n";
                pthread_exit(NULL);
            }
            // memory management
            delete (fname);
            delete (buffer);
        }
        else if (strcmp(peer_request_type, "bitfield_request") == 0)
        {
            char *fname = new char[MAX_FNAME];
            recv(ac_sock, fname, MAX_FNAME, 0);

            int size = bit_fields[fname].size();
            send(ac_sock, &size, sizeof(size), 0);

            send(ac_sock, bit_fields[fname].data(), sizeof(int) * size, 0);
            delete (fname);
        }
        else
        {
            cerr << "Bad request type from peer\n";
            pthread_exit(NULL);
        }
        delete (peer_request_type);
        shutdown(ac_sock, SHUT_RDWR);
        close(ac_sock);
    }
    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);
    return NULL;
}

vector<int> getiplist(string &filename)
{

    int trac_fd;
    sockaddr_in sockaddr;
    // establish connection with tracker
    if ((trac_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        cout << "Binding (for tracker) fails..\n";
    }
    sockaddr.sin_port = htons(TR_PORT);
    sockaddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, SERVER_IP, &sockaddr.sin_addr) <= 0)
    {
        cerr << "Connecting with tracker fails ...\n";
    }
    if (connect(trac_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
    {
        cerr << "Failed to connect to tracker...\n";
    }

    int listsize;
    char *peer_request = new char[MAX_REQSIZE];
    strcpy(peer_request, "iplist");
    send(trac_fd, peer_request, MAX_REQSIZE, 0);
    send(trac_fd, filename.c_str(), MAX_FNAME, 0);
    recv(trac_fd, &listsize, sizeof(int), 0);
    vector<int> iplist(listsize); // iplist[0] is basicalliy num_chunks
    recv(trac_fd, iplist.data(), sizeof(int) * listsize, 0);
    close(trac_fd);
    return iplist;
}

int main(int argv, char *argc[])
{
    //    implement the threadpool concept instead of loop ing and creating thread for every chunk...
    pthread_t pserver;
    peer_port = atoi(argc[1]);
    pthread_create(&pserver, NULL, peer_server, &peer_port);
    pthread_detach(pserver);

    sem_init(&file_write_semaphore, 0, 1);
    string cmd;
    while (1)
    {
        cout << ">>";
        getline(cin, cmd);

        if (cmd.substr(0, 7) == "getfile")
        {
            auto start = chrono::high_resolution_clock::now();

            string filename = cmd.substr(8);
            ofstream file("kf1.txt");

            vector<int> iplist = getiplist(filename);
            int num_chunks = iplist.back();
            iplist.pop_back();

            vector<pthread_t> tid(num_chunks);
            cout << "Requested for file :" << filename << '\n';

            map<int, vector<int>> peer_chunk_map;
            get_bitfield(filename, iplist, peer_chunk_map);

            vector<vector<int>> req_list;
            req_list = prep_req_list(peer_chunk_map, num_chunks);
            sort(req_list.begin(), req_list.end(), [&](vector<int> &x, vector<int> &y)
                 { return x.size() < y.size(); });

            bool received_chunk = false;

            for (int chunk = 0; chunk < num_chunks; chunk++)
            {
                th_args *args = new th_args;
                args->chunk_no = chunk;
                args->filename = filename;
                args->ip_list = req_list[chunk];
                args->file = &file;
                th_pool.push(args);
            }

            for (int chunk = 0; chunk < num_chunks; chunk++)
            {

                pthread_create(&tid[chunk], NULL, th_function, NULL);
                if (!received_chunk)
                {
                    received_chunk = true;
                    reg_file(filename);
                }
            }
            auto end = chrono::high_resolution_clock::now();
            for (int i = 0; i < num_chunks; i++)
            {
                pthread_join(tid[i], NULL);
            }
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            sleep(2);
            file.close();
            cout << "Time taken(ns)::" << duration.count() << "\n";
            tid.clear();
        }
        else if (cmd.substr(0, 7) == "regfile")
        {
            string filename = cmd.substr(8);
            if (!reg_file(filename, 1, 957))
            {
                cout << "file already in local...\n";
            }
            else
            {
                cout << "sucessfully registered " << filename << " for p2p sharing ...\n";
            }
        }
        else if (cmd.substr(0, 4) == "getb")
        {
            string fname = cmd.substr(5);

            cout << bit_fields[fname].size() << '\n';
        }
        else if (cmd == "exit")
            exit(0);
    }
}