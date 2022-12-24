#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <sstream>
#include <utility>
#include <string.h>
#include <stdlib.h>

#include <thread>
#include <mutex>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

#define PORT 8080

#define BLOCK_SIZE 16
#define TOTAL_MEMORY 256

struct CONTENT {
    char content[BLOCK_SIZE];
    bool used;
};

struct FDIR {
    string name;
    int size;
    bool type;                      // 0 for dir and 1 for file
    int numberOfChildren;
    vector<FDIR*> childrens;
    FDIR* parent;

    int numberOfBlocks;
    vector<int> externals;

    FDIR(string name, bool type, FDIR* parent) {
        this->name = name;
        this->type = type;
        this->parent = parent;
    }
};

// blocks vector with fixed size
vector<pair<bool, string>> blocks(TOTAL_MEMORY/BLOCK_SIZE);

// freed block list
vector<int> freedBlockList;

int freeBlock = 0;

// keeps track of the free block

FDIR *root;
FDIR* createTree();
FDIR *x = createTree();

bool hasReachedEnd = false;

// mutex gLock;

void getFreeBlockIndex();

struct FileObject {
    FDIR* file;
    char mode;
    string name;

    FileObject(FDIR* file, char mode, string name) {
        this->file = file;
        this->mode = mode;
        this->name = name;
    }

    void append(string data, vector<FileObject*>& openFileTable) {
        bool flag = false;
        for (int i = 0; i < openFileTable.size(); ++i) {
            if (openFileTable[i]->name == this->name && (openFileTable[i]->mode == 'w' || openFileTable[i]->mode == 't')) {
                // gLock.lock();
                flag = true;
                break;
            }
        }
        int dataLength = data.length();

        int lastBlock = this->file->externals[this->file->externals.size() - 1];
        int spaceLeftInLastBlock = BLOCK_SIZE - blocks[lastBlock].second.length();

        if (spaceLeftInLastBlock >= data.length()) {
            blocks[lastBlock].second += data;
        }
        else {
            // find how much data can we fit in last block
            blocks[lastBlock].second += data.substr(0, spaceLeftInLastBlock);
            dataLength -= spaceLeftInLastBlock;
            
            while (true) {
                if (freeBlock == -1) {
                    cout << "Ran out of space\n";
                    return;
                }
                blocks[freeBlock].first = true;
                if (dataLength > BLOCK_SIZE) {
                    blocks[freeBlock].second += data.substr(spaceLeftInLastBlock, BLOCK_SIZE);
                    this->file->externals.push_back(freeBlock);
                    getFreeBlockIndex();
                    dataLength -= BLOCK_SIZE;
                    spaceLeftInLastBlock = spaceLeftInLastBlock + BLOCK_SIZE;
                    this->file->numberOfBlocks++;
                }
                else {
                    blocks[freeBlock].second += data.substr(spaceLeftInLastBlock, dataLength);
                    this->file->externals.push_back(freeBlock);
                    getFreeBlockIndex();
                    this->file->numberOfBlocks++;
                    break;
                }
            }
        }

        int oldSize = this->file->size;
        // subtract old file size
        this->file->parent->size -= oldSize;

        // calculate new file size
        this->file->size = (this->file->externals.size() - 1) * BLOCK_SIZE + blocks[this->file->externals[this->file->externals.size() - 1]].second.length();

        // add new file size
        this->file->parent->size += this->file->size;

        // traverse to parent
        FDIR* tmp = this->file->parent->parent;

        while (tmp != NULL) {
            tmp->size -= oldSize;
            tmp->size += this->file->size;
            tmp = tmp->parent;
        }

        // if (flag) {gLock.unlock();}
    }

    string read() {
        string data = "";
        cout << this->file->name << endl;

        for (int i = 0; i < this->file->externals.size(); ++i) {
            
            data += blocks[this->file->externals[i]].second;
        }

        return data;
    }

