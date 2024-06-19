#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 1024

void print_last_N_lines(const char *filename, int num_lines) {
    /*
     * Hint:
     * 
     * There are many possible solutions. Below is a simple straightforward one:
     *
     * 1. `open()` file
     * 2. `lseek()` to end-of-file
     * 3. Scan backward from end-of-file to the (n+1)-th newline character '\n'. Record the current position.
     * 4. `read()` from the current position to end-of-file into a buffer. Note that the buffer is limited in size.
     * 5. Print the buffer to stdout.
     */
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return;
    }

    /*
     * Fill in your code here.
     */


    char arr[num_lines][100];
    int curr_pos = num_lines - 1;

    FILE * fp = fopen(filename,"r");
    FILE * prev_fp = (FILE*)malloc(sizeof(FILE));
    //Modified
    fseek(fp,-1,SEEK_END);
    
    int line_num = -1;

    while(1){
    //Back to Begin

    if(line_num != -1)
        fseek(fp,-1,SEEK_CUR);

    
    char current = fgetc(fp);
    if(line_num != -1)
        fseek(fp,-1,SEEK_CUR);

    char buffer[50];
    int flag = 0;

    
    if(current == '\n'){
        ++line_num;
        if(line_num > 0){
            fseek(fp,1,SEEK_CUR);
            *prev_fp = *fp;
            fgets(buffer,sizeof(buffer),fp);
            *fp = *prev_fp;
            fseek(fp,-1,SEEK_CUR);

            
            memcpy(arr[curr_pos],buffer,sizeof(buffer));
            curr_pos--;
        }

        if(line_num == num_lines + 1)
            flag = 1;

    }
    else if(ftell(fp) == 0){
         ++line_num;
        fgets(buffer,sizeof(buffer),fp);

        memcpy(arr[curr_pos],buffer,sizeof(buffer));
        curr_pos--;
        flag = 1;
    
    }
    

    if(flag){
        
        for(int i = curr_pos + 1;i < num_lines;++i){
        printf("%s",arr[i]);
        }

        break;
    }

    }

    //
    fclose(fp);
    //fclose(prev_fp);
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <filename> <number of lines>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int num_lines = atoi(argv[2]);

    print_last_N_lines(filename, num_lines);

    return 0;
}
