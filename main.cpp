// add same file and dir name check
// file does not exist check when deleting
// check for running out of blocks in append
// dir not exist or file not exist in move

#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <sstream>
#include <utility>
#include <string.h>

using namespace std;

#define BLOCK_SIZE 16
#define TOTAL_MEMORY 1024

bool hasReachedEnd = false;

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

void getFreeBlockIndex(vector<int>&, int&);

struct FileObject {
    FDIR* file;
    char mode;

    FileObject(FDIR* file, char mode) {
        this->file = file;
        this->mode = mode;
    }

    void append(vector<pair<bool, string>>& blocks, int& freeBlock, vector<int>& freedBlockList, string data) {
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
                    getFreeBlockIndex(freedBlockList, freeBlock);
                    dataLength -= BLOCK_SIZE;
                    spaceLeftInLastBlock = spaceLeftInLastBlock + BLOCK_SIZE;
                    this->file->numberOfBlocks++;
                }
                else {
                    blocks[freeBlock].second += data.substr(spaceLeftInLastBlock, dataLength);
                    this->file->externals.push_back(freeBlock);
                    getFreeBlockIndex(freedBlockList, freeBlock);
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
    }

    string read(vector<pair<bool, string>>& blocks) {
        string data = "";

        for (int i = 0; i < this->file->externals.size(); ++i) {
            data += blocks[this->file->externals[i]].second;
        }

        return data;
    }