    void writeAt(vector<pair<bool, string>>& blocks, int& freeBlock, vector<int>& freedBlockList, string data, int writePos) {
        string original = this->read();

        if (writePos + data.length() > original.length()) {
            cout << "Out of length for file\n";
            return;
        }

        original.replace(writePos, data.length(), data);

        int tmp = 0;
        int dataLeft = original.length();
        for (int i = 0; i < this->file->externals.size() - 1; ++i) {
            blocks[this->file->externals[i]].second = original.substr(tmp, BLOCK_SIZE);
            tmp += BLOCK_SIZE;
            dataLeft -= BLOCK_SIZE;
        }

        blocks[this->file->externals[this->file->externals.size() - 1]].second = original.substr(tmp, dataLeft);
    }

    string readFrom(vector<pair<bool, string>>& blocks, int readPos, int readSize) {
        string data = "";

        for (int i = 0; i < this->file->externals.size(); ++i) {
            data += blocks[this->file->externals[i]].second;
        }

        return data.substr(readPos, readSize);
    }

    void moveWithin(vector<pair<bool, string>>& blocks, int from, int to, int size) {
        string original = this->read();
        if (from + size > original.length()) {
            cout << "Out of length for file\n";
            return;
        }

        string sub = original.substr(from, size);
        original.replace(from, size, "");
        
        string sub2 = original.substr(0, to);
        sub2 = sub2 + sub + original.substr(to, original.length() - to);
        
        original = sub2;

        int tmp = 0;
        int dataLeft = original.length();
        for (int i = 0; i < this->file->externals.size() - 1; ++i) {
            blocks[this->file->externals[i]].second = original.substr(tmp, BLOCK_SIZE);
            tmp += BLOCK_SIZE;
            dataLeft -= BLOCK_SIZE;
        }
        blocks[this->file->externals[this->file->externals.size() - 1]].second = original.substr(tmp, dataLeft);
    }

    void truncateFile(int size, vector<FileObject*>& openFileTable) {
        bool flag = false;
        for (int i = 0; i < openFileTable.size(); ++i) {
            if (openFileTable[i]->name == this->name && (openFileTable[i]->mode == 'w' || openFileTable[i]->mode == 't')) {
                // gLock.lock();
                flag = true;
                break;
            }
        }
        string original = this->read();
        int originalSize = original.length();

        // truncate
        original = original.substr(0, size);

        int blocksReq = (original.length() / BLOCK_SIZE) + 1;

        int tmp = 0;
        int dataLeft = original.length();
        for (int i = 0; i < blocksReq - 1; ++i) {
            blocks[this->file->externals[i]].second = original.substr(tmp, BLOCK_SIZE);
            tmp += BLOCK_SIZE;
            dataLeft -= BLOCK_SIZE;
        }
        blocks[this->file->externals[blocksReq - 1]].second = original.substr(tmp, dataLeft);

        int sizeOfExternals = this->file->externals.size();
        for (int j = blocksReq; j < sizeOfExternals; ++j) {
            blocks[this->file->externals[j]].first = false;
            blocks[this->file->externals[j]].second = "";
            freedBlockList.push_back(this->file->externals[j]);
        }

        for (int j = blocksReq; j < sizeOfExternals; ++j) {
            this->file->externals.pop_back();
        }

        this->file->parent->size -= this->file->size;
        this->file->parent->size += original.length();
        FDIR* p = this->file->parent->parent;

        while (p != NULL) {
            p->size -= this->file->size;
            p->size += original.length();
            p = p->parent;
        }

        this->file->size = original.length();
        this->file->numberOfBlocks = this->file->externals.size();
        
        // if (flag) {gLock.unlock();}
    }
};

vector<FileObject*> openFileTable;

// utilities
void bfs(FDIR *);
FDIR* reconstruct();
void freeTree(FDIR*);
string getPath(FDIR*);
void tokenizePath(string, vector<string>&);

void writeContent(vector<pair<bool, string>>&);
void readContent(vector<pair<bool, string>>&);

// lab functions
FDIR* chDir(FDIR*, FDIR*, vector<string>&, int);
FDIR* chDir(FDIR*, FDIR*, string);

void mkDir(string, FDIR*);
void createFile(string, FDIR*);
void deleteFile(string, FDIR*);
void moveFile(string, string, FDIR*);

