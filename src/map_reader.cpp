#include "map_reader.hpp"

struct node* readNodesFromMap(FILE *f){
    int n = 1024;
    struct node *nodes = (struct node *)malloc(sizeof(struct node) * n);
    for(int i = 0; i < 32; i++) {
        for(int j = 0; j < 32; j++) {
            int index = i * 32 + j;
            nodes[index].id = index;
            nodes[index].x = j * 20.0;
            nodes[index].y = i * 20.0;
        }
    }
    fwrite(&n, sizeof(int), 1, f);
    fwrite(nodes, sizeof(struct node), n, f);
    return nodes;
}

struct node* readNodesFromCache(FILE *f){
    int n;
    fread(&n, sizeof(int), 1, f);
    struct node *nodes = (struct node*)malloc(sizeof(struct node) * n);
    fread(nodes, sizeof(struct node), n, f);
    printf("Nodes: %d\n", n);
    return nodes;
}

map readMapFromMap(FILE *f){
    map mapObj;
    mapObj.readChunks(16, 4);
    mapObj.writeToCache(f);
    return mapObj;
}

map readMapFromCache(FILE *f){
    map mapObj;
    mapObj.readFromCache(f);
    return mapObj;
}

int doItAll(){
    FILE *f = fopen("map_data/map.bin", "rb");
    struct node *nodes;
    map mapObj;
    if(!f){
        f = fopen("map_data/map.bin", "wb");
        nodes = readNodesFromMap(f);
        mapObj = readMapFromMap(f);
        fclose(f);
        f = fopen("map_data/map.bin", "rb");
    }
    else{
        nodes = readNodesFromCache(f);
        mapObj = readMapFromCache(f);
    }
    mapObj.wrapUp();
    free(nodes);
    if(f) fclose(f);
    return 0;
}
