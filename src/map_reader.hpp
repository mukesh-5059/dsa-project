#include <stdlib.h>    

struct node {
    int id;
    double x, y;
};

class way {
    int id;
    int nodeCount;
    int *nodes; 
    public:
    way(){
        id = 1;
        nodeCount = 2;
        nodes = (int *)malloc(nodeCount * sizeof(int));
        nodes[0] = 1;
        nodes[1] = 2;
    }
    void writeToCache(FILE *f){
        int ind;
        fwrite(&id, sizeof(int), 1, f);
        fwrite(&nodeCount, sizeof(int), 1, f);
        for(int i = 0; i<nodeCount; i++){
            fwrite(nodes, sizeof(int), nodeCount, f);
        }
    }
    void readFromCache(FILE *f){
        fread(&id, sizeof(int), 1, f);
        fread(&nodeCount, sizeof(int), 1, f);
        nodes = (int *)malloc(nodeCount * sizeof(int));
        fread(nodes, sizeof(int), nodeCount, f);
    }
    void wrapUp(){
        free(nodes);
    }
};

class chunk{
    way *ways;
    int wayCount;
    public:
    chunk(){
        ways = NULL;
        wayCount = 0;
    }
    chunk(int wayCount){
        this->wayCount = wayCount;
        ways = (way *)malloc(sizeof(way) * wayCount);
        for(int  i = 0; i<wayCount; i++){
            ways[i] = way();
        }
    }
    void writeToCache(FILE *f){
        fwrite(&wayCount, sizeof(int), 1, f);
        for(int i = 0; i<wayCount; i++) ways[i].writeToCache(f);
    }
    void readFromCache(FILE *f){
        fread(&wayCount, sizeof(int), 1, f);
        ways = (way *)malloc(sizeof(way) * wayCount);
        for(int i = 0; i < wayCount; i++) {
            ways[i].readFromCache(f);
        }
    }
    void wrapUp(){
        if(ways) {
            for(int i  = 0; i<wayCount; i++) ways[i].wrapUp();
            free(ways);
        }
    }
};


class map{
    int chunkSize, noOfChunks;
    chunk *chunks;
    public:
    map(){
        chunks = NULL;
        chunkSize = 0;
        noOfChunks = 0;
    }
    void readChunks(int chunkSize, int noOfChunks){
        this->noOfChunks = noOfChunks;
        this->chunkSize = chunkSize;
        chunks = (chunk *)malloc(sizeof(chunk) * noOfChunks);
        for(int i = 0; i<noOfChunks; i++){
            chunks[i] = chunk(chunkSize);
        }
    }
    void writeToCache(FILE *f){
        fwrite(&noOfChunks, sizeof(int), 1, f);
        for(int i = 0; i< noOfChunks; i++){
            chunks[i].writeToCache(f);
        }
    }
    void readFromCache(FILE *f){
        fread(&noOfChunks, sizeof(int), 1, f);
        chunks = (chunk *)malloc(sizeof(chunk) * noOfChunks);
        for(int i = 0; i < noOfChunks; i++){
            chunks[i] = chunk();
            chunks[i].readFromCache(f);
        }
    }
    void wrapUp(){
        if(chunks) {
            for(int i = 0; i < noOfChunks; i++) chunks[i].wrapUp();
            free(chunks);
        }
    }
};


struct node* readNodesFromMap(FILE *f);
struct node* readNodesFromCache(FILE *f);
map readMapFromMap(FILE *f);
map readMapFromCache(FILE *f);