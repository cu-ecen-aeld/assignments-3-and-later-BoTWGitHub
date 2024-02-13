#include <stdio.h>
#include <syslog.h>

int main(int argc, char* argv[])
{
    FILE *fp;
    openlog(NULL, 0, LOG_USER);

    if(argc != 3){
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc-1);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    fp = fopen(argv[1], "w");
    if(fp == NULL){
        syslog(LOG_ERR, "fopen %s failed", argv[1]);
    }
    fprintf(fp, "%s\n", argv[2]);
    fclose(fp);

    return 0;
}