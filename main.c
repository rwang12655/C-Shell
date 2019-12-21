//
//  main.c
//  shell1
//
//  Created by Robert Wang on 10/25/19.
//  Copyright © 2019 Robert Wang. All rights reserved.
//

#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

typedef struct {
    bool read_only;
    char *first_byte;
    char *current_byte;
    int size;
} ok;


bool fwrite(BYTE *buffer, int size, FILE *stream){
    if(stream->read_only){
        return false;
    } else {
        for(int i = 0; i < size; i++){
            (stream->current_byte)++ = buffer[i];
        }
        return true;
    }
}