    void writeAt(vector<pair<bool, string>>& blocks, int& freeBlock, vector<int>& freedBlockList, string data, int writePos) {
        string original = this->read(blocks);

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
        string original = this->read(blocks);
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

    void truncateFile(vector<pair<bool, string>>& blocks, vector<int>& freedBlockList, int size) {
        string original = this->read(blocks);
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
    }
};

// utilities
void bfs(FDIR *);
FDIR* reconstruct();
FDIR* createTree(int&, vector<pair<bool, string>>&, vector<int>&);
void freeTree(FDIR*);
string getPath(FDIR*);
void tokenizePath(string, vector<string>&);
void getFreeBlockIndex(vector<int>&);
void writeContent(vector<pair<bool, string>>&);
void readContent(vector<pair<bool, string>>&);

// lab functions
FDIR* chDir(FDIR*, FDIR*, vector<string>&, int);
FDIR* chDir(FDIR*, FDIR*, string);
void mkDir(string, FDIR*, FDIR*);
void createFile(string, FDIR*, FDIR*, int&, vector<pair<bool, string>>&, vector<int>&);
void deleteFile(string, FDIR*, FDIR*, vector<pair<bool, string>>&, vector<int>&);
void moveFile(string, string, FDIR*, FDIR*);
FileObject* openFile(string, char, FDIR*, FDIR*);

int main() {
    // blocks vector with fixed size
    vector<pair<bool, string>> blocks(TOTAL_MEMORY/BLOCK_SIZE);
    // freed block list
    vector<int> freedBlockList;
    // keeps track of the free block
    int freeBlock = 0;
    
    FDIR *root = createTree(freeBlock, blocks, freedBlockList);
    FDIR *currentDirectory = root;

    do {
        int choice;
        cout << "Enter choice: ";
        cin >> choice;

        switch (choice) {
            case 1: {
                string name;
                cout << "Enter directory name or path: ";
                cin >> name;
                mkDir(name, root, currentDirectory);
                cout << "Current working directory: " + getPath(currentDirectory) << '\n';
                break;
            }
            case 2: {
                string name;
                cout << "Enter File name or path: ";
                cin >> name;
                createFile(name, root, currentDirectory, freeBlock, blocks, freedBlockList);
                cout << "Current working directory: " + getPath(currentDirectory) << '\n';
                break;
            }
            case 3: {
                string path;
                cout << "Enter Path: ";
                cin >> path;
                currentDirectory = chDir(root, currentDirectory, path);
                cout << "Current working directory: " + getPath(currentDirectory) << '\n';
                break;
            }
            case 4: {
                string path;
                cout << "Enter file name or path: ";
                cin >> path;
                deleteFile(path, root, currentDirectory, blocks, freedBlockList);
                cout << "Current working directory: " + getPath(currentDirectory) << '\n';
                break;
            }
            case 5: {
                string path;
                cout << "Enter file name or path to open: ";
                cin >> path;
                
                char mode;
                cout << "Enter mode in which to open file: ";
                cin >> mode;

                FileObject* fileObject = openFile(path, mode, root, currentDirectory);

                if (fileObject != NULL) {
                    cout << "File opened\n";
                }
                else {
                    cout << "File could not be found\n";
                    continue;
                }

                if (mode == 'w') {
                    cout << "1. Append\n";
                    cout << "2. Write at\n";

                    cout << "Enter operation: ";
                    int use;
                    cin >> use;

                    if (use == 1) {
                        string data;
                        cout << "Enter content to write in file: ";
                        cin.ignore();
                        getline(cin, data);
                        fileObject->append(blocks, freeBlock, freedBlockList, data);
                    }
                    else if (use == 2) {
                        string data;
                        cout << "Enter content to write in file: ";
                        cin.ignore();
                        getline(cin, data);

                        int writePos;
                        cout << "Enter write position: ";
                        cin >> writePos;

                        fileObject->writeAt(blocks, freeBlock, freedBlockList, data, writePos);
                    }
                }
                else if (mode == 'r') {
                    cout << "1. Read all\n";
                    cout << "2. Read from\n";

                    cout << "Enter operation: ";
                    int use;
                    cin >> use;

                    if (use == 1) {cout << fileObject->read(blocks) << '\n';}
                    else if (use == 2) {
                        int readPos, readSize;
                        cout << "Enter read position: ";
                        cin >> readPos;
                        cout << "Enter read size: ";
                        cin >> readSize;
                        cout << fileObject->readFrom(blocks, readPos, readSize) << '\n';
                    }
                }
                else if (mode == 'm') {
                    int from, size, to;
                    
                    cout << "Enter from: ";
                    cin >> from;
                    
                    cout << "Enter size: ";
                    cin >> size;
                    
                    cout << "Enter to: ";
                    cin >> to;

                    fileObject->moveWithin(blocks, from, to, size);
                }
                else if (mode == 't') {
                    int size;

                    cout << "Enter size: ";
                    cin >> size;

                    fileObject->truncateFile(blocks, freedBlockList, size);
                }
                break;
            }
            case 6: {
                string source;
                cout << "Enter source: ";
                cin >> source;

                string dest;
                cout << "Enter dest: ";
                cin >> dest;

                moveFile(source, dest, root, currentDirectory);
                cout << "Current working directory: " + getPath(currentDirectory) << '\n';
                break;
            }
            case 7: {
                freeTree(root);
                root = reconstruct();
                currentDirectory = root;
                readContent(blocks);
                break;
            }
            default: {
                bfs(root);
                writeContent(blocks);
                exit(0);
            }
        }
    } while(true);

    return 0;
}

FDIR* createTree(int& freeBlock, vector<pair<bool, string>>& blocks, vector<int>& freedBlockList) {
    FDIR *root = new FDIR("root", 0, NULL);

    mkDir("dir1", root, root);
    mkDir("dir2", root, root);
    mkDir("dir3", root, root);
    mkDir("dir1/dir4", root, root);
    mkDir("dir1/dir4/dir7", root, root);
    mkDir("dir3/dir5", root, root);
    mkDir("dir3/dir5/dir6", root, root);

    createFile("f1", root, root, freeBlock, blocks, freedBlockList);
    createFile("f2", root, root, freeBlock, blocks, freedBlockList);
    createFile("f3", root, root, freeBlock, blocks, freedBlockList);
    createFile("dir1/f4", root, root, freeBlock, blocks, freedBlockList);
    createFile("dir2/f5", root, root, freeBlock, blocks, freedBlockList);
    createFile("dir3/dir5/dir6/f6", root, root, freeBlock, blocks, freedBlockList);
    

    // FDIR *dir1 = new FDIR("dir1", 0, root);
    // FDIR *dir2 = new FDIR("dir2", 0, root);
    // FDIR *dir3 = new FDIR("dir3", 0, root);
    // FDIR *dir4 = new FDIR("dir4", 0, dir1);
    // FDIR *dir5 = new FDIR("dir5", 0, dir3);
    // FDIR *dir6 = new FDIR("dir6", 0, dir5);
    // FDIR *dir7 = new FDIR("dir7", 0, dir4);

    // FDIR *f1 = new FDIR("f1", 1, root);
    // FDIR *f2 = new FDIR("f2", 1, root);
    // FDIR *f3 = new FDIR("f3", 1, root);
    // FDIR *f4 = new FDIR("f4", 1, dir1);
    // FDIR *f5 = new FDIR("f5", 1, dir2);
    // FDIR *f6 = new FDIR("f6", 1, dir6);

    // root->childrens.push_back(dir1);
    // root->childrens.push_back(dir2);
    // root->childrens.push_back(dir3);
    // root->childrens.push_back(f1);
    // root->childrens.push_back(f2);
    // root->childrens.push_back(f3);
    // root->numberOfChildren = root->childrens.size();

    // dir1->childrens.push_back(dir4);
    // dir1->childrens.push_back(f4);
    // dir1->numberOfChildren = dir1->childrens.size();

    // dir2->childrens.push_back(f5);
    // dir2->numberOfChildren = dir2->childrens.size();

    // dir3->childrens.push_back(dir5);
    // dir3->numberOfChildren = dir3->childrens.size();

    // dir4->childrens.push_back(dir7);
    // dir4->numberOfChildren = dir4->childrens.size();

    // dir5->childrens.push_back(dir6);
    // dir5->numberOfChildren = dir5->childrens.size();

    // dir6->childrens.push_back(f6);
    // dir6->numberOfChildren = dir6->childrens.size();

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

void mkDir(string path, FDIR* root, FDIR* currentDirectory) {
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

void createFile(string path, FDIR* root, FDIR* currentDirectory, int& freeBlock, vector<pair<bool, string>>&blocks, vector<int>& freedBlockList) {
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

    newDir->externals.push_back(freeBlock);
    blocks[freeBlock].first = true;
    getFreeBlockIndex(freedBlockList, freeBlock);

    if (freeBlock == TOTAL_MEMORY / BLOCK_SIZE) {
        hasReachedEnd = true;
        getFreeBlockIndex(freedBlockList, freeBlock);
    }

    // add and increment the children of parent
    currentDirectory->childrens.push_back(newDir);
    currentDirectory->numberOfChildren++;
}

void deleteFile(string path, FDIR* root, FDIR* currentDirectory, vector<pair<bool, string>>& blocks, vector<int>& freedBlockList) {
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
        freeTree(currentDirectory->childrens[tmp]);
    }
    else {
        cout << "File not found in the specified directory!\n";
    }
}

void moveFile(string source, string dest, FDIR* root, FDIR* currentDirectory) {
    // source
    // tokenize path
    vector<string> tokens;
    tokenizePath(source, tokens);

    // remove last element
    string sourceName = tokens[tokens.size() - 1];
    tokens.pop_back();

    // change directory
    FDIR* sourceDir = NULL;
    if (tokens.size() != 0) {
        sourceDir = chDir(root, currentDirectory, tokens, source[0] == '/' ? 1 : 0);
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
                    valid = false;
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

FileObject* openFile(string path, char mode, FDIR* root, FDIR* currentDirectory) {
    FileObject* fileObject = NULL;
    
    // tokenize path
    vector<string> tokens;
    tokenizePath(path, tokens);

    // change directory
    if (tokens.size() != 0) {
        currentDirectory = chDir(root, currentDirectory, tokens, path[0] == '/' ? 1 : 0);
    }

    if (currentDirectory->type == 1) {
        fileObject = new FileObject(currentDirectory, mode);
    }

    return fileObject;
}

void getFreeBlockIndex(vector<int>& freedBlockList, int& freeBlock) {
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