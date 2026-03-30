#include <stdio.h>
#include "map_reader.hpp"

struct node* readNodesFromMap(FILE *f){
    int n = 5;
    double lat, lon;
    struct node *nodes = (struct node *)malloc(sizeof(struct node) * n);
    for(int i = 0; i<n; i++){
        nodes[i].id = i;
        lat = i*n + 2;
        lon = i*n + 1;
        nodes[i].x = lat - 2;
        nodes[i].y = lon - 1;
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
    return nodes;
}

map readMapFromMap(FILE *f){
    int size = 5;
    int n = 10;
    map mapObj;
    mapObj.readChunks(size, n);
    mapObj.writeToCache(f);
    return mapObj;
}

map readMapFromCache(FILE *f){
    map mapObj;
    mapObj.readFromCache(f);
    return mapObj;
}

int main(){
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
