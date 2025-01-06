# P2P File Sharing System
## Architecture description ::
1. Similar to the bittorrent architecture this project uses various peers of a network to parally download different chunks of a file thereby reducing load on any single node 
as in client server architecture .
2. Uses multithreading concepts to contact relevent peers and download chunks of a requested file .. also a filepool is available at every node to keep a cache of open files
 local to a node to reduce disk accesses.
3. Every node has a thread running  a socket to listen and send file (like a server)
4. Using mutex and semaphores dirty reads are avoided in a node(Thread Management)
5. Tracker node is essentially a directory node which keeps a list of ip addresses against a particular file available in a network

## Working ::
1. When a node requests a file over the network it first contacts the tracker node to get the list of ip-addresses with chunks of that file
   the requesting node then prepares a list of ip-address to contact for every chunk of a file . this is done on the "rarest piece first" basis
2. The node then contacts the nessesary peers over different threads to simultanuosly get the chunks of the file and save it in its local directory
3. The node then informs the tracker about the presence of chunks for the requested file so that it too can contribute to the network

### P.S: project at a initial stage