FileObject* openFile(string, char, FDIR*);
void closeFile(string path);

void displayFileInfo(FDIR*, string&);

void memoryMap(FDIR*, int, string&);

FileObject* findInOpenFileTable(string name) {
    FileObject *file = NULL;
    for (int i = 0; i < openFileTable.size(); ++i) {
        if (openFileTable[i]->name == name) {
            file = openFileTable[i];
        }
    }
    return file;
}

FileObject* write_to_file(string name, string data, vector<FileObject*>& openFileTable) {
    FileObject *file = findInOpenFileTable(name);
    
    if (file != NULL && file->mode == 'w') {
        file->append(data, openFileTable);
        return file;
    }
    else {
        return NULL;
    }
}

FileObject* read_from_file(string name) {
    FileObject *file = findInOpenFileTable(name);
    return file;
}

FileObject* truncate_file(string name, int size, vector<FileObject*>& openFileTable) {
    FileObject *file = findInOpenFileTable(name);
    
    if (file != NULL && file->mode == 't') {
        file->truncateFile(size, openFileTable);
        return file;
    }
    else {
        return file;
    }
}

void menu(int new_socket, FDIR* currentDirectory) {
    cout << "Connection Established: " << new_socket << endl;
    
    while(1) {
        char line[1024] = {0};
        int valread = read(new_socket, line, 1024);
        stringstream ss(line);
        string tmp;
        vector<string> tmps;
    
        while(getline(ss, tmp, ' ')) {
            tmps.push_back(tmp);
        }    

        string command = tmps[0];
        
        if (command == "create") {
            string path = tmps[1];
            createFile(path, currentDirectory);

            string res = "File Created Successfully!";
            const char *response = res.c_str();
            send(new_socket, response, strlen(response), 0);
        }
        else if (command == "open") {
            string path = tmps[1];
            char mode = tmps[2].c_str()[0];
            FileObject* file = openFile(path, mode, currentDirectory);

            if (file != NULL) {
                string res = "File Opened Successfully!";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
            else {
                string res = "File Already Open";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
        }
        else if (command == "close") {
            string path = tmps[1];
            closeFile(path);

            string res = "File Closed Successfully!";
            const char *response = res.c_str();
            send(new_socket, response, strlen(response), 0);
        }
        else if (command == "move") {
            string srcPath = tmps[1];
            string destPath = tmps[2];

            moveFile(srcPath, destPath, currentDirectory);

            string res = "File Moved Successfully!";
            const char *response = res.c_str();
            send(new_socket, response, strlen(response), 0);
        }
        else if (command == "delete") {
            string path = tmps[1];
            closeFile(path);

            deleteFile(path, currentDirectory);
            
            string res = "File Deleted Successfully!";
            const char *response = res.c_str();
            send(new_socket, response, strlen(response), 0);
        }
        else if (command == "chdir") {
            string path = tmps[1];
            currentDirectory = chDir(root, currentDirectory, path);

            string res = "Changed directory to " + getPath(currentDirectory);
            const char *response = res.c_str();
            send(new_socket, response, strlen(response), 0);
        }
        else if (command == "write_to_file") {
            string path = tmps[1];
            string data = tmps[2];
            
            vector<string> tokens;
            tokenizePath(path, tokens);
            string name = tokens[tokens.size() - 1];

            FileObject *file = write_to_file(name, data, openFileTable);

            if (file == NULL) {
                string res = "Either the file is not open or it is being used by another client";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
            else {
                string res = "Wrote to file!";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
        }
        else if (command == "read_from_file") {
            string path = tmps[1];
            
            vector<string> tokens;
            tokenizePath(path, tokens);
            string name = tokens[tokens.size() - 1];

            if (findInOpenFileTable(name) == NULL) {
                FDIR *tmp = chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
                FileObject* file = new FileObject(tmp, 'r', name);
                string res = "Read from file: " + file->read();
                cout << "die" << endl;
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
            else {
                cout << "here" << endl;
                string res = "File is opened for either write or truncate. Read operation not allowed in these conditions";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
        }
        else if (command == "truncate_file") {
            string path = tmps[1];
            int size = stoi(tmps[2]);

            vector<string> tokens;
            tokenizePath(path, tokens);
            string name = tokens[tokens.size() - 1];

            FileObject *file = truncate_file(name, size, openFileTable);

            if (file == NULL) {
                string res = "Either the file is not open or it is being used by another client";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);    
            }
            else {
                string res = "Truncated file!";
                const char *response = res.c_str();
                send(new_socket, response, strlen(response), 0);
            }
        }
        else if (command == "show_memory_map") {
            string s;
            memoryMap(root, 0, s);

            string c;
            displayFileInfo(root, c);

            string r = s + "\n" + c;

            const char *response = r.c_str();
            send(new_socket, response, strlen(response), 0);
        }
    }

    close(new_socket);
}

int main() {
    FDIR *currentDirectory = root;

    int server_fd;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);

	// Creating socket file descriptor
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	// Forcefully attaching socket to the port 8080
	if (setsockopt(server_fd, SOL_SOCKET,
				SO_REUSEADDR | SO_REUSEPORT, &opt,
				sizeof(opt))) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	// Forcefully attaching socket to the port 8080
	if (bind(server_fd, (struct sockaddr*)&address,
			sizeof(address))
		< 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, 3) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	vector<thread> ts;
	while(1) {
		int new_socket;
		if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}

		ts.push_back(thread(menu, new_socket, currentDirectory));
		cout << "Size of thread array: " << ts.size() << endl;
	}

	// join threads
	for (int i = 0; i < ts.size(); ++i) {
		ts[i].join();
	}

	// shutdown(server_fd, SHUT_RDWR);
    
    return 0;
}

FDIR* createTree() {
    root = new FDIR("root", 0, NULL);

    mkDir("dir1", root);
    mkDir("dir2", root);
    mkDir("dir3", root);
    mkDir("dir1/dir4", root);
    mkDir("dir1/dir4/dir7", root);
    mkDir("dir3/dir5", root);
    mkDir("dir3/dir5/dir6", root);

    createFile("f1", root);
    createFile("f2", root);
    createFile("f3", root);
    createFile("dir1/f4", root);
    createFile("dir2/f5", root);
    createFile("dir3/dir5/dir6/f6", root);

    return root;
}

void freeTree(FDIR *root) {
    if (root != NULL) {
        for (int i = 0; i < root->numberOfChildren; ++i) {
            freeTree(root->childrens[i]);
        }
        delete root;
        root = NULL;
    }
}

void bfs(FDIR* root) {
    ofstream structFile;
    structFile.open("structFile.txt");

    queue<FDIR*> q;
    FDIR *p = root;

    if (p != NULL){
        q.push(p);
    }
    while (!q.empty()){
        p = q.front();

        string tmp = "";
        for (int i = 0; i < p->externals.size(); ++i) {
            tmp = tmp + "," + to_string(p->externals[i]);
        }

        structFile << p->name << ',' << p->size << ',' << (int) p->type << ',' << p->childrens.size() << ',' << p->numberOfBlocks << tmp << "\n";
        q.pop();

        for (int i = 0; i < p->childrens.size(); ++i){
            if (p->childrens[i] != NULL) {
                q.push(p->childrens[i]);
            }
        }
    }

    structFile.close();
}

FDIR* reconstruct() {
    queue<FDIR*> q;

    ifstream structFile;
    structFile.open("structFile.txt");

    string line;
    getline(structFile, line);
    stringstream ss(line);
    string tmp;
    vector<string> tmps;
    
    while(getline(ss, tmp, ',')) {
        tmps.push_back(tmp);
    }

    FDIR *current = new FDIR(tmps[0], (bool) stoi(tmps[2]), NULL);
    current->size = stoi(tmps[1]);
    current->numberOfChildren = stoi(tmps[3]);
    current->numberOfBlocks = 0;

    FDIR *root = current;

    int count = 0;
    while(getline(structFile, line)) {
        stringstream ss(line);
        string tmp;
        vector<string> tmps;

        while(getline(ss, tmp, ',')) {
            tmps.push_back(tmp);
        }

        FDIR *p = new FDIR(tmps[0], (bool) stoi(tmps[2]), NULL);
        p->size = stoi(tmps[1]);
        p->numberOfChildren = stoi(tmps[3]);
        p->numberOfBlocks = stoi(tmps[4]);

        for (int k = 5; k < 5 + p->numberOfBlocks; ++k) {
            p->externals.push_back(stoi(tmps[k]));
        }

        p->parent = current;
        
        current->childrens.push_back(p);

        if (p->type == 0 && p->numberOfChildren > 0) {q.push(p);}

        ++count;

        if (count == current->numberOfChildren) {
            count = 0;
            current = q.front();
            q.pop();
        }
    }

    structFile.close();
    return root;
}

void mkDir(string path, FDIR* currentDirectory) {
    // tokenize path
    vector<string> tokens;
    tokenizePath(path, tokens);

    // remove last element
    string name = tokens[tokens.size() - 1];
    tokens.pop_back();

    // change directory
    if (tokens.size() != 0) {
        currentDirectory = chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
    }

    // create new directory
    FDIR* newDir = new FDIR(name, 0, currentDirectory);
    newDir->size = 0;
    newDir->numberOfChildren = 0;

    newDir->numberOfBlocks = 0;

    // add and increment the children of parent
    currentDirectory->childrens.push_back(newDir);
    currentDirectory->numberOfChildren++;
}

void createFile(string path, FDIR* currentDirectory) {
    if (freeBlock == -1) {
        cout << "No storage space left\n";
        return;
    }
    // tokenize path
    vector<string> tokens;
    tokenizePath(path, tokens);

    // remove last element
    string name = tokens[tokens.size() - 1];
    tokens.pop_back();

    // change directory
    if (tokens.size() != 0) {
        currentDirectory = chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
    }

    // create new file
    FDIR* newDir = new FDIR(name, 1, currentDirectory);
    newDir->size = 0;
    newDir->numberOfChildren = 0;
    
    newDir->numberOfBlocks = 1;

    // gLock.lock();

    cout << "Creating FILE " << name << endl;
    newDir->externals.push_back(freeBlock); // reads freeBlock
    blocks[freeBlock].first = true;         // modifies blocks
    getFreeBlockIndex();                    // modifies freeBlock and freeBlockList

    if (freeBlock == TOTAL_MEMORY / BLOCK_SIZE) {
        hasReachedEnd = true;               // modifies hasReachedEnd
        getFreeBlockIndex();                // modifies freeBlock and freeBlockList
    }
    

    // add and increment the children of parent
    currentDirectory->childrens.push_back(newDir);
    currentDirectory->numberOfChildren++;
    
    // memoryMap(root, 0);
    // gLock.unlock();
}

void deleteFile(string path, FDIR* currentDirectory) {
    // tokenize path
    vector<string> tokens;
    tokenizePath(path, tokens);

    // remove last element
    string name = tokens[tokens.size() - 1];
    tokens.pop_back();

    if (path[0] == '/' && tokens.size() == 1) {currentDirectory = root;}

    // change directory
    else if (tokens.size() != 0) {
        currentDirectory = chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
    }

    // place lock here
    // gLock.lock();
    cout << "Delete FILE " << name << endl;
    int tmp = -1;
    for (int i = 0; i < currentDirectory->childrens.size(); ++i) {
        if (currentDirectory->childrens[i]->name == name) {
            tmp = i;
            // check for external blocks
            for (int j = 0; j < currentDirectory->childrens[i]->externals.size(); ++j) {
                blocks[currentDirectory->childrens[i]->externals[j]].first = false;
                blocks[currentDirectory->childrens[i]->externals[j]].second = "";
                freedBlockList.push_back(currentDirectory->childrens[i]->externals[j]);
            }
            break;
        }
    }

    if (tmp != -1) {
        // remove from childrens
        FDIR *p = currentDirectory->childrens[tmp];
        currentDirectory->childrens[tmp] = currentDirectory->childrens[currentDirectory->childrens.size() - 1];
        currentDirectory->childrens[currentDirectory->childrens.size() - 1] = p;
        currentDirectory->childrens.pop_back();

        // decrement dir childrenCount
        currentDirectory->numberOfChildren--;

        // change size of dir
        currentDirectory->size -= p->size;
        // traverse to parents and change size
        FDIR* par = currentDirectory->parent;
        while (par != NULL) {
            par->size -= p->size;
            par = par->parent;
        }

        // delete file
        freeTree(p);
    }
    else {
        cout << "File not found in the specified directory!\n";
    }
    // memoryMap(root, 0);
    // gLock.unlock();
}

void moveFile(string source, string dest, FDIR* currentDirectory) {
    // source
    // tokenize path
    vector<string> tokens;
    tokenizePath(source, tokens);

    // remove last element
    string sourceName = tokens[tokens.size() - 1];
    tokens.pop_back();

    // change directory
    FDIR* sourceDir = NULL;
    if (source[0] == '/' && tokens.size() == 1) {sourceDir= root;}
    else if (tokens.size() != 0) {
        currentDirectory = chDir(root, currentDirectory, tokens, source[0] == '/' ? 1 : 0);
    }

    // change directory
    FDIR* destDir = NULL;
    if (tokens.size() != 0) {
        destDir = chDir(root, currentDirectory, dest);
    }

    // get File
    int tmp = -1;
    for (int i = 0; i < sourceDir->childrens.size(); ++i) {
        if (sourceName == sourceDir->childrens[i]->name) {
            tmp = i;
            break;
        }
    }

    // gLock.lock();
    cout << "Moved File " << sourceName << endl;
    // change size
    sourceDir->size -= sourceDir->childrens[tmp]->size;
    FDIR* par = sourceDir->parent;
    while (par != NULL) {
        par->size -= sourceDir->childrens[tmp]->size;
        par = par->parent;
    }

    destDir->size += sourceDir->childrens[tmp]->size;
    par = destDir->parent;
    while (par != NULL) {
        par->size += sourceDir->childrens[tmp]->size;
        par = par->parent;
    }

    sourceDir->childrens[tmp]->parent = destDir;

    destDir->numberOfChildren++;
    destDir->childrens.push_back(sourceDir->childrens[tmp]);

    FDIR *p = sourceDir->childrens[tmp];
    sourceDir->childrens[tmp] = sourceDir->childrens[sourceDir->childrens.size() - 1];
    sourceDir->childrens[sourceDir->childrens.size() - 1] = p;
    sourceDir->childrens.pop_back();
    sourceDir->numberOfChildren--; 

    // gLock.unlock();
}

FDIR* chDir(FDIR* root, FDIR* currentDirectory, string path) {
    vector<string> tokens;
    tokenizePath(path, tokens);
    if (path.length() == 1 && path[0] == '/') {return root;}
    return chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
}

FDIR* chDir(FDIR* root, FDIR* currentDirectory, vector<string>& tokens, int startAtRoot) {
    FDIR* tmpDir = currentDirectory; // if path is wrong. then stay in same dir
    bool valid = false;
    currentDirectory = startAtRoot ? root : currentDirectory; 
    for (int i = startAtRoot ? 1 : 0; i < tokens.size(); ++i) {
        if (tokens[i] == "..") {
            if (currentDirectory->parent == NULL) {
                valid = false;
                break;
            }
            else {
                currentDirectory = currentDirectory->parent;
                valid = true;
            }
        }
        else if (tokens[i] == ".") {continue;}
        else {
            for (int j = 0; j < currentDirectory->childrens.size(); ++j) {
                if (currentDirectory->childrens[j]->name == tokens[i]) {
                    currentDirectory = currentDirectory->childrens[j];
                    valid = true;
                    break;
                }
                else {
                    valid = false; // see
                }
            }
        }
    }

    if (!valid) {
        cout << "Invalid path\n";
        currentDirectory = tmpDir;
    }

    return currentDirectory;
}

string getPath(FDIR* currentDirectory) {
    string path = currentDirectory->name;
    currentDirectory = currentDirectory->parent;

    while (currentDirectory != NULL) {
        path = currentDirectory->name + "/" + path;
        currentDirectory = currentDirectory->parent;
    }

    return path;
}

void tokenizePath(string path, vector<string>& tokens) {
    stringstream ss(path);
    string tmp;
    vector<string> tmps;

    while(getline(ss, tmp, '/')) {
        tokens.push_back(tmp);
    }
}

FileObject* openFile(string path, char mode, FDIR* currentDirectory) {
    FileObject* fileObject = NULL;
    
    // tokenize path
    vector<string> tokens;
    tokenizePath(path, tokens);
    string name = tokens[tokens.size() - 1];

    // change directory
    if (tokens.size() != 0) {
        currentDirectory = chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
    }

    if (currentDirectory->type == 1) {
        fileObject = new FileObject(currentDirectory, mode, name);

        for (int i = 0; i < openFileTable.size(); ++i) {
            if (openFileTable[i]->name == name) {
                return NULL;
            }
        }
        openFileTable.push_back(fileObject);
    }

    return fileObject;
}

void closeFile(string path) {
    vector<string> tokens;
    tokenizePath(path, tokens);
    string name = tokens[tokens.size() - 1];
    
    for (int i = 0; i < openFileTable.size(); ++i) {
        if (openFileTable[i]->name == name) {
            delete openFileTable[i];
            openFileTable.erase(openFileTable.begin() + i);
            break;
        }
    }
}

void getFreeBlockIndex() {
    if (hasReachedEnd) {
        if (freedBlockList.size() == 0) {
            freeBlock = -1;
        }
        else {
            freeBlock = freedBlockList[freedBlockList.size() - 1];
            freedBlockList.pop_back();
        }
    }
    else {
        freeBlock++;

        if (freeBlock == TOTAL_MEMORY / BLOCK_SIZE) {
            hasReachedEnd = true;
            getFreeBlockIndex();
        }
    }
}

void writeContent(vector<pair<bool, string>>& blocks) {
    FILE* p = fopen("sample.dat", "w");


    for (int i = 0; i < blocks.size(); ++i) {
        CONTENT* cnt = new CONTENT();

        strcpy(cnt->content, blocks[i].second.c_str());

        cnt->used = blocks[i].first;

        fwrite(cnt, sizeof(CONTENT), 1, p);
        
        delete cnt;
    }

    fclose(p);
}

void readContent(vector<pair<bool, string>>& blocks) {
    FILE* p = fopen("sample.dat", "r");

    CONTENT *cnt = new CONTENT();
    for (int i = 0; i < blocks.size(); ++i) {
        fread(cnt, sizeof(CONTENT), 1, p);

        string s(cnt->content);
        s = s.substr(0, BLOCK_SIZE);
        blocks[i].second = s;
        
        blocks[i].first = cnt->used;
    }

    fclose(p);
}

void displayFileInfo(FDIR* root, string &c){
    queue<FDIR*> q;
    FDIR *p = root;

    if (p != NULL){
        q.push(p);
    }

    while (!q.empty()){
        p = q.front();
        q.pop();

        string tmp = "";
        for (int i = 0; i < p->externals.size(); ++i) {
            tmp += to_string(p->externals[i]) + " ";
        }

        if (p->type == 1) {
            c += "File name:\t\t" + p->name + '\n';
            c += "Parent Directory:\t" + p->parent->name + '\n';
            c += "Size:\t\t\t" + to_string(p->size) + '\n';
            c += "Number of Blocks:\t" + to_string(p->numberOfBlocks) + '\n';
            c += "Blocks List:\t\t" + tmp + '\n';
            c += "\n\n";
        }

        for (int i = 0; i < p->childrens.size(); ++i){
            if (p->childrens[i] != NULL) {
                q.push(p->childrens[i]);
            }
        }
    }
}

void memoryMap(FDIR* root, int spaces, string& s) {
    if (root == NULL) {
        return;
    }
    for (int i = 0; i < spaces; ++i) {s += "-";}

    s = s + root->name + "\n";
    for (int i = 0; i < root->childrens.size(); ++i) {
        memoryMap(root->childrens[i], spaces + 1, s);
    }
}